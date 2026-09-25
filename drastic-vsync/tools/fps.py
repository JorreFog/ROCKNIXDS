import sys, collections, statistics as st
for fn in sys.argv[1:]:
    L=open(fn).read().splitlines()
    hdr=L[0]
    pf=[tuple(map(int,l.split()[1:])) for l in L if l.startswith("pf ")]
    if not pf: print(fn,"no pf"); continue
    t0=pf[0][0]; pf=[p for p in pf if p[0]-t0>15e6]   # skip boot/logo 15s
    span=(pf[-1][0]-pf[0][0])/1e6
    shown=[p for p in pf if p[3]==1]
    steps=collections.Counter(b[2]-a[2] for a,b in zip(shown,shown[1:]))
    per=collections.Counter(int((p[0]-t0)/1e6) for p in pf)
    lat=sorted((p[1]-p[0])/1000 for p in shown)
    print(f"{fn.split('/')[-1]}: presents/s {len(pf)/span:.1f} shown/s {len(shown)/span:.1f} discards {sum(p[3]==2 for p in pf)}")
    print("   vblank steps", dict(sorted(steps.items())), f" lat med {st.median(lat):.1f} p95 {lat[int(len(lat)*.95)]:.1f}")
    print("   per-sec presents:", " ".join(str(per[k]) for k in sorted(per)))
    print("  ", hdr[:200])
