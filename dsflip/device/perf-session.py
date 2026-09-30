#!/usr/bin/env python3
"""perf-session.py — the on-device copy of tools/rgds-monitor.py's session log.

sample <dir>   once a second while a game runs, into <dir>/samples.jsonl (session.sh)
finish <dir>   summary line, keep the log, and if the player allowed it upload it (restore.sh, once)
ask            first time the menu appears: A uploads, B does not
selftest       check the summary, the redaction and the upload request

A session file is the monitor's JSON lines (game, fps, drops, clocks, load, temperature, battery, the new
dsflip.log lines) and, at the end, the same summary object. Quit is a hard exit here, so the session closes in
finish instead of after the monitor's 6 s gap (that gap is only for an SSH connection dropping).

Upload publishes the file on the device-logs branch of JorreFog/ROCKNIXDS (docs/data/device/<id>/). The beta
branch the updater tracks is left alone. The handheld posts the log to a queue (.github/ingest-perf.py on main
commits it); a token in /storage/.config/rocknixds/upload.token is optional and commits directly. Nothing is
sampled or uploaded until nds.share_performance_logs is 1.
"""
import json, os, re, shutil, signal, sys, time, urllib.error, urllib.request

REPO = "JorreFog/ROCKNIXDS"
BRANCH = "device-logs"
# Keep in step with .github/ingest-perf.py on main. The topic is public: it can only receive logs, not push code.
QUEUE_TOPIC = "rocknixds-perf-c4a91e7b2d08f653"
QUEUE = os.environ.get("ROCKNIXDS_PERF_QUEUE", "https://ntfy.sh/" + QUEUE_TOPIC)
LOGNAME = re.compile(r"^[0-9]{8}-[0-9]{6}_[A-Za-z0-9._-]{1,80}\.jsonl$")
API = os.environ.get("ROCKNIXDS_PERF_API", "https://api.github.com/repos/" + REPO)
CFG = os.environ.get("ROCKNIXDS_SYSCFG", "/storage/.config/system/configs/system.cfg")
STATE = os.environ.get("ROCKNIXDS_STATE", "/storage/.config/rocknixds")
LOG = "/storage/.config/drastic/dsflip/dsflip.log"
GPU = "/sys/class/devfreq/fde60000.gpu"
CPU = "/sys/devices/system/cpu/cpufreq/policy0"
BAT = "/sys/class/power_supply/battery"
# padkey.py: A confirms in ES, B does not. The message box only has OK, so B is read from the pad itself.
BTN_A, BTN_B = 304, 305
SHARE_KEY = "nds.share_performance_logs"
_SECRET = re.compile(
    r"(?i)(token|password|passwd|secret|authorization|api[_-]?key)([\"'\s:=]+)[^\s\",]+"
)
_GHTOK = re.compile(r"ghp_[A-Za-z0-9]+|github_pat_[A-Za-z0-9_]+|gho_[A-Za-z0-9]+")
# dsflip logs the RetroAchievements name on login ("logged in as <name>"). The local monitor keeps it; a public
# commit should not.
_RAUSER = re.compile(r"(\[ra\] logged in (?:with token )?as ).+")


def note(msg):
    try:
        os.makedirs(STATE, exist_ok=True)
        path = os.path.join(STATE, "perf.log")
        with open(path, "a") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S") + " " + msg + "\n")
        if os.path.getsize(path) > 200000:
            tail = open(path, "rb").read()[-100000:]
            open(path, "wb").write(tail[tail.find(b"\n") + 1:])
    except OSError:
        pass


def rd(p, d=""):
    try:
        with open(p) as f:
            return f.read().strip()
    except OSError:
        return d


def redact(line):
    line = _GHTOK.sub("[redacted]", line)
    line = _RAUSER.sub(r"\1[redacted]", line)  # before the token= rule, which would eat "token as <name>"
    return _SECRET.sub(r"\1\2[redacted]", line)


def parse_log(lines, st):
    """libdsflip's per-second and periodic lines -> st (the latest values). Same rules as rgds-monitor.py."""
    for l in lines:
        if l.startswith("[dsflip] present/s="):
            kv = dict(p.split("=", 1) for p in l.split() if "=" in p)
            st["fps"] = float(kv.get("[dsflip] present/s", kv.get("present/s", "0")) or 0)
            st["dropped"] = int(kv.get("dropped", 0))
            st["commits"] = int(kv.get("commits", 0))
            parts = l.split()
            if "max-iv" in parts:
                i = parts.index("max-iv")
                st["maxiv"] = [int(parts[i + 1].split("=")[1]) / 1000, int(parts[i + 2].split("=")[1]) / 1000]
        elif l.startswith("[pace]"):
            for p in l.split():
                if p.startswith("late="):
                    st["late"] = int(p[5:])
                if p.startswith("missed="):
                    st["missed"] = int(p[7:])
        elif l.startswith("[cpugov] ") and "->" in l:
            st["cpugov"] = l[9:]
        elif l.startswith("[audio] pump") and "underruns" in l:
            st["underruns"] = int(l.split("underruns ")[1].split(",")[0])
        elif l.startswith("[dsflip] libdsflip"):
            st["lib"] = l.split()[-1]


class Session:
    """The monitor's Session, replayed from the samples at quit (finish may run in another process)."""

    def __init__(self, game, t):
        self.game, self.t0, self.n = game, t, 0
        self.fps, self.drops, self.cpu, self.gpu, self.tmax = [], 0, [], [], {}
        self.bat0, self.mah, self.bat1, self.lib = None, 0.0, None, None

    def add(self, s, st):
        self.n += 1
        if st.get("fps_new"):
            self.fps.append(st["fps"])
            self.drops += st.get("dropped", 0)
        self.cpu.append(s["cpu_mhz"])
        if s.get("gpu_avg_mhz"):
            self.gpu.append(s["gpu_avg_mhz"])
        for k, v in s.get("temp", {}).items():
            self.tmax[k] = max(self.tmax.get(k, 0), v)
        b = s.get("bat", {})
        if self.bat0 is None:
            self.bat0 = b.get("pct")
        self.bat1 = b.get("pct")
        self.mah += -min(0, b.get("ma", 0)) / 3600
        if st.get("lib"):
            self.lib = st["lib"]

    def summary(self, t, extra=None):
        f = self.fps
        out = {
            "summary": True, "game": self.game, "start": self.t0, "seconds": round(t - self.t0),
            "fps_avg": round(sum(f) / len(f), 2) if f else None, "fps_min": min(f) if f else None,
            "pct_seconds_below_59_5": round(100 * sum(1 for v in f if v < 59.5) / len(f), 2) if f else None,
            "drops": self.drops, "drops_per_s": round(self.drops / len(f), 3) if f else None,
            "cpu_mhz_avg": round(sum(self.cpu) / len(self.cpu)) if self.cpu else None,
            "gpu_mhz_avg": round(sum(self.gpu) / len(self.gpu)) if self.gpu else None,
            "temp_max": self.tmax, "battery_pct": [self.bat0, self.bat1], "mah_drawn": round(self.mah),
        }
        if self.lib:
            out["lib"] = self.lib
        if extra:
            out.update(extra)
        return out


def replay(samples):
    game = next((s.get("game") for s in samples if s.get("game")), None)
    if not samples:
        return None
    st, sess = {}, Session(game, samples[0]["t"])
    for s in samples:
        if s.get("game"):
            game = s["game"]
        use = s if s.get("game") else dict(s, game=game)
        st["fps_new"] = any(l.startswith("[dsflip] present/s=") for l in s.get("log", []))
        parse_log(s.get("log", []), st)
        sess.add(use, st)
    sess.game = game
    return sess


def cfg_values():
    per, glob = {}, None
    try:
        lines = open(CFG)
    except OSError:
        return per, glob
    for line in lines:
        line = line.strip()
        mark = '"].share_performance_logs='
        if line.startswith('nds["') and mark in line:
            name, val = line[len('nds["'):].split(mark, 1)
            per[name] = val
        elif line.startswith(SHARE_KEY + "="):
            glob = line.split("=", 1)[1]
    return per, glob


def share_value(rom=None):
    per, glob = cfg_values()
    if rom and rom in per and per[rom] != "":
        return per[rom]
    return glob


def allowed(rom=None):
    return share_value(rom) in ("1", "yes")


def write_share(val):
    os.makedirs(os.path.dirname(CFG), exist_ok=True)
    try:
        lines = open(CFG).read().splitlines()
    except OSError:
        lines = []
    out, found = [], False
    for line in lines:
        if line.startswith(SHARE_KEY + "="):
            if not found:
                out.append(SHARE_KEY + "=" + val)
                found = True
        else:
            out.append(line)
    if not found:
        out.append(SHARE_KEY + "=" + val)
    tmp = CFG + ".rocknixds-tmp"
    with open(tmp, "w") as f:
        f.write("\n".join(out) + "\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, CFG)


def device_id():
    path = os.path.join(STATE, "device-id")
    try:
        cur = open(path).read().strip()
        if re.fullmatch(r"[0-9a-f]{8,32}", cur):
            return cur
    except OSError:
        pass
    import uuid
    cur = uuid.uuid4().hex[:16]
    try:
        os.makedirs(STATE, exist_ok=True)
        with open(path, "w") as f:
            f.write(cur + "\n")
    except OSError:
        pass
    return cur


def version():
    return rd("/storage/.config/rocknixds-version", "unknown")


def safe_name(rom):
    name = "".join(c if c.isalnum() or c in "-_." else "_" for c in (rom or "game"))[:60]
    return name or "game"


# ---- sampling (the monitor's collector, writing a file instead of stdout) ----------------------------

class Tail:
    def __init__(self, p):
        self.p, self.f, self.ino, self.first = p, None, None, True

    def lines(self):
        try:
            st = os.stat(self.p)
        except OSError:
            self.f = None
            return []
        if self.f is None or st.st_ino != self.ino or st.st_size < self.f.tell():
            self.f = open(self.p, errors="replace")
            self.ino = st.st_ino
            if self.first:
                self.f.seek(0, 2)  # at start: only what comes next
        self.first = False
        return [redact(l.rstrip("\n")) for l in self.f.readlines()][:400]


def cpustat():
    out = []
    for l in open("/proc/stat"):
        if l.startswith("cpu") and l[3] != " ":
            v = [int(x) for x in l.split()[1:8]]
            out.append((sum(v), v[3] + v[4]))
    return out


def gputime():
    t = {}
    for line in rd(GPU + "/trans_stat").splitlines():
        p = line.replace("*", " ").split()
        if p and p[0].endswith(":") and p[0][:-1].isdigit():
            t[int(p[0][:-1])] = int(p[-1])
    return t


def find_game_pid():
    for d in os.listdir("/proc"):
        if d.isdigit() and rd("/proc/%s/comm" % d) in ("drastic", "drastic.real", "retroarch"):
            return int(d)
    return None


def running_game(pid):
    if pid and rd("/proc/%d/comm" % pid).startswith("drastic"):
        try:
            args = [a for a in open("/proc/%d/cmdline" % pid, "rb").read().split(b"\0") if a]
            env = dict(e.split(b"=", 1) for e in open("/proc/%d/environ" % pid, "rb").read().split(b"\0") if b"=" in e)
        except OSError:
            return None
        shader = (env.get(b"DSFLIP_SHADER") or env.get(b"DSHOOK_SHADER") or b"").decode(errors="replace")
        rom = os.path.basename(args[-1].decode(errors="replace")) if len(args) > 1 else ""
        return {"system": "nds", "rom": rom, "shader": shader}
    try:
        r = urllib.request.urlopen("http://localhost:1234/runningGame", timeout=0.4)
        if r.status == 200:
            j = json.loads(r.read() or b"{}")
            return {"system": j.get("systemName", ""), "rom": os.path.basename(j.get("path", "")), "name": j.get("name", "")}
    except Exception:
        pass
    return None


def one_sample(tail, pc, gt, pid, pid_t, pcpu, hz):
    now = time.time()
    c, g = cpustat(), gputime()
    load = [round(100 * (1 - (i1 - i0) / max(1, t1 - t0_))) for (t0_, i0), (t1, i1) in zip(pc, c)]
    dg = {f: g.get(f, 0) - gt.get(f, 0) for f in g}
    tot = sum(dg.values())
    gavg = round(sum(f * v for f, v in dg.items()) / tot / 1e6) if tot > 0 else None
    if time.monotonic() - pid_t > 2 or (pid and not os.path.exists("/proc/%d" % pid)):
        pid, pid_t = find_game_pid(), time.monotonic()
    proc = None
    if pid:
        s = rd("/proc/%d/stat" % pid).rsplit(")", 1)[-1].split()
        if len(s) > 13:
            ticks = int(s[11]) + int(s[12])
            if pcpu and pcpu[0] == pid:
                proc = round(100 * (ticks - pcpu[1]) / hz / max(0.5, now - pcpu[2]))
            pcpu = (pid, ticks, now)
    tz = {}
    for z in range(4):
        ty = rd("/sys/class/thermal/thermal_zone%d/type" % z)
        if ty:
            tz[ty.replace("-thermal", "")] = int(rd("/sys/class/thermal/thermal_zone%d/temp" % z, "0")) / 1000
    bat = {
        "pct": int(rd(BAT + "/capacity", "0") or 0), "status": rd(BAT + "/status"),
        "ma": int(rd(BAT + "/current_avg", "0") or 0) / 1000, "v": int(rd(BAT + "/voltage_avg", "0") or 0) / 1e6,
    }
    out = {
        "t": round(now, 2), "game": running_game(pid), "cpu_mhz": int(rd(CPU + "/scaling_cur_freq", "0")) // 1000,
        "cpu_max_mhz": int(rd(CPU + "/scaling_max_freq", "0")) // 1000, "cpu_load": load,
        "gpu_mhz": int(rd(GPU + "/cur_freq", "0")) // 1000000, "gpu_avg_mhz": gavg, "game_cpu": proc,
        "temp": tz, "bat": bat, "log": tail.lines(),
    }
    return out, c, g, pid, pid_t, pcpu


def cmd_sample(directory):
    os.makedirs(directory, exist_ok=True)
    meta = {
        "profile": os.environ.get("ROCKNIXDS_PROFILE", ""),
        "queue": os.environ.get("DSFLIP_QUEUE", ""),
        "queue_wait": os.environ.get("DSFLIP_QUEUE_WAIT", ""),
        "cpu_max": os.environ.get("DSFLIP_CPU_MAX", ""),
        "rom": os.environ.get("ROCKNIXDS_ROM", ""),
        "t0": time.time(),
    }
    with open(os.path.join(directory, "meta.json"), "w") as f:
        json.dump(meta, f)
    path = os.path.join(directory, "samples.jsonl")

    def stop(signum, frame):
        raise SystemExit

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    hz = os.sysconf("SC_CLK_TCK")
    tail, pc, gt = Tail(LOG), cpustat(), gputime()
    pid, pid_t, pcpu = None, 0, None
    t0 = time.monotonic()
    note("sampling " + (meta["rom"] or "a game"))
    try:
        while True:
            delay = max(0, 1 - (time.monotonic() - t0) % 1)
            time.sleep(delay)
            sample, pc, gt, pid, pid_t, pcpu = one_sample(tail, pc, gt, pid, pid_t, pcpu, hz)
            with open(path, "a") as f:
                f.write(json.dumps(sample) + "\n")
                f.flush()
    except SystemExit:
        pass
    except Exception as e:
        note("sampler stopped: " + type(e).__name__ + ": " + str(e)[:200])
        raise


# ---- close, keep, upload ------------------------------------------------------------------------------

def load_meta(directory):
    try:
        return json.load(open(os.path.join(directory, "meta.json")))
    except (OSError, ValueError):
        return {}


def load_samples(directory):
    out = []
    try:
        lines = open(os.path.join(directory, "samples.jsonl"))
    except OSError:
        return out
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            out.append(json.loads(line))
        except ValueError:
            continue
    return out


def num(v):
    if v is None or v == "":
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        return v


def build_session(directory):
    samples = load_samples(directory)
    if not samples:
        return None, None
    for s in samples:
        if s.get("log"):
            s["log"] = [redact(l) for l in s["log"]]
    meta = load_meta(directory)
    sess = replay(samples)
    if sess.game is None and meta.get("rom"):
        sess.game = {"system": "nds", "rom": meta["rom"]}
    rom = (sess.game or {}).get("rom") or meta.get("rom") or "game"
    extra = {
        "device": device_id(),
        "rocknixds": version(),
        "profile": meta.get("profile") or None,
        "queue": num(meta.get("queue")),
        "queue_wait_ms": num(meta.get("queue_wait")),
        "cpu_max_khz": num(meta.get("cpu_max")),
    }
    summary = sess.summary(samples[-1]["t"], extra)
    name = time.strftime("%Y%m%d-%H%M%S", time.localtime(samples[0]["t"])) + "_" + safe_name(rom) + ".jsonl"
    body = "".join(json.dumps(s) + "\n" for s in samples) + json.dumps(summary) + "\n"
    return name, body


def token():
    path = os.path.join(STATE, "upload.token")
    try:
        t = open(path).read().strip()
    except OSError:
        return ""
    return t


def gh(method, path, body, tok, timeout=30):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(API + path, data=data, method=method)
    req.add_header("Authorization", "Bearer " + tok)
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("X-GitHub-Api-Version", "2022-11-28")
    req.add_header("User-Agent", "ROCKNIXDS")
    if data is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read()
            return r.status, json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", "replace")
        if tok:
            raw = raw.replace(tok, "[redacted]")
        err = urllib.error.HTTPError(e.url, e.code, raw[:300], e.headers, None)
        err.code = e.code
        err.body = raw[:300]
        raise err


def ref_sha(name, tok):
    try:
        status, j = gh("GET", "/git/ref/heads/" + name, None, tok)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise
    return j.get("object", {}).get("sha")


def ensure_branch(tok):
    sha = ref_sha(BRANCH, tok)
    if sha:
        return sha
    base = ref_sha("beta", tok) or ref_sha("main", tok)
    if not base:
        raise RuntimeError("no beta or main branch to base device-logs on")
    try:
        gh("POST", "/git/refs", {"ref": "refs/heads/" + BRANCH, "sha": base}, tok)
    except urllib.error.HTTPError as e:
        if e.code not in (409, 422):
            raise
    sha = ref_sha(BRANCH, tok)
    if not sha:
        raise RuntimeError("device-logs branch was not created")
    return sha


def upload_text(relpath, text, message, tok):
    """One commit on device-logs. Retries once if another device committed in between."""
    last = None
    for _ in range(2):
        parent = ensure_branch(tok)
        _, commit = gh("GET", "/git/commits/" + parent, None, tok)
        tree = commit["tree"]["sha"]
        _, blob = gh("POST", "/git/blobs", {"content": text, "encoding": "utf-8"}, tok)
        _, newtree = gh("POST", "/git/trees", {
            "base_tree": tree,
            "tree": [{"path": relpath, "mode": "100644", "type": "blob", "sha": blob["sha"]}],
        }, tok)
        _, made = gh("POST", "/git/commits", {"message": message, "tree": newtree["sha"], "parents": [parent]}, tok)
        try:
            gh("PATCH", "/git/refs/heads/" + BRANCH, {"sha": made["sha"]}, tok)
            return made["sha"]
        except urllib.error.HTTPError as e:
            last = e
            if e.code not in (409, 422):
                raise
    raise last


def commit_message(summary):
    g = summary.get("game") or {}
    name = g.get("rom") or g.get("name") or "game"
    fps = summary.get("fps_avg")
    return "perf: %s, %ss, %s fps avg" % (name, summary.get("seconds"), fps if fps is not None else "-")


def es_post(path, text, timeout=5):
    req = urllib.request.Request("http://localhost:1234" + path, data=text.encode(), method="POST")
    urllib.request.urlopen(req, timeout=timeout).read()


def notify_once(flag, msg):
    path = os.path.join(STATE, flag)
    if os.path.exists(path):
        return
    for _ in range(20):
        try:
            es_post("/notify", msg)
            open(path, "w").write("1\n")
            return
        except Exception:
            time.sleep(1)


def github_has(dev, name):
    """True when device-logs already has this session. Unknown (rate limit, offline) is False."""
    if os.environ.get("ROCKNIXDS_PERF_SKIP_GITHUB_CHECK"):
        return False
    if not re.fullmatch(r"[0-9a-f]{8,32}", dev or "") or not LOGNAME.fullmatch(name or ""):
        return False
    url = "https://api.github.com/repos/%s/contents/docs/data/device/%s/%s?ref=%s" % (REPO, dev, name, BRANCH)
    req = urllib.request.Request(url, method="GET")
    req.add_header("User-Agent", "ROCKNIXDS")
    req.add_header("Accept", "application/vnd.github+json")
    try:
        urllib.request.urlopen(req, timeout=15).read()
        return True
    except Exception:
        return False


def recently_queued(path, window=900):
    try:
        return time.time() - float(open(path + ".queued").read().strip()) < window
    except (OSError, ValueError):
        return False


def queue_log(dev, name, text):
    """Hand the session to the queue the repository imports. No write token on the device."""
    data = text.encode()
    if len(data) > 8000000:
        raise ValueError("log is too large")
    req = urllib.request.Request(QUEUE, data=data, method="PUT")
    req.add_header("Filename", name)
    req.add_header("Title", dev)
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("User-Agent", "ROCKNIXDS")
    with urllib.request.urlopen(req, timeout=60) as r:
        r.read()


def logs_dir():
    d = os.path.join(STATE, "logs")
    os.makedirs(d, exist_ok=True)
    return d


def prune(d):
    names = sorted(n for n in os.listdir(d) if n.endswith(".jsonl"))
    for n in names[:-40]:
        for suf in ("", ".uploaded", ".upload-error", ".queued"):
            try:
                os.remove(os.path.join(d, n + suf))
            except OSError:
                pass


def mark_uploaded(path, text):
    try:
        open(path + ".uploaded", "w").write(text + "\n")
        os.remove(path + ".upload-error")
    except OSError:
        pass


def try_upload(path, text, summary):
    """True when the session is on GitHub or accepted by the queue. The local copy always stays."""
    if os.path.exists("/tmp/rocknixds-testing"):
        note("not uploaded (test launch): " + os.path.basename(path))
        return False
    dev = summary.get("device") or device_id()
    name = os.path.basename(path)
    if github_has(dev, name):
        mark_uploaded(path, "github")
        note("already on device-logs: " + name)
        return True
    tok = token()
    if tok:
        rel = "docs/data/device/%s/%s" % (dev, name)
        try:
            sha = upload_text(rel, text, commit_message(summary), tok)
            mark_uploaded(path, sha)
            note("uploaded %s %s" % (rel, sha[:12]))
            return True
        except Exception as e:
            body = getattr(e, "body", str(e))[:200].replace(tok, "[redacted]")
            note("direct upload failed %s: %s" % (getattr(e, "code", ""), body))
    if recently_queued(path):
        return True
    if not LOGNAME.fullmatch(name) or not re.fullmatch(r"[0-9a-f]{8,32}", dev or ""):
        note("not queued (bad name): " + name)
        return False
    try:
        queue_log(dev, name, text)
    except Exception as e:
        note("queue failed: " + str(e)[:200])
        notify_once("upload-failed-noted", "Performance log stayed on the device. The upload did not go through.")
        return False
    try:
        open(path + ".queued", "w").write(str(time.time()))
    except OSError:
        pass
    note("queued " + name)
    return True


def cmd_finish(directory):
    name, body = build_session(directory)
    if not name:
        shutil.rmtree(directory, ignore_errors=True)
        return
    summary = json.loads(body.strip().splitlines()[-1])
    dest = os.path.join(logs_dir(), name)
    with open(dest, "w") as f:
        f.write(body)
    shutil.rmtree(directory, ignore_errors=True)
    prune(os.path.dirname(dest))
    rom = (summary.get("game") or {}).get("rom")
    if not allowed(rom):
        note("kept, not uploaded (sharing off): " + name)
        return
    # this quit, then a few that failed earlier
    pending = [dest]
    d = os.path.dirname(dest)
    for n in sorted(os.listdir(d)):
        p = os.path.join(d, n)
        if n.endswith(".jsonl") and p not in pending and not os.path.exists(p + ".uploaded"):
            pending.append(p)
    for p in pending[:3]:
        try:
            text = open(p).read()
            summ = json.loads(text.strip().splitlines()[-1])
        except (OSError, ValueError, IndexError):
            continue
        if not summ.get("summary"):
            continue
        if not try_upload(p, text, summ):
            break


# ---- first launch -------------------------------------------------------------------------------------

QUESTION = (
    "May ROCKNIXDS upload a performance log to its GitHub repository when you quit a game?\n\n"
    "The log matches the test monitor: frames per second, dropped frames, CPU and GPU clocks, "
    "temperature, battery and the game file name. It is used to improve the build and to grow a "
    "database of performance tests.\n\n"
    "A   Yes\n"
    "B   No"
)
DECLINED = (
    "Nothing will be uploaded.\n\n"
    "To allow it later: Game settings, Per system advanced configuration, Nintendo DS, "
    "Share performance logs."
)
NO_PAD = (
    "ROCKNIXDS can upload a performance log to its GitHub repository when you quit a game "
    "(frames per second, dropped frames, clocks, temperature, battery, the game file name).\n\n"
    "The gamepad could not be read, so choose Yes or No under Game settings, Nintendo DS, "
    "Share performance logs. Nothing is uploaded until you choose Yes."
)


def es_reachable():
    try:
        urllib.request.urlopen("http://localhost:1234/isIdle", timeout=1).read()
        return True
    except Exception:
        return False


def es_idle():
    try:
        body = urllib.request.urlopen("http://localhost:1234/isIdle", timeout=1).read()
        return b"true" in body
    except Exception:
        return False


def wait_ready():
    for _ in range(120):
        if es_idle() and not os.path.exists("/tmp/dsflip-notice"):
            time.sleep(1)
            return es_reachable()
        time.sleep(0.5)
    return False


def open_pad():
    import glob
    for dev in sorted(glob.glob("/sys/class/input/event*")):
        try:
            name = open(dev + "/device/name").read().strip()
        except OSError:
            continue
        if name != "retrogame_joypad":
            continue
        path = "/dev/input/" + os.path.basename(dev)
        try:
            return os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            return None
    return None


def drain(fd):
    import select
    while True:
        r, _, _ = select.select([fd], [], [], 0)
        if not r:
            return
        try:
            if not os.read(fd, 4096):
                return
        except OSError:
            return


def read_choice(fd):
    """A (the box's OK) is yes. B is no; the box stays up until a second message is closed."""
    import select, struct
    fmt = "llHHi"
    size = struct.calcsize(fmt)
    missed = 0
    while True:
        r, _, _ = select.select([fd], [], [], 0.5)
        if not es_reachable():
            missed += 1
            if missed >= 6:
                return None
            continue
        missed = 0
        if not r:
            continue
        try:
            buf = os.read(fd, size * 16)
        except OSError:
            continue
        while len(buf) >= size:
            _, _, typ, code, val = struct.unpack(fmt, buf[:size])
            buf = buf[size:]
            if typ != 1 or val != 1:
                continue
            if code == BTN_A:
                return "yes"
            if code == BTN_B:
                return "no"


def take_lock():
    path = "/tmp/rocknixds-share-ask.pid"
    if os.path.exists(path):
        try:
            pid = int(open(path).read().strip())
            os.kill(pid, 0)
            return False
        except (OSError, ValueError):
            pass
    try:
        with open(path, "w") as f:
            f.write(str(os.getpid()))
        return True
    except OSError:
        return False


def drop_lock():
    try:
        os.remove("/tmp/rocknixds-share-ask.pid")
    except OSError:
        pass


def cmd_ask():
    if share_value() not in (None, ""):
        return
    if os.path.exists("/tmp/rocknixds-testing"):
        return
    if not take_lock():
        return
    try:
        if not wait_ready():
            return
        # answered while we waited
        if share_value() not in (None, ""):
            return
        fd = open_pad()
        if fd is None:
            try:
                es_post("/messagebox", NO_PAD)
            except Exception as e:
                note("could not ask: " + str(e)[:160])
            return
        try:
            drain(fd)
            es_post("/messagebox", QUESTION)
            choice = read_choice(fd)
        finally:
            os.close(fd)
        if choice == "yes":
            write_share("1")
            note("share performance logs: yes")
            try:
                es_post("/notify", "Performance logs will be uploaded to the ROCKNIXDS repository when you quit a game.")
            except Exception:
                pass
        elif choice == "no":
            write_share("0")
            note("share performance logs: no")
            try:
                es_post("/messagebox", DECLINED)
            except Exception:
                pass
    finally:
        drop_lock()


# ---- self test ----------------------------------------------------------------------------------------

def selftest():
    samples = []
    for i, fps, dropped in ((0, 60, 0), (1, 58, 2), (2, 60, 0)):
        samples.append({
            "t": 1700000000 + i, "game": {"system": "nds", "rom": "Heart Gold.nds", "shader": "ds-crisp"},
            "cpu_mhz": 816 + i, "cpu_max_mhz": 1416, "cpu_load": [10],
            "gpu_mhz": 200, "gpu_avg_mhz": 400 if i else None, "game_cpu": 30,
            "temp": {"soc": 50 + i}, "bat": {"pct": 80 - i, "status": "Discharging", "ma": -200, "v": 3.8},
            "log": ["[dsflip] present/s=%s dropped=%s commits=60" % (fps, dropped),
                    "[dsflip] libdsflip 0.3.0-beta.2",
                    "password=hunter2 token=github_pat_shouldnotappear"],
        })
    sess = replay(samples)
    s = sess.summary(samples[-1]["t"])
    assert s["game"]["rom"] == "Heart Gold.nds", s
    assert s["seconds"] == 2, s
    assert s["fps_avg"] == 59.33, s
    assert s["fps_min"] == 58, s
    assert s["drops"] == 2 and s["drops_per_s"] == 0.667, s
    assert s["pct_seconds_below_59_5"] == 33.33, s
    assert s["cpu_mhz_avg"] == 817, s
    assert s["gpu_mhz_avg"] == 400, s
    assert s["temp_max"]["soc"] == 52, s
    assert s["battery_pct"] == [80, 78], s
    assert s["lib"] == "0.3.0-beta.2", s
    assert "token=github_pat_secret" not in redact("ra token=github_pat_secret ok")
    assert "[redacted]" in redact("password=hunter2")
    assert "[redacted]" in redact("saw ghp_abcdefghijklmnopqrstuvwxyz0123456789")
    assert redact("[ra] logged in as playername") == "[ra] logged in as [redacted]"
    assert "playername" not in redact("[ra] logged in with token as playername")
    assert safe_name("Heart Gold.nds") == "Heart_Gold.nds"

    import http.server, shutil, tempfile, threading
    box = {"blobs": [], "patched": None}

    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _json(self):
            n = int(self.headers.get("Content-Length", 0))
            return json.loads(self.rfile.read(n) or b"{}")

        def _send(self, code, obj):
            raw = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            assert self.headers.get("Authorization", "").startswith("Bearer ")
            if self.path == "/git/ref/heads/device-logs":
                return self._send(200, {"object": {"sha": "parentsha"}})
            if self.path == "/git/commits/parentsha":
                return self._send(200, {"tree": {"sha": "treesha"}})
            self._send(404, {})

        def do_POST(self):
            body = self._json()
            if self.path == "/git/blobs":
                assert body["content"].rstrip().endswith('"summary": true') or '"summary": true' in body["content"]
                box["blobs"].append(body["content"])
                return self._send(201, {"sha": "blobsha"})
            if self.path == "/git/trees":
                assert body["base_tree"] == "treesha"
                assert body["tree"][0]["path"].startswith("docs/data/device/")
                assert body["tree"][0]["sha"] == "blobsha"
                return self._send(201, {"sha": "newtree"})
            if self.path == "/git/commits":
                assert body["parents"] == ["parentsha"] and body["message"].startswith("perf:")
                return self._send(201, {"sha": "newcommit"})
            self._send(404, {})

        def do_PATCH(self):
            body = self._json()
            assert self.path == "/git/refs/heads/device-logs"
            box["patched"] = body["sha"]
            self._send(200, {})

        def do_PUT(self):
            n = int(self.headers.get("Content-Length", 0))
            raw = self.rfile.read(n)
            box["put"] = {"title": self.headers.get("Title"), "filename": self.headers.get("Filename"), "body": raw.decode()}
            self._send(200, {"id": "queued"})

    httpd = http.server.HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    global API, CFG, STATE, QUEUE
    API = "http://127.0.0.1:%d" % httpd.server_address[1]
    os.environ["ROCKNIXDS_PERF_SKIP_GITHUB_CHECK"] = "1"
    tmp = tempfile.mkdtemp()
    try:
        CFG = os.path.join(tmp, "system.cfg")
        STATE = os.path.join(tmp, "state")
        os.makedirs(STATE)
        write_share("1")
        assert allowed("Heart Gold.nds")
        write_share("0")
        assert not allowed("Heart Gold.nds")
        open(CFG, "a").write('nds["Other.nds"].share_performance_logs=1\n')
        assert allowed("Other.nds") and not allowed("Heart Gold.nds")
        write_share("1")
        open(os.path.join(STATE, "upload.token"), "w").write("github_pat_testtoken\n")
        open(os.path.join(STATE, "device-id"), "w").write("abc123def456\n")
        d = os.path.join(tmp, "active")
        os.makedirs(d)
        with open(os.path.join(d, "samples.jsonl"), "w") as f:
            for sample in samples:
                f.write(json.dumps(sample) + "\n")
        json.dump({"profile": "balanced", "queue": "2", "queue_wait": "20", "cpu_max": "1416000", "rom": "Heart Gold.nds"},
                  open(os.path.join(d, "meta.json"), "w"))
        # finish uploads because sharing is on and the fake GitHub answers
        os.environ.pop("ROCKNIXDS_TESTING", None)
        cmd_finish(d)
        assert box["patched"] == "newcommit", box
        assert "Heart_Gold.nds" in box["blobs"][0] or "Heart Gold.nds" in box["blobs"][0]
        saved = os.listdir(os.path.join(STATE, "logs"))
        assert any(n.endswith(".jsonl") for n in saved), saved
        assert any(n.endswith(".uploaded") for n in saved), saved
        text = box["blobs"][0]
        assert "hunter2" not in text and "shouldnotappear" not in text and "github_pat_" not in text
        summ = json.loads(text.strip().splitlines()[-1])
        assert summ["profile"] == "balanced" and summ["queue"] == 2 and summ["device"] == "abc123def456"
        assert not os.path.isdir(d)
        os.remove(os.path.join(STATE, "upload.token"))
        QUEUE = API
        queued = os.path.join(STATE, "logs", "20231114-221320_Heart_Gold.nds.jsonl")
        open(queued, "w").write(text)
        assert try_upload(queued, text, summ)
        assert box["put"]["title"] == "abc123def456"
        assert box["put"]["filename"] == "20231114-221320_Heart_Gold.nds.jsonl"
        assert "hunter2" not in box["put"]["body"] and '"summary": true' in box["put"]["body"]
    finally:
        httpd.shutdown()
        shutil.rmtree(tmp)
    print("perf-session selftest ok")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "sample" and len(sys.argv) == 3:
        cmd_sample(sys.argv[2])
    elif cmd == "finish" and len(sys.argv) == 3:
        cmd_finish(sys.argv[2])
    elif cmd == "ask":
        cmd_ask()
    elif cmd == "selftest":
        selftest()
    else:
        sys.exit("usage: perf-session.py sample DIR | finish DIR | ask | selftest")


if __name__ == "__main__":
    main()
