#!/usr/bin/env python3
"""rgds-monitor: live stats from an RG DS running ROCKNIXDS, logged per game session (run on a PC).

usage: rgds-monitor.py [host] [--no-ui] [--logdir DIR] [--port N | --no-web]
       rgds-monitor.py report FILE.jsonl...

Connects with ssh and keeps reconnecting (the device sleeping, rebooting or leaving Wi-Fi is fine). The host is
remembered in ~/.config/rgds-monitor/host; RGDS_SSH="<ssh command>" replaces `ssh root@<host>` (e.g. a wrapper that
supplies a password). Once a second: the game, fps and dropped frames, frame pacing, CPU and GPU clocks and load,
DraStic's own CPU use, temperatures and battery. Every sample is appended to <logdir>/<date>.jsonl; each game
session gets its own <logdir>/<start>_<game>.jsonl (its samples and libdsflip's log lines as they were written) with
a summary line at the end. `report` prints the summary of saved sessions. q quits.
While it runs, http://localhost:8765 shows the same live, with charts of the last 5 minutes, the events and the
logged sessions (rgds-monitor-web.html, next to this file; it only listens on this PC).
The device side reads files only (no processes started), so the measuring doesn't change what it measures.
"""
import collections, curses, json, os, queue, shlex, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

COLLECTOR = r'''
import json, os, sys, time, urllib.request
D = "/storage/.config/drastic/dsflip"; LOG = D + "/dsflip.log"
GPU = "/sys/class/devfreq/fde60000.gpu"; CPU = "/sys/devices/system/cpu/cpufreq/policy0"
BAT = "/sys/class/power_supply/battery"; HZ = os.sysconf("SC_CLK_TCK")
def rd(p, d=""):
    try:
        with open(p) as f: return f.read().strip()
    except Exception: return d
def cpustat():
    out = []
    for l in open("/proc/stat"):
        if l.startswith("cpu") and l[3] != " ":
            v = [int(x) for x in l.split()[1:8]]; out.append((sum(v), v[3] + v[4]))
    return out
def gputime():
    t = {}
    for line in rd(GPU + "/trans_stat").splitlines():
        p = line.replace("*", " ").split()
        if p and p[0].endswith(":") and p[0][:-1].isdigit(): t[int(p[0][:-1])] = int(p[-1])
    return t
def find_game_pid():
    for d in os.listdir("/proc"):
        if d.isdigit() and rd("/proc/%s/comm" % d) in ("drastic", "drastic.real", "retroarch"): return int(d)
    return None
def running_game(pid):
    if pid and rd("/proc/%d/comm" % pid).startswith("drastic"):     # the process itself: its ROM and shader
        try:
            args = [a for a in open("/proc/%d/cmdline" % pid, "rb").read().split(b"\0") if a]
            env = dict(e.split(b"=", 1) for e in open("/proc/%d/environ" % pid, "rb").read().split(b"\0") if b"=" in e)
        except OSError: return None
        shader = (env.get(b"DSFLIP_SHADER") or env.get(b"DSHOOK_SHADER") or b"").decode(errors="replace")
        return {"system": "nds", "rom": os.path.basename(args[-1].decode(errors="replace")) if len(args) > 1 else "", "shader": shader}
    try:
        r = urllib.request.urlopen("http://localhost:1234/runningGame", timeout=0.4)
        if r.status == 200:
            j = json.loads(r.read() or b"{}")
            return {"system": j.get("systemName", ""), "rom": os.path.basename(j.get("path", "")), "name": j.get("name", "")}
    except Exception: pass
    return None
class Tail:
    def __init__(self, p): self.p, self.f, self.ino = p, None, None
    def lines(self):
        try: st = os.stat(self.p)
        except OSError: self.f = None; return []
        if self.f is None or st.st_ino != self.ino or st.st_size < self.f.tell():
            self.f = open(self.p, errors="replace"); self.ino = st.st_ino
            if self.first: self.f.seek(0, 2)           # at start: only what comes next
        self.first = False
        return [l.rstrip("\n") for l in self.f.readlines()]
    first = True
def thread_cpu(pid, prev, now):
    """{thread name: % of a core} for the game's threads above 2% ("main" = the main thread; threads of one name are
    summed: SuperDrastic's 3D render threads are all rast-3d). prev: {tid: (ticks, t)} from the last sample, updated."""
    out, seen = {}, set()
    try: tids = os.listdir("/proc/%d/task" % pid)
    except OSError: return out
    for t in tids:
        st = rd("/proc/%s/task/%s/stat" % (pid, t))
        if not st: continue
        name = st[st.find("(") + 1:st.rfind(")")]; f = st.rsplit(")", 1)[-1].split()
        if len(f) < 13: continue
        ticks, tid = int(f[11]) + int(f[12]), int(t); seen.add(tid)
        if tid in prev:
            pct = 100 * (ticks - prev[tid][0]) / HZ / max(0.5, now - prev[tid][1])
            if pct >= 2:
                key = "main" if tid == pid else name; out[key] = round(out.get(key, 0) + pct)
        prev[tid] = (ticks, now)
    for tid in [k for k in prev if k not in seen]: del prev[tid]
    return out
tail = Tail(LOG); pc, gt = cpustat(), gputime(); pid, pid_t, pcpu = None, 0, None; t0 = time.monotonic(); tprev, tpid = {}, None
while True:
    time.sleep(max(0, 1 - (time.monotonic() - t0) % 1))
    now = time.time(); c, g = cpustat(), gputime()
    load = [round(100 * (1 - (i1 - i0) / max(1, t1 - t0_))) for (t0_, i0), (t1, i1) in zip(pc, c)]
    dg = {f: g.get(f, 0) - gt.get(f, 0) for f in g}; tot = sum(dg.values())
    gavg = round(sum(f * v for f, v in dg.items()) / tot / 1e6) if tot > 0 else None
    pc, gt = c, g
    if time.monotonic() - pid_t > 2 or (pid and not os.path.exists("/proc/%d" % pid)):
        pid, pid_t = find_game_pid(), time.monotonic()
    proc, threads = None, None
    if pid:
        s = rd("/proc/%d/stat" % pid).rsplit(")", 1)[-1].split()
        if len(s) > 13:
            ticks = int(s[11]) + int(s[12])
            if pcpu and pcpu[0] == pid: proc = round(100 * (ticks - pcpu[1]) / HZ / max(0.5, now - pcpu[2]))
            pcpu = (pid, ticks, now)
        if tpid != pid: tprev, tpid = {}, pid
        threads = thread_cpu(pid, tprev, now) or None
    tz = {}
    for z in range(4):
        ty = rd("/sys/class/thermal/thermal_zone%d/type" % z)
        if ty: tz[ty.replace("-thermal", "")] = int(rd("/sys/class/thermal/thermal_zone%d/temp" % z, "0")) / 1000
    bat = {"pct": int(rd(BAT + "/capacity", "0") or 0), "status": rd(BAT + "/status"),
           "ma": int(rd(BAT + "/current_avg", "0") or 0) / 1000, "v": int(rd(BAT + "/voltage_avg", "0") or 0) / 1e6}
    out = {"t": round(now, 2), "game": running_game(pid), "cpu_mhz": int(rd(CPU + "/scaling_cur_freq", "0")) // 1000,
           "cpu_max_mhz": int(rd(CPU + "/scaling_max_freq", "0")) // 1000, "cpu_load": load,
           "gpu_mhz": int(rd(GPU + "/cur_freq", "0")) // 1000000, "gpu_avg_mhz": gavg, "game_cpu": proc,
           "game_threads": threads, "temp": tz, "bat": bat, "log": tail.lines()}
    sys.stdout.write(json.dumps(out) + "\n"); sys.stdout.flush()
'''

CONF = os.path.expanduser("~/.config/rgds-monitor/host")


def parse_log(lines, st):
    """libdsflip's per-second and periodic lines -> st (the latest values)"""
    for l in lines:
        if l.startswith("[dsflip] present/s="):
            kv = dict(p.split("=", 1) for p in l.split() if "=" in p)
            st["fps"] = float(kv.get("[dsflip] present/s", kv.get("present/s", "0")) or 0)
            st["dropped"] = int(kv.get("dropped", 0)); st["commits"] = int(kv.get("commits", 0))
            parts = l.split()
            if "max-iv" in parts:
                i = parts.index("max-iv"); st["maxiv"] = [int(parts[i + 1].split("=")[1]) / 1000, int(parts[i + 2].split("=")[1]) / 1000]
        elif l.startswith("[pace]"):
            for p in l.split():
                if p.startswith("late="): st["late"] = int(p[5:])
                if p.startswith("missed="): st["missed"] = int(p[7:])
        elif l.startswith("[cpugov] ") and "->" in l:
            st["cpugov"] = l[9:]
        elif l.startswith("[audio] pump") and "underruns" in l:
            st["underruns"] = int(l.split("underruns ")[1].split(",")[0])
        elif l.startswith("[dsflip] libdsflip"):
            st["lib"] = l.split()[-1]


class Session:
    def __init__(self, logdir, game, t):
        name = "".join(c if c.isalnum() or c in "-_." else "_" for c in (game.get("rom") or game.get("name") or "game"))[:60]
        self.path = os.path.join(logdir, time.strftime("%Y%m%d-%H%M%S", time.localtime(t)) + "_" + name + ".jsonl")
        self.f = open(self.path, "a"); self.game = game; self.t0 = t; self.n = 0
        self.fps, self.drops, self.cpu, self.gpu, self.tmax, self.bat0, self.mah = [], 0, [], [], {}, None, 0.0
        self.gcpu, self.threads, self.tn = [], {}, 0

    def add(self, s, st):
        self.f.write(json.dumps(s) + "\n"); self.f.flush(); self.n += 1
        if st.get("fps_new"):
            self.fps.append(st["fps"]); self.drops += st.get("dropped", 0)
        self.cpu.append(s["cpu_mhz"])
        if s.get("gpu_avg_mhz"): self.gpu.append(s["gpu_avg_mhz"])
        if s.get("game_cpu") is not None: self.gcpu.append(s["game_cpu"])
        if s.get("game_threads") is not None:            # per-thread % of a core (threads below 2% are left out: 0)
            self.tn += 1
            for k, v in s["game_threads"].items(): self.threads[k] = self.threads.get(k, 0) + v
        for k, v in s.get("temp", {}).items(): self.tmax[k] = max(self.tmax.get(k, 0), v)
        b = s.get("bat", {})
        if self.bat0 is None: self.bat0 = b.get("pct")
        self.bat1 = b.get("pct"); self.mah += -min(0, b.get("ma", 0)) / 3600

    def summary(self, t):
        f = self.fps
        return {"summary": True, "game": self.game, "start": self.t0, "seconds": round(t - self.t0),
                "fps_avg": round(sum(f) / len(f), 2) if f else None, "fps_min": min(f) if f else None,
                "pct_seconds_below_59_5": round(100 * sum(1 for v in f if v < 59.5) / len(f), 2) if f else None,
                "drops": self.drops, "drops_per_s": round(self.drops / len(f), 3) if f else None,
                "cpu_mhz_avg": round(sum(self.cpu) / len(self.cpu)) if self.cpu else None,
                "gpu_mhz_avg": round(sum(self.gpu) / len(self.gpu)) if self.gpu else None,
                "temp_max": self.tmax, "battery_pct": [self.bat0, getattr(self, "bat1", None)], "mah_drawn": round(self.mah),
                "game_cpu_avg": round(sum(self.gcpu) / len(self.gcpu)) if self.gcpu else None,
                "threads_avg": thread_avg(self.threads, self.tn)}

    def close(self, t):
        s = self.summary(t); self.f.write(json.dumps(s) + "\n"); self.f.close(); return s


def thread_avg(threads, n):
    """{thread: average % of a core over n samples}, busiest first, or None without per-thread samples"""
    return {k: round(v / n) for k, v in sorted(threads.items(), key=lambda kv: -kv[1])} if n else None


def fmt_threads(s):
    """'  game 142% of a core: main 93, rast-3d 35, drastic 14' from a summary, or '' when it was not logged"""
    th = s.get("threads_avg")
    g = f"  game {s['game_cpu_avg']}% of a core" if s.get("game_cpu_avg") is not None else ""
    return g + (": " + ", ".join(f"{k} {v}" for k, v in th.items()) if th else "")


def fmt_summary(s):
    g = s["game"]
    return (f"{g.get('rom') or g.get('name')}: {s['seconds'] // 60} min, {s['fps_avg']} fps avg (min {s['fps_min']}), "
            f"{s['drops_per_s']} drops/s, {s['pct_seconds_below_59_5']}% of seconds <59.5 fps, CPU {s['cpu_mhz_avg']} MHz, "
            f"GPU {s['gpu_mhz_avg']} MHz, max {', '.join(f'{k} {v:.0f} C' for k, v in s['temp_max'].items())}, "
            f"battery {s['battery_pct'][0]}% -> {s['battery_pct'][1]}% ({s['mah_drawn']} mAh)" + fmt_threads(s))


def report(files):
    for p in files:
        sess = None
        for line in open(p):
            try: j = json.loads(line)
            except ValueError: continue
            if j.get("summary"): sess = j
        print(p + ":\n  " + (fmt_summary(sess) if sess else "no summary (the session was still running, or the file is a day log)"))


class Web:
    """the live page: GET / (the page), /events (server-sent events: the history, then every sample and event),
    /api/sessions (the logged sessions and their summaries), /logs/<file> (a session log)"""
    def __init__(self, port, logdir):
        self.logdir, self.hist, self.events, self.subs, self.lock = logdir, collections.deque(maxlen=900), collections.deque(maxlen=100), set(), threading.Lock()
        self.conn = "connecting"
        page = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rgds-monitor-web.html")
        self.page = open(page, "rb").read()
        web = self

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a): pass
            def send(self, code, body, ctype):
                self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store"); self.end_headers(); self.wfile.write(body)
            def do_GET(self):
                path = self.path.split("?")[0]
                if path == "/": return self.send(200, web.page, "text/html; charset=utf-8")
                if path == "/api/sessions": return self.send(200, json.dumps(web.sessions()).encode(), "application/json")
                if path.startswith("/logs/"):
                    name = os.path.basename(__import__("urllib.parse").parse.unquote(path[6:]))
                    f = os.path.join(web.logdir, name)
                    if name.endswith(".jsonl") and os.path.isfile(f): return self.send(200, open(f, "rb").read(), "application/x-ndjson")
                    return self.send(404, b"not found", "text/plain")
                if path == "/events":
                    self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.send_header("Cache-Control", "no-store"); self.end_headers()
                    q = queue.Queue(maxsize=600)
                    with web.lock:
                        hello = {"type": "hello", "history": list(web.hist), "events": list(web.events), "conn": web.conn}
                        web.subs.add(q)
                    try:
                        self.wfile.write(b"data: " + json.dumps(hello).encode() + b"\n\n"); self.wfile.flush()
                        while True:
                            try: msg = q.get(timeout=15)
                            except queue.Empty: msg = None
                            self.wfile.write((b"data: " + msg.encode() + b"\n\n") if msg else b": keepalive\n\n"); self.wfile.flush()
                    except (BrokenPipeError, ConnectionResetError, OSError): pass
                    finally:
                        with web.lock: web.subs.discard(q)
                    return
                self.send(404, b"not found", "text/plain")
        self.server = ThreadingHTTPServer(("127.0.0.1", port), H); self.server.daemon_threads = True
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def publish(self, msg, keep=None):
        data = json.dumps(msg)
        with self.lock:
            if keep is not None: keep.append(msg)
            for q in list(self.subs):
                try: q.put_nowait(data)
                except queue.Full: self.subs.discard(q)

    def sessions(self):
        out = []
        for name in sorted(os.listdir(self.logdir), reverse=True)[:200]:
            if not name.endswith(".jsonl") or "_" not in name: continue
            f = os.path.join(self.logdir, name)
            try:
                with open(f, "rb") as fh:
                    first = json.loads(fh.readline() or b"{}")
                    fh.seek(max(0, os.path.getsize(f) - 4096)); last = fh.read().splitlines()[-1]
                summ = json.loads(last) if last else {}
            except (OSError, ValueError, IndexError): continue
            g = first.get("game") or {}
            out.append({"file": name, "start": first.get("t", 0), "game": g.get("name") or g.get("rom") or "?",
                        "summary": summ if summ.get("summary") else None})
            if len(out) >= 50: break
        return out


def spark(vals, lo=40, hi=61):
    bars = " ▁▂▃▄▅▆▇█"
    return "".join(bars[max(0, min(8, int((v - lo) / (hi - lo) * 8)))] for v in vals)


def main():
    args = [a for a in sys.argv[1:]]
    if args and args[0] == "report": return report(args[1:])
    ui = "--no-ui" not in args; args = [a for a in args if a != "--no-ui"]
    port = 8765
    if "--port" in args:
        i = args.index("--port"); port = int(args[i + 1]); del args[i:i + 2]
    if "--no-web" in args: port = 0; args.remove("--no-web")
    logdir = os.path.expanduser("~/rgds-logs")
    if "--logdir" in args:
        i = args.index("--logdir"); logdir = os.path.expanduser(args[i + 1]); del args[i:i + 2]
    os.makedirs(logdir, exist_ok=True)
    host = args[0] if args else (open(CONF).read().strip() if os.path.exists(CONF) else "")
    ssh = shlex.split(os.environ["RGDS_SSH"]) if os.environ.get("RGDS_SSH") else None
    if not host and not ssh: sys.exit("usage: rgds-monitor.py <device ip or hostname> (remembered for next time)")
    if host:
        os.makedirs(os.path.dirname(CONF), exist_ok=True); open(CONF, "w").write(host + "\n")
    cmd = (ssh or ["ssh", "-o", "ConnectTimeout=5", "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=2", "root@" + host]) + ["python3 -u -"]

    state = {"conn": "connecting", "sample": None, "st": {}, "fpshist": [], "session": None, "last": None, "quit": False}
    lock = threading.Lock()
    web = None
    if port:
        try:
            web = Web(port, logdir); url = f"http://localhost:{port}"
        except OSError as e:
            url = f"(web page off: port {port}: {e.strerror})"
    else:
        url = ""
    if not ui and url: print("live page: " + url, flush=True)

    def event(t, text, session=False):
        if web: web.publish({"type": "event", "t": t, "text": text, "session": session}, web.events)

    def reader():
        while not state["quit"]:
            try:
                p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
                p.stdin.write(COLLECTOR); p.stdin.close()
                for line in p.stdout:
                    try: s = json.loads(line)
                    except ValueError: continue
                    handle(s)
                p.wait()
            except OSError:
                pass
            with lock: state["conn"] = "disconnected: retrying"
            if web: web.conn = state["conn"]; web.publish({"type": "conn", "conn": state["conn"]})
            time.sleep(3)

    def handle(s):
        with lock:
            state["conn"] = "connected"
            st = state["st"]; st["fps_new"] = any(l.startswith("[dsflip] present/s=") for l in s.get("log", []))
            gov = st.get("cpugov")
            parse_log(s.get("log", []), st)
            if st.get("cpugov") != gov and st.get("cpugov"): event(s["t"], "CPU governor: " + st["cpugov"])
            if st["fps_new"]: state["fpshist"] = (state["fpshist"] + [st["fps"]])[-60:]
            with open(os.path.join(logdir, time.strftime("%Y-%m-%d") + ".jsonl"), "a") as f: f.write(json.dumps(s) + "\n")
            g, sess = s.get("game"), state["session"]
            # the game is briefly unseen while ES hands over to DraStic (and while DraStic starts): a session ends
            # only after 6 s without it, or when another game appears
            if g: state["seen"] = s["t"]
            gone = not g and s["t"] - state.get("seen", 0) > 6
            if sess and (gone or (g and g.get("rom") != sess.game.get("rom"))):
                state["last"] = fmt_summary(sess.close(s["t"])); state["session"] = sess = None
                if not ui: print("session ended: " + state["last"], flush=True)
                event(s["t"], "Session ended: " + state["last"], True)
            if g and not sess:
                state["session"] = sess = Session(logdir, g, s["t"]); st.clear(); state["fpshist"] = []
                if not ui: print("session started: " + sess.path, flush=True)
                event(s["t"], f"Session started: {g.get('name') or g.get('rom')}" + (f" (shader {g['shader']})" if g.get("shader") else ""), True)
            if sess and (g or gone): sess.add(s, st)
            elif sess and not g: sess.add(dict(s, game=sess.game), st)      # the handover gap still belongs to it
            state["sample"] = s
            if web:
                web.conn = "connected"
                msg = dict(s); msg.pop("log", None)
                msg.update({"type": "sample", "conn": "connected", "late": st.get("late"), "missed": st.get("missed"),
                            "fps": st.get("fps") if st.get("fps_new") else None, "dropped": st.get("dropped") if st.get("fps_new") else None,
                            "maxiv": st.get("maxiv") if st.get("fps_new") else None})
                if sess:
                    n = len(sess.fps)
                    msg["session"] = {"start": sess.t0, "drops": sess.drops, "seconds": n, "fps_avg": sum(sess.fps) / n if n else None,
                                      "below": 100 * sum(1 for v in sess.fps if v < 59.5) / n if n else 0}
                web.publish(msg, web.hist)

    threading.Thread(target=reader, daemon=True).start()
    if not ui:
        try:
            while True: time.sleep(1)
        except KeyboardInterrupt: pass
        finally:
            if state["session"]: print("session ended: " + fmt_summary(state["session"].close(time.time())))
        return

    def draw(scr):
        curses.curs_set(0); scr.nodelay(True)
        while True:
            if scr.getch() in (ord("q"), ord("Q")): break
            with lock:
                s, st, sess = state["sample"], dict(state["st"]), state["session"]
                lines = [f"RG DS monitor  {host or 'RGDS_SSH'}  [{state['conn']}]   logs: {logdir}   {'live page: ' + url + '   ' if url else ''}q quits", ""]
                if s:
                    g = s.get("game")
                    gname = (g.get("rom") or g.get("name")) + f"  ({g.get('system')}{', shader ' + g['shader'] if g.get('shader') else ''})" if g else "(menu)"
                    dur = f"   session {int(s['t'] - sess.t0) // 60}:{int(s['t'] - sess.t0) % 60:02d}" if sess else ""
                    lines.append(f"Game     {gname}{dur}")
                    if g and g.get("system") == "nds" and "fps" in st:
                        sd = f"   session: {sess.drops / max(1, len(sess.fps)):.3f} drops/s, {sum(1 for v in sess.fps if v < 59.5) * 100 / max(1, len(sess.fps)):.1f}% of seconds <59.5" if sess else ""
                        lines.append(f"FPS      {st['fps']:5.1f}   dropped {st.get('dropped', 0)} this second{sd}")
                        mi = st.get("maxiv", [0, 0])
                        lines.append(f"Pacing   longest frame top {mi[0]:.1f} ms  bottom {mi[1]:.1f} ms   late latches {st.get('late', '-')}/10 s   missed {st.get('missed', '-')}   audio underruns {st.get('underruns', '-')}")
                        lines.append(f"fps 60 s {spark(state['fpshist'])}")
                    load = s.get("cpu_load", [])
                    lines.append(f"CPU      {s['cpu_mhz']:4d} MHz (limit {s['cpu_max_mhz']})   load {' '.join(f'{v:3d}%' for v in load)}" +
                                 (f"   game process {s['game_cpu']}% of a core" if s.get("game_cpu") is not None else ""))
                    if s.get("game_threads"):
                        lines.append("Threads  " + "   ".join(f"{k} {v}%" for k, v in sorted(s["game_threads"].items(), key=lambda kv: -kv[1])))
                    lines.append(f"GPU      {s['gpu_mhz']:4d} MHz" + (f"   (avg {s['gpu_avg_mhz']} MHz last second)" if s.get("gpu_avg_mhz") else ""))
                    lines.append("Temps    " + "   ".join(f"{k} {v:.1f} C" for k, v in s.get("temp", {}).items()))
                    b = s.get("bat", {})
                    lines.append(f"Battery  {b.get('pct')}%  {b.get('status')}  {b.get('ma', 0):+.0f} mA  {b.get('v', 0):.2f} V")
                    if st.get("cpugov"): lines.append(f"cpugov   {st['cpugov']}")
                if state["last"]: lines += ["", "last session: " + state["last"]]
            scr.erase()
            h, w = scr.getmaxyx()
            for i, l in enumerate(lines[:h - 1]): scr.addnstr(i, 0, l, w - 1)
            scr.refresh(); time.sleep(0.25)
    try:
        curses.wrapper(draw)
    finally:
        state["quit"] = True
        with lock:
            if state["session"]: print("session ended: " + fmt_summary(state["session"].close(time.time())))


if __name__ == "__main__":
    main()
