#!/usr/bin/env python3
"""ra-fetch.py <outdir> [ES game id ...]  (run ON the device)

For every game ES knows (or only the games named) with a RetroAchievements id (cheevosId, which ES fills in itself), fetches the achievement
set and the user's unlocks from RetroAchievements' client API, with the account ROCKNIX already has
(system.cfg: global.retroachievements.username / .token). The token stays on the device. Read-only: it uses
r=patch (the set) and r=unlocks (softcore unlocks); it does not start a play session.
Writes <outdir>/ra.json ({ES game id: {...}}) and <outdir>/<RA game id>.png (the game's badge).
"""
import json, os, sys, time, urllib.error, urllib.parse, urllib.request

CFG = "/storage/.config/system/configs/system.cfg"
API = "https://retroachievements.org/dorequest.php"
UA = "rgds-theme/1.0 (ROCKNIX; RG DS) ra-fetch"
WARNING_ID = 101000001                      # RC_CLIENT_ACHIEVEMENT_WARNING_ID in rcheevos


def cfg(key):
    with open(CFG) as f:
        for line in f:
            if line.startswith(key + "="):
                return line.split("=", 1)[1].strip()
    return ""


def call(**params):
    """One API request; retries rate limits and server errors (a long list of games can hit them)."""
    req = urllib.request.Request(API, data=urllib.parse.urlencode(params).encode(), headers={"User-Agent": UA})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                data = json.load(r)
            break
        except urllib.error.HTTPError as e:
            if attempt == 3 or (e.code != 429 and e.code < 500):
                raise
        except (urllib.error.URLError, TimeoutError):
            if attempt == 3:
                raise
        time.sleep(2 ** attempt * 2)
    if isinstance(data, dict) and data.get("Success") is False:
        raise RuntimeError(data.get("Error") or "request refused")
    return data


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    user, token = cfg("global.retroachievements.username"), cfg("global.retroachievements.token")
    if not user or not token:
        sys.exit("no RetroAchievements account in system.cfg")
    games = json.load(urllib.request.urlopen("http://localhost:1234/systems/nds/games"))
    only = set(sys.argv[2:])
    if only:
        games = [g for g in games if g["id"] in only]
    result = {}
    for g in games:
        gid = int(g.get("cheevosId") or 0)
        if gid <= 0:
            result[g["id"]] = {"name": g["name"], "ra": None}
            continue
        try:
            info = fetch(user, token, gid, g["name"], out)
        except Exception as e:                  # one bad game must not cost every other game its strip
            info = {"name": g["name"], "ra": gid, "error": str(e)}
            print(g["name"], "RetroAchievements error:", e)
        else:
            print(g["name"], info["unlocked"], "/", info["total"], "achievements,", info["points"], "points")
        result[g["id"]] = info
    with open(os.path.join(out, "ra.json"), "w") as f:
        json.dump(result, f, indent=1)


def fetch(user, token, gid, name, out):
    patch = call(r="patch", u=user, t=token, g=gid)
    pd = patch.get("PatchData") or {}
    # core achievements (Flags 3), minus RA's warning pseudo-achievements (id >= 101000001, e.g. "Warning:
    # Unknown Emulator", which the server adds for clients it doesn't know, like this script, and reports as
    # unlocked even for a game never played; rcheevos skips them the same way)
    core = [a for a in pd.get("Achievements", []) if a.get("Flags") == 3 and a["ID"] < WARNING_ID]
    unl = call(r="unlocks", u=user, t=token, g=gid, h=0)
    got = set(unl.get("UserUnlocks") or [])
    info = {"name": name, "ra": gid, "title": pd.get("Title", ""),
            "total": len(core), "points": sum(a.get("Points", 0) for a in core),
            "unlocked": sum(1 for a in core if a["ID"] in got),
            "unlocked_points": sum(a.get("Points", 0) for a in core if a["ID"] in got)}
    icon = pd.get("ImageIconURL") or ""
    if icon:
        try:
            req = urllib.request.Request(icon, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=20) as r, open(os.path.join(out, "%d.png" % gid), "wb") as f:
                f.write(r.read())
        except OSError as e:
            info["icon_error"] = str(e)
    return info


if __name__ == "__main__":
    main()
