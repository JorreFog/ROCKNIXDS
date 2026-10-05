#!/usr/bin/env python3
"""Import performance sessions posted by ROCKNIXDS onto the device-logs branch.

The handheld PUTs the jsonl to ntfy.sh (see dsflip/device/perf-session.py, QUEUE_TOPIC). This reads that
queue and writes docs/data/device/<device-id>/<session>.jsonl, and next to it <session>.jsonl.prof.txt when the
handheld ran SuperDrastic's sampling profiler (a text report). A workflow on main runs it; scheduled
workflows only run from the default branch. It does not run the log as code, and it only accepts an
attachment hosted on ntfy.sh.
"""
import json, os, re, sys, urllib.request

TOPIC = "rocknixds-perf-c4a91e7b2d08f653"  # keep in step with perf-session.py
LOGNAME = re.compile(r"^[0-9]{8}-[0-9]{6}_[A-Za-z0-9._-]{1,80}\.jsonl$")
PROFNAME = re.compile(r"^[0-9]{8}-[0-9]{6}_[A-Za-z0-9._-]{1,80}\.jsonl\.prof\.txt$")  # perf-session.py, keep in step
DEVICE = re.compile(r"^[0-9a-f]{8,32}$")
MAX = 8000000
PROFMAX = 1000000


def body_ok(text):
    if not text or len(text) > MAX:
        return False
    rows = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except ValueError:
            return False
    return bool(rows) and rows[-1].get("summary") is True


def prof_ok(text):
    """The profiler's report: text with its header line, nothing else is taken under that name."""
    return bool(text) and len(text) <= PROFMAX and text.startswith("# SuperDrastic sampling profiler")


def relpath(device, filename):
    if not DEVICE.fullmatch(device or "") or not (LOGNAME.fullmatch(filename or "") or PROFNAME.fullmatch(filename or "")):
        return None
    return "docs/data/device/%s/%s" % (device, filename)


def fetch(url, limit=MAX + 1):
    from urllib.parse import urlparse
    u = urlparse(url)
    if u.scheme != "https" or u.hostname != "ntfy.sh":
        raise ValueError("refused host")
    req = urllib.request.Request(url, headers={"User-Agent": "ROCKNIXDS"})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read(limit)
    if len(data) > MAX:
        raise ValueError("too big")
    return data.decode("utf-8")


def poll(topic=TOPIC):
    url = "https://ntfy.sh/%s/json?poll=1&since=all" % topic
    req = urllib.request.Request(url, headers={"User-Agent": "ROCKNIXDS"})
    with urllib.request.urlopen(req, timeout=60) as r:
        raw = r.read().decode("utf-8", "replace")
    out = []
    for line in raw.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            out.append(json.loads(line))
        except ValueError:
            continue
    return out


def already(root):
    path = os.path.join(root, "docs/data/device/.ingested")
    try:
        return set(l.strip() for l in open(path) if l.strip())
    except OSError:
        return set()


def remember(root, ids):
    d = os.path.join(root, "docs/data/device")
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, ".ingested")
    cur = already(root)
    cur.update(ids)
    with open(path, "w") as f:
        f.write("\n".join(sorted(cur)) + "\n")


def import_messages(root, messages, fetch_url=fetch):
    """Write new sessions. Returns how many files were added. Dead attachments are marked ingested."""
    seen = already(root)
    fresh, done = [], 0
    for msg in messages:
        if done >= 30:
            break
        mid = msg.get("id") or ""
        if not mid or mid in seen:
            continue
        att = msg.get("attachment") or {}
        rel = relpath(msg.get("title") or "", att.get("name") or "")
        url = att.get("url") or ""
        if not rel or not url:
            fresh.append(mid)  # not a session; don't look at it again
            continue
        dest = os.path.join(root, rel)
        if os.path.exists(dest):
            fresh.append(mid)
            continue
        try:
            text = fetch_url(url)
        except Exception as e:
            # an expired attachment will not come back; a network error should retry next run
            if "404" in str(e) or "410" in str(e) or "refused" in str(e):
                fresh.append(mid)
            print("skip %s: %s" % (mid, e), file=sys.stderr)
            continue
        if not (prof_ok(text) if rel.endswith(".prof.txt") else body_ok(text)):
            fresh.append(mid)
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "w") as f:
            f.write(text if text.endswith("\n") else text + "\n")
        fresh.append(mid)
        done += 1
        print("imported", rel)
    if fresh:
        remember(root, fresh)
    return done


def selftest():
    assert body_ok('{"t":1}\n{"summary": true}\n')
    assert not body_ok('{"summary": true}\nnot json\n')
    assert not body_ok("")
    assert relpath("abc123def456", "20231114-221320_Heart_Gold.nds.jsonl")
    assert relpath("../etc", "20231114-221320_a.jsonl") is None
    assert relpath("abc123def456", "../x.jsonl") is None
    assert relpath("abc123def456", "20231114-221320_a.nds.jsonl.prof.txt")
    assert relpath("abc123def456", "20231114-221320_a.nds.prof.txt") is None
    assert prof_ok("# SuperDrastic sampling profiler 0.3.0, pid 1: report 1 (exit)\n") and not prof_ok('{"summary": true}\n')
    import tempfile, shutil
    root = tempfile.mkdtemp()
    try:
        files = {"https://ntfy.sh/file/ok.json": '{"t": 1, "game": {"rom": "a.nds"}}\n{"summary": true, "game": {"rom": "a.nds"}}\n',
                 "https://ntfy.sh/file/prof.txt": "# SuperDrastic sampling profiler 0.3.0, pid 1: report 1 (exit)\n"}
        def fake(url):
            if url not in files:
                raise urllib.error.HTTPError(url, 404, "gone", None, None)
            return files[url]
        n = import_messages(root, [
            {"id": "1", "title": "abc123def456", "attachment": {"name": "20231114-221320_a.nds.jsonl", "url": "https://ntfy.sh/file/ok.json"}},
            {"id": "2", "title": "abc123def456", "attachment": {"name": "not a name", "url": "https://evil.example/x"}},
            {"id": "3", "title": "nope", "attachment": {"name": "20231114-221320_a.nds.jsonl", "url": "https://ntfy.sh/file/ok.json"}},
            {"id": "4", "title": "abc123def456", "attachment": {"name": "20231114-221320_a.nds.jsonl.prof.txt", "url": "https://ntfy.sh/file/prof.txt"}},
            {"id": "5", "title": "abc123def456", "attachment": {"name": "20231114-221320_b.nds.jsonl.prof.txt", "url": "https://ntfy.sh/file/ok.json"}},
        ], fake)
        assert n == 2, n
        assert open(os.path.join(root, "docs/data/device/abc123def456/20231114-221320_a.nds.jsonl.prof.txt")).read().startswith("# SuperDrastic")
        assert not os.path.exists(os.path.join(root, "docs/data/device/abc123def456/20231114-221320_b.nds.jsonl.prof.txt"))
        got = open(os.path.join(root, "docs/data/device/abc123def456/20231114-221320_a.nds.jsonl")).read()
        assert '"summary": true' in got
        assert import_messages(root, [
            {"id": "1", "title": "abc123def456", "attachment": {"name": "20231114-221320_a.nds.jsonl", "url": "https://ntfy.sh/file/ok.json"}},
        ], fake) == 0
        try:
            fetch("http://127.0.0.1/x")
            raise SystemExit("http was accepted")
        except ValueError:
            pass
        try:
            fetch("https://evil.example/file")
            raise SystemExit("other host was accepted")
        except ValueError:
            pass
    finally:
        shutil.rmtree(root)
    print("ingest-perf selftest ok")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "selftest":
        return selftest()
    if len(sys.argv) != 2:
        sys.exit("usage: ingest-perf.py <device-logs checkout> | selftest")
    n = import_messages(sys.argv[1], poll())
    print("imported %d" % n)


if __name__ == "__main__":
    main()
