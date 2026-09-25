import sys, collections
for fn in sys.argv[1:]:
    L=open(fn).read().splitlines()
    pf=[tuple(map(int,l.split()[1:])) for l in L if l.startswith("pf ")]
    t0=pf[0][0]; pf=[p for p in pf if p[0]-t0>15e6]
    sh=[p for p in pf if p[3]==1]
    rep=collections.Counter()
    for a,b in zip(sh,sh[1:]):
        if b[2]-a[2]>=2: rep[int((b[0]-t0)/1e6)]+=b[2]-a[2]-1
    per=collections.Counter(int((p[0]-t0)/1e6) for p in pf)
    print(f"{fn.split('-')[-1][:-4]:4s} repeats {sum(rep.values()):3d} min-fps {min(per[s] for s in range(16,58))} rep:", "".join(str(min(rep[s],9)) if rep[s] else "." for s in range(15,60)))
