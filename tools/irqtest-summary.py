#!/usr/bin/env python3
"""irqtest-summary.py <outdir>   (on a PC, after tools/irqtest.sh; it runs this itself at the end)

One table per workload, a row per layout, every number the mean over the rounds with the spread (min..max) where it
matters:
  hg      presents/s, drops/s, late flips (seconds whose longest flip interval passed 20 ms), the average CPU clock
          libdsflip's governor held, battery current, the highest SoC temperature
  stress  frames per second over the last 30 s (the heaviest levels) and over the whole run
  irq     for every layout: interrupts per second on each CPU, the share of CPU time spent in interrupts and softirqs,
          the busiest interrupt sources, and how often each CPU entered its deep idle state (per second)
"""
import glob
import json
import os
import re
import sys

LATE_US = 20000


def rd(p):
    try:
        with open(p) as f:
            return f.read()
    except OSError:
        return None


def proc_table(text):
    """/proc/interrupts or /proc/softirqs -> (ncpu, {key: ([count per cpu], name)})"""
    lines = text.splitlines()
    ncpu = len(lines[0].split())
    out = {}
    for line in lines[1:]:
        key, sep, rest = line.partition(":")
        if not sep:
            continue
        f = rest.split()
        counts = []
        for x in f[:ncpu]:
            if not x.isdigit():
                break
            counts.append(int(x))
        if not counts:
            continue
        counts += [0] * (ncpu - len(counts))
        out[key.strip()] = (counts, " ".join(f[len(counts):]))
    return ncpu, out


def irq_run(d):
    """the interrupts, softirqs, CPU time split and idle entries of one run (<tag>.irq/), as rates"""
    t0, t1 = rd(f"{d}/before.uptime"), rd(f"{d}/after.uptime")
    if not t0 or not t1 or not rd(f"{d}/after.interrupts"):
        return None
    secs = float(t1) - float(t0)
    if secs <= 0:
        return None
    ncpu, a = proc_table(rd(f"{d}/before.interrupts"))
    _, b = proc_table(rd(f"{d}/after.interrupts"))
    per_cpu = [0.0] * ncpu
    sources = []
    for k, (cb, name) in b.items():
        ca = a.get(k, ([0] * ncpu, ""))[0]
        r = [(y - x) / secs for x, y in zip(ca, cb)]
        if k.isdigit() or k.startswith("IPI"):
            for i, v in enumerate(r):
                per_cpu[i] += v
        if sum(r) >= 1 and k != "Err":
            sources.append((sum(r), k, name, r))
    sources.sort(reverse=True)
    soft = {}
    s0, s1 = rd(f"{d}/before.softirqs"), rd(f"{d}/after.softirqs")
    if s0 and s1:
        _, sa = proc_table(s0)
        _, sb = proc_table(s1)
        for k, (cb, _) in sb.items():
            ca = sa.get(k, ([0] * ncpu, ""))[0]
            soft[k] = [(y - x) / secs for x, y in zip(ca, cb)]
    split = None
    c0, c1 = rd(f"{d}/before.stat"), rd(f"{d}/after.stat")
    if c0 and c1:
        v0, v1 = [list(map(int, c.split()[1:9])) for c in (c0, c1)]
        dv = [y - x for x, y in zip(v0, v1)]
        tot = sum(dv) or 1
        split = {"irq": 100.0 * dv[5] / tot, "softirq": 100.0 * dv[6] / tot}
    idle = {}
    i0, i1 = rd(f"{d}/before.idle"), rd(f"{d}/after.idle")
    if i0 and i1:
        def states(t):
            out = {}
            for line in t.splitlines():
                f = line.split()
                if len(f) == 5:
                    out[(f[0], f[2])] = (int(f[3]), int(f[4]))
            return out
        ia, ib = states(i0), states(i1)
        for k, (u, _) in ib.items():
            if k in ia:
                idle[k] = (u - ia[k][0]) / secs
    return {"secs": secs, "ncpu": ncpu, "per_cpu": per_cpu, "sources": sources, "soft": soft, "split": split,
            "idle": idle}


def hg_run(out, tag):
    r = {}
    txt = rd(f"{out}/probe/{tag}.txt") or ""
    m = re.search(r"frames: \d+ s, ([\d.]+) presents/s, ([\d.]+) drops/s", txt)
    if m:
        r["presents"], r["drops"] = float(m.group(1)), float(m.group(2))
    js = rd(f"{out}/probe/{tag}.json")
    if js:
        j = json.loads(js)
        r["mhz"] = j.get("cpu_avg_mhz")
        r["ma"] = j.get("bat_ma")
        t = j.get("temp_c") or []
        r["temp"] = max(t) if t else None
        r["busy"] = j.get("cpu_busy_pct")
    log = rd(f"{out}/logs/hp-{tag}.log") or ""
    iv = [max(int(a), int(b)) for a, b in re.findall(r"max-iv top=(\d+) bot=(\d+)", log)]
    if iv:
        r["late"] = sum(1 for v in iv if v > LATE_US) / len(iv) * 60   # per minute
    return r


def sg_run(out, tag):
    log = rd(f"{out}/logs/sg-{tag}.log") or ""
    fps = [float(x) for x in re.findall(r"present/s=([\d.]+)", log)]
    fps = fps[1:]                           # the first second is DraStic starting
    if not fps:
        return {}
    last = fps[-30:]
    return {"fps_last30": sum(last) / len(last), "fps_all": sum(fps) / len(fps)}


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else None


def cell(xs, fmt, spread=False):
    xs = [x for x in xs if x is not None]
    if not xs:
        return "-"
    s = fmt.format(mean(xs))
    if spread and len(xs) > 1:
        s += (" (" + fmt + ".." + fmt + ")").format(min(xs), max(xs))
    return s


def table(rows, head):
    w = [max(len(str(r[i])) for r in [head] + rows) for i in range(len(head))]
    out = ["  ".join(str(c).ljust(w[i]) if i == 0 else str(c).rjust(w[i]) for i, c in enumerate(r))
           for r in [head] + rows]
    return "\n".join(out)


def collect(out):
    """{layout: {"hg": [run...], "sg": [run...]}} in the order the layouts were first run"""
    runs = {}
    for d in sorted(glob.glob(f"{out}/probe/irq-*.irq"), key=lambda p: int(os.path.basename(p).split("-")[1])):
        tag = os.path.basename(d)[:-4]
        work = tag.split("-")[2]
        layout = (rd(f"{d}/layout") or "?").strip()
        r = hg_run(out, tag) if work == "hg" else sg_run(out, tag)
        r["irq"] = irq_run(d)
        runs.setdefault(layout, {"hg": [], "sg": []})[work].append(r)
    return runs


def summary(out):
    runs = collect(out)
    if not runs:
        return f"no runs in {out}/probe"
    lines = []
    hg = [(l, v["hg"]) for l, v in runs.items() if v["hg"]]
    if hg:
        lines.append(f"HeartGold at 2x, walking ({len(hg[0][1])} rounds)")
        lines.append(table([[l, len(rs),
                             cell([r.get("presents") for r in rs], "{:.2f}"),
                             cell([r.get("drops") for r in rs], "{:.3f}", True),
                             cell([r.get("late") for r in rs], "{:.1f}", True),
                             cell([r.get("mhz") for r in rs], "{:.0f}", True),
                             cell([r.get("busy") for r in rs], "{:.0f}"),
                             cell([r.get("ma") for r in rs], "{:.0f}"),
                             cell([r.get("temp") for r in rs], "{:.1f}")] for l, rs in hg],
                           ["layout", "runs", "presents/s", "drops/s", "late flips/min", "CPU MHz", "CPU %", "bat mA",
                            "max C"]))
        lines.append("")
    sg = [(l, v["sg"]) for l, v in runs.items() if v["sg"]]
    if sg:
        lines.append("3D stress ROM ramp at 2x")
        lines.append(table([[l, len(rs), cell([r.get("fps_last30") for r in rs], "{:.1f}", True),
                             cell([r.get("fps_all") for r in rs], "{:.1f}")] for l, rs in sg],
                           ["layout", "runs", "fps last 30 s", "fps whole run"]))
        lines.append("")
    lines.append("Interrupts (all runs of a layout)")
    rows = []
    for l, v in runs.items():
        irqs = [r["irq"] for r in v["hg"] + v["sg"] if r.get("irq")]
        if not irqs:
            continue
        n = irqs[0]["ncpu"]
        cpu = [mean([x["per_cpu"][i] for x in irqs]) for i in range(n)]
        irq_pct = mean([x["split"]["irq"] for x in irqs if x["split"]])
        soft_pct = mean([x["split"]["softirq"] for x in irqs if x["split"]])
        rows.append([l] + [f"{c:.0f}" for c in cpu] +
                    [f"{irq_pct:.2f}" if irq_pct is not None else "-", f"{soft_pct:.2f}" if soft_pct is not None else "-"])
    if rows:
        n = len(rows[0]) - 3
        lines.append(table(rows, ["layout"] + [f"CPU{i}/s" for i in range(n)] + ["irq %", "softirq %"]))
    for l, v in runs.items():
        irqs = [r["irq"] for r in v["hg"] + v["sg"] if r.get("irq")]
        if not irqs:
            continue
        x = irqs[0]
        lines.append(f"\n{l}: busiest sources (first run, per second per CPU)")
        for tot, k, name, r in x["sources"][:8]:
            lines.append(f"  {k:>6} {' '.join(f'{v:8.0f}' for v in r)}  {name}")
        if x["idle"]:
            deep = {}
            for (c, name), rate in x["idle"].items():
                if name.lower() != "wfi":
                    deep.setdefault(name, []).append(f"{c} {rate:.0f}")
            for name, cs in deep.items():
                lines.append(f"  idle {name} entries/s: {', '.join(sorted(cs))}")
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    print(summary(sys.argv[1]))
