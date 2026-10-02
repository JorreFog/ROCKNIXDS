#!/usr/bin/env python3
"""perf-logs.py <dir>: one line per performance log under <dir>/<device>/*.jsonl (the device-logs branch's
docs/data/device): fps, dropped and repeated frames a second, the drop sources, the CPU clock, battery current,
temperature, governor steps and late latches. Used for docs/perf-logs-1.5-beta.md."""
import json, re, sys, glob, os, collections, statistics as st
rows = []
for f in sorted(glob.glob(sys.argv[1] + "/*/*.jsonl")):
    S = None; samples = []; pres = []; gov = []; late = 0; other = collections.Counter()
    for l in open(f):
        d = json.loads(l)
        if d.get("summary"): S = d; continue
        samples.append(d)
        for x in d.get("log") or []:
            m = re.match(r"\[dsflip\] present/s=([\d.]+) .*?dropped=(\d+).*?max-iv top=(\d+) bot=(\d+).*?drop-src=(\d+) drop-q=(\d+) drop-buf=(\d+)(?: dup=(\d+))? repeat=(\d+)", x)
            if m: pres.append([float(v) if v else 0 for v in m.groups()]); continue
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
    rows.append(dict(dev=dev[:6], file=os.path.basename(f)[:15], game=game[:34], ver=(S or {}).get("rocknixds", "?"), lib=(S or {}).get("lib", ""),
        prof=(S or {}).get("profile", "?"), q=(S or {}).get("queue", "?"), shader=(S or samples[0]).get("game", {}).get("shader", "?"),
        secs=len(samples), fps=st.mean(fps) if fps else 0, below=100 * sum(1 for x in fps if x < 59.5) / n,
        drops=sum(p[1] for p in pres) / n, repeat=sum(p[8] for p in pres) / n, dsrc=sum(p[4] for p in pres) / n, dq=sum(p[5] for p in pres) / n,
        dbuf=sum(p[6] for p in pres) / n, mhz=st.mean(mhz) if mhz else 0, top=",".join(f"{k}:{100*v//len(mhz)}" for k, v in sorted(hist.items()) if 100 * v // len(mhz) >= 5),
        ma=-st.mean(ma) if ma else 0, tmax=tmax, gov=len(gov), late=late, summary=bool(S)))
print(f"{'dev':6} {'when':15} {'game':34} {'ver':22} {'prof':11} q {'shader':10} {'secs':>5} {'fps':>5} {'<59.5%':>6} {'drop/s':>6} {'rep/s':>5} {'src':>5} {'q':>5} {'buf':>5} {'MHz':>5} {'mA':>5} {'T':>4} gov late  clocks(MHz:%)")
for r in rows:
    print(f"{r['dev']:6} {r['file']:15} {r['game']:34} {r['ver'][:22]:22} {r['prof']:11} {r['q']} {r['shader'][:10]:10} {r['secs']:5} {r['fps']:5.2f} {r['below']:6.1f} {r['drops']:6.3f} {r['repeat']:5.2f} {r['dsrc']:5.2f} {r['dq']:5.2f} {r['dbuf']:5.2f} {r['mhz']:5.0f} {r['ma']:5.0f} {r['tmax']:4.0f} {r['gov']:3} {r['late']:4}  {r['top']}" + ("" if r["summary"] else "  (no summary)"))
