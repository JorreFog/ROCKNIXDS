#!/usr/bin/env python3
"""perf-logs.py <dir>: one line per performance log under <dir>/<device>/*.jsonl (the device-logs branch's
docs/data/device): fps, dropped and repeated frames a second, the drop sources, the CPU clock, battery current,
temperature, governor steps and late latches. Used for docs/perf-logs-1.5-beta.md, docs/perf-logs-1.5.md and
docs/perf-logs-1.5.12.md.
The frame numbers count play only: a second in DraStic's menu (the bottom panel flips, the top doesn't) or under 10
presents (loading, paused) is left out of them and counted on its own. "busy<top" is the share of the slow play
seconds (under 57 fps) in which DraStic's threads were busy (a core or more) and the clock wasn't at its top.
"res" is the 3D resolution: the summary's (from the release after 1.6.1), else the governor's remembered-drops
file name (1x or 2x; 3x counts as 2x there up to SuperDrastic 0.5.0-beta.1). After the sessions, one line per
ROCKNIXDS version: play, fps and the slow play seconds split into busy below the top clock, at the top clock, and
the rest."""
import json, re, sys, glob, os, collections, statistics as st
rows = []
for f in sorted(glob.glob(sys.argv[1] + "/*/*.jsonl")):
    S = None; samples = []; pres = []; gov = []; late = 0; other = collections.Counter(); menu = idle = slow = held = attop = 0
    for l in open(f):
        d = json.loads(l)
        if d.get("summary"): S = d; continue
        samples.append(d)
        for x in d.get("log") or []:
            m = re.match(r"\[dsflip\] present/s=([\d.]+) .*?dropped=(\d+).*?flips top=(\d+) bot=(\d+).*?drop-src=(\d+) drop-q=(\d+) drop-buf=(\d+)(?: dup=(\d+))? repeat=(\d+)", x)
            if m:
                p = [float(v) if v else 0 for v in m.groups()]
                if p[2] == 0 and p[3] > 0: menu += 1; continue
                if p[0] < 10: idle += 1; continue
                pres.append(p)
                if p[0] < 57:
                    slow += 1
                    busy, low = (d.get("game_cpu") or 0) >= 100, (d.get("cpu_mhz") or 0) < (d.get("cpu_hw_max_mhz") or 1990)
                    held += busy and low; attop += not low
                continue
            if x.startswith("[cpugov]"): gov.append(x)
            elif x.startswith("[late"): late += 1
    if not samples: continue
    dev = f.split("/")[-2]; game = (S or samples[0])["game"]["rom"] if (S or samples[0]).get("game") else "?"
    n = len(pres) or 1
    fps = [p[0] for p in pres]
    mhz = [s["cpu_mhz"] for s in samples if s.get("cpu_mhz")]
    ma = [s["bat"]["ma"] for s in samples if s.get("bat") and s["bat"].get("ma") is not None and s["bat"].get("status") == "Discharging"]
    tmax = max((s["temp"]["cpu"] for s in samples if s.get("temp") and s["temp"].get("cpu")), default=0)
    hist = collections.Counter(mhz)
    g = (S or samples[0]).get("game") or {}
    res = g.get("res3d") or next((m[1] + "x" for x in gov for m in [re.search(r"remembered drops for .*\.(\d)x(?:,|:|$)", x)] if m), "?")
    rows.append(dict(dev=dev[:6], file=os.path.basename(f)[:15], game=game[:34], ver=(S or {}).get("rocknixds", "?"), lib=(S or {}).get("lib", ""),
        prof=(S or {}).get("profile", "?"), q=(S or {}).get("queue", "?"), shader=(S or samples[0]).get("game", {}).get("shader", "?"),
        secs=len(samples), fps=st.mean(fps) if fps else 0, below=100 * sum(1 for x in fps if x < 59.5) / n,
        drops=sum(p[1] for p in pres) / n, repeat=sum(p[8] for p in pres) / n, dsrc=sum(p[4] for p in pres) / n, dq=sum(p[5] for p in pres) / n,
        dbuf=sum(p[6] for p in pres) / n, mhz=st.mean(mhz) if mhz else 0, top=",".join(f"{k}:{100*v//len(mhz)}" for k, v in sorted(hist.items()) if 100 * v // len(mhz) >= 5),
        ma=-st.mean(ma) if ma else 0, tmax=tmax, gov=len(gov), late=late, summary=bool(S),
        play=len(pres), menu=menu, idle=idle, held=100 * held / slow if slow else 0, res=res, model=(S or {}).get("model") or "?",
        slow=slow, nheld=held, attop=attop, fpssum=sum(fps), drop_n=sum(p[1] for p in pres)))
print(f"{'dev':6} {'when':15} {'game':34} {'ver':22} {'prof':11} q {'shader':10} res {'play':>5} {'menu':>4} {'idle':>4} {'fps':>5} {'<59.5%':>6} {'drop/s':>6} {'rep/s':>5} {'src':>5} {'q':>5} {'buf':>5} {'MHz':>5} {'mA':>5} {'T':>4} gov late busy<top  clocks(MHz:%)")
for r in rows:
    print(f"{r['dev']:6} {r['file']:15} {r['game']:34} {r['ver'][:22]:22} {r['prof']:11} {r['q']} {r['shader'][:10]:10} {r['res']:3} {r['play']:5} {r['menu']:4} {r['idle']:4} {r['fps']:5.2f} {r['below']:6.1f} {r['drops']:6.3f} {r['repeat']:5.2f} {r['dsrc']:5.2f} {r['dq']:5.2f} {r['dbuf']:5.2f} {r['mhz']:5.0f} {r['ma']:5.0f} {r['tmax']:4.0f} {r['gov']:3} {r['late']:4} {r['held']:7.0f}%  {r['top']}" + ("" if r["summary"] else "  (no summary)"))
print()
print(f"{'version':22} {'sess':>4} {'units':>5} {'play':>6} {'fps':>5} {'<57%':>5} {'busy<top':>8} {'at top':>6} {'other':>5} {'drop/s':>6}")
by = collections.defaultdict(list)
for r in rows: by[r["ver"]].append(r)
for v, rs in sorted(by.items(), key=lambda kv: ([int(n) for n in re.findall(r"\d+", kv[0].split(" ")[0])], kv[0])):
    p = sum(r["play"] for r in rs) or 1; sl = sum(r["slow"] for r in rs); h = sum(r["nheld"] for r in rs); t = sum(r["attop"] for r in rs)
    print(f"{v[:22]:22} {len(rs):4} {len({r['dev'] for r in rs}):5} {p:6} {sum(r['fpssum'] for r in rs) / p:5.2f} {100 * sl / p:5.1f} {h:8} {t:6} {sl - h - t:5} {sum(r['drop_n'] for r in rs) / p:6.3f}")
