#!/usr/bin/env python3
"""powerprobe.py <seconds> [tag]   (run ON the device; tools/powerprobe.sh pushes and runs it)

Measures one window of whatever the device is doing, for comparing settings and builds (plan-1.4 sections 6 and 7):
  cpu      busy % (all 4 cores = 400), average clock and time at each clock (cpufreq stats)
  gpu      average clock and time at each clock (devfreq trans_stat); busy % isn't exposed by mali_kbase
  temp     SoC (cpu-thermal) at the start, the end and the highest seen
  battery  mean current (uA, negative = draining) and voltage, sampled every second. On the charger this is the
           charger's input minus the system's draw; with the charger saturated (it is on a weak USB supply) a
           change in draw shows up 1:1 as a change here, so compare runs on the same charger, or unplugged.
  threads  the busiest threads (CPU % of one core), with their process
Prints a summary and, with a tag, writes /storage/dsflip/probe/<tag>.json.
"""
import json, os, sys, time

CPUF = "/sys/devices/system/cpu/cpufreq/policy0"
GPU = "/sys/class/devfreq/fde60000.gpu"
TZ = "/sys/class/thermal/thermal_zone0/temp"
BAT = "/sys/class/power_supply/battery"
HZ = os.sysconf("SC_CLK_TCK")


def rd(p, d=""):
    try:
        with open(p) as f:
            return f.read().strip()
    except OSError:
        return d


def cpu_stat():
    f = rd("/proc/stat").split("\n")[0].split()[1:]
    v = list(map(int, f))
    idle = v[3] + v[4]
    return sum(v[:8]), idle, v[:8]


CATS = ("user", "nice", "system", "idle", "iowait", "irq", "softirq", "steal")


def cpu_freqs():
    out = {}
    for line in rd(CPUF + "/stats/time_in_state").split("\n"):
        if line.strip():
            k, t = line.split()
            out[int(k)] = int(t) * 10          # 10 ms units -> ms
    return out


def gpu_freqs():
    out = {}
    for line in rd(GPU + "/trans_stat").split("\n"):
        line = line.replace("*", " ").strip()
        if line and line[0].isdigit() and ":" in line:
            k, rest = line.split(":", 1)
            out[int(k)] = int(rest.split()[-1])  # ms
    return out


def threads():
    out = {}
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        comm = rd(f"/proc/{pid}/comm", "?")
        try:
            tids = os.listdir(f"/proc/{pid}/task")
        except OSError:
            continue
        for tid in tids:
            s = rd(f"/proc/{pid}/task/{tid}/stat")
            if not s:
                continue
            r = s[s.rfind(")") + 2:].split()
            name = s[s.find("(") + 1:s.rfind(")")]
            out[(int(pid), int(tid))] = (comm, name, int(r[11]) + int(r[12]))
    return out


def dist(a, b):
    d = {k: b.get(k, 0) - a.get(k, 0) for k in b}
    tot = sum(d.values()) or 1
    avg = sum(k * v for k, v in d.items()) / tot
    return avg, {k: round(100.0 * v / tot, 1) for k, v in sorted(d.items()) if v > 0}


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
    tag = sys.argv[2] if len(sys.argv) > 2 else ""
    c0, g0, (t0, i0, v0), th0 = cpu_freqs(), gpu_freqs(), cpu_stat(), threads()
    temp0 = int(rd(TZ, "0")) / 1000
    tmax, cur, volt = temp0, [], []
    start = time.monotonic()
    while time.monotonic() - start < secs:
        time.sleep(1)
        tmax = max(tmax, int(rd(TZ, "0")) / 1000)
        cur.append(int(rd(BAT + "/current_avg", "0")))
        volt.append(int(rd(BAT + "/voltage_avg", "0")))
    wall = time.monotonic() - start
    c1, g1, (t1, i1, v1), th1 = cpu_freqs(), gpu_freqs(), cpu_stat(), threads()
    cats = {c: round(400.0 * (b - a) / max(1, t1 - t0), 1) for c, a, b in zip(CATS, v0, v1) if c not in ("idle", "iowait")}
    temp1 = int(rd(TZ, "0")) / 1000
    busy = 400.0 * (1 - (i1 - i0) / max(1, t1 - t0))
    cavg, cdist = dist(c0, c1)
    gavg, gdist = dist(g0, g1)
    tops = []
    for k, (comm, name, t) in th1.items():
        if k in th0:
            pct = 100.0 * (t - th0[k][2]) / HZ / wall
            if pct >= 0.5:
                tops.append((round(pct, 1), comm, name, k[1]))
    tops.sort(reverse=True)
    res = {"tag": tag, "secs": round(wall, 1), "cpu_busy_pct": round(busy, 1), "cpu_avg_mhz": round(cavg / 1000),
           "cpu_time_at_khz_pct": cdist, "cpu_governor": rd(CPUF + "/scaling_governor"),
           "gpu_avg_mhz": round(gavg / 1e6), "gpu_time_at_hz_pct": gdist, "gpu_governor": rd(GPU + "/governor"),
           "temp_c": [temp0, temp1, tmax],
           "bat_ma": round(sum(cur) / max(1, len(cur)) / 1000, 1), "bat_v": round(sum(volt) / max(1, len(volt)) / 1e6, 3),
           "charger": rd("/sys/class/power_supply/charger/online"), "cpu_by_kind_pct": cats, "threads": tops[:12]}
    print(f"{tag or 'probe'}: {wall:.0f} s  CPU {busy:.0f}% of 400 at avg {cavg/1000:.0f} MHz ({res['cpu_governor']})  "
          f"GPU avg {gavg/1e6:.0f} MHz ({res['gpu_governor']})  SoC {temp0:.1f}->{temp1:.1f} C (max {tmax:.1f})  "
          f"battery {res['bat_ma']:+.0f} mA at {res['bat_v']:.3f} V (charger {'on' if res['charger'] == '1' else 'off'})")
    print("  CPU by kind (% of 400): " + "  ".join(f"{k} {v}" for k, v in cats.items() if v))
    for pct, comm, name, tid in tops[:12]:
        print(f"  {pct:5.1f}%  {comm}/{name} ({tid})")
    if tag:
        os.makedirs("/storage/dsflip/probe", exist_ok=True)
        with open(f"/storage/dsflip/probe/{tag}.json", "w") as f:
            json.dump(res, f, indent=1)


if __name__ == "__main__":
    main()
