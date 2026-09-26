#!/usr/bin/env python3
"""ra-fetch.py <outdir>  (run ON the device)

For every game ES knows with a RetroAchievements id (cheevosId, which ES fills in itself), fetches the achievement
set and the user's unlocks from RetroAchievements' client API, with the account ROCKNIX already has
(system.cfg: global.retroachievements.username / .token). The token stays on the device. Read-only: it uses
r=patch (the set) and r=unlocks (softcore unlocks); it does not start a play session.
Writes <outdir>/ra.json ({ES game id: {...}}) and <outdir>/<RA game id>.png (the game's badge).
"""
import json, os, sys, urllib.parse, urllib.request

CFG = "/storage/.config/system/configs/system.cfg"
API = "https://retroachievements.org/dorequest.php"
UA = "rgds-theme/1.0 (ROCKNIX; RG DS) ra-fetch"


def cfg(key):
    with open(CFG) as f:
        for line in f:
            if line.startswith(key + "="):
                return line.split("=", 1)[1].strip()
    return ""


def call(**params):
    req = urllib.request.Request(API, data=urllib.parse.urlencode(params).encode(), headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    user, token = cfg("global.retroachievements.username"), cfg("global.retroachievements.token")
    if not user or not token:
        sys.exit("no RetroAchievements account in system.cfg")
    games = json.load(urllib.request.urlopen("http://localhost:1234/systems/nds/games"))
    result = {}
    for g in games:
        gid = int(g.get("cheevosId") or 0)
        if gid <= 0:
            result[g["id"]] = {"name": g["name"], "ra": None}
            continue
        patch = call(r="patch", u=user, t=token, g=gid)
        pd = patch.get("PatchData") or {}
        core = [a for a in pd.get("Achievements", []) if a.get("Flags") == 3]
        unl = call(r="unlocks", u=user, t=token, g=gid, h=0)
        got = set(unl.get("UserUnlocks") or [])
        info = {"name": g["name"], "ra": gid, "title": pd.get("Title", ""),
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
        result[g["id"]] = info
        print(g["name"], info["unlocked"], "/", info["total"], "achievements,", info["points"], "points")
    with open(os.path.join(out, "ra.json"), "w") as f:
        json.dump(result, f, indent=1)


if __name__ == "__main__":
    main()
