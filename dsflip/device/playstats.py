#!/usr/bin/env python3
"""playstats.py <rom> <seconds played>  -- record a DS game session in ES's gamelist, as ES itself would.

A libdsflip game stops ES while it runs, so ES never records the session: its play count, last played date and time
played stayed at the last game played some other way (issue #3). This does ES's post-game update instead, while ES
is stopped: play count +1, game time + the session (if 10 s or more, like ES), last played = now (local time, ES's
format). ES reads a game's gamelist entry from <system>/gamelist.xml and then from its per-game recovery file
(~/.emulationstation/recovery/<system>/<rom stem>.xml), which wins as long as its parentHash equals gamelist.xml's
size. So, exactly like ES after a game, this writes the updated entry to that recovery file and leaves gamelist.xml
alone (rewriting it would change its size and void every other game's recovery file). ES folds the recovery files
into gamelist.xml the next time it saves.
"""
import os, re, sys, time
import xml.etree.ElementTree as ET

RECOVERY = "/storage/.emulationstation/recovery"
ES_SETTINGS = "/storage/.config/emulationstation/es_settings.cfg"


def same(sysroot, path, rom):
    p = path if os.path.isabs(path) else os.path.join(sysroot, path)
    return os.path.normpath(p) == os.path.normpath(rom)


def find(xml_file, sysroot, rom, parent_hash=None):
    try:
        root = ET.parse(xml_file).getroot()
    except (OSError, ET.ParseError):
        return None
    if parent_hash is not None and root.get("parentHash") != str(parent_hash):
        return None                                   # ES ignores a recovery file for another gamelist.xml
    for g in root.findall("game"):
        if same(sysroot, g.findtext("path", ""), rom):
            return g
    return None


def main():
    rom, secs = os.path.abspath(sys.argv[1]), int(float(sys.argv[2]))
    try:
        if re.search(r'name="SaveGamelistsOnExit" value="false"', open(ES_SETTINGS).read()):
            return                                    # ES itself records nothing then
    except OSError:
        pass
    parts = rom.split("/")
    if "roms" not in parts[:-2]:
        return
    i = parts.index("roms")
    system, sysroot = parts[i + 1], "/".join(parts[:i + 2])
    rel = os.path.relpath(rom, sysroot)
    gamelist = os.path.join(sysroot, "gamelist.xml")
    size = os.path.getsize(gamelist) if os.path.isfile(gamelist) else 0
    rec = os.path.join(RECOVERY, system, os.path.splitext(rel)[0] + ".xml")

    g = find(rec, sysroot, rom, size)
    if g is None and size:
        g = find(gamelist, sysroot, rom)
    if g is None:                                     # not in the gamelist yet: ES names it after the file
        g = ET.Element("game")
        ET.SubElement(g, "path").text = "./" + rel
        ET.SubElement(g, "name").text = os.path.splitext(os.path.basename(rom))[0]

    def num(tag):
        try:
            return int(g.findtext(tag, "0"))
        except ValueError:
            return 0

    def put(tag, value):
        e = g.find(tag)
        if e is None:
            e = ET.SubElement(g, tag)
        e.text = str(value)

    put("playcount", num("playcount") + 1)
    if secs >= 10:
        put("gametime", num("gametime") + secs)
    put("lastplayed", time.strftime("%Y%m%dT%H%M%S"))

    out = ET.Element("gameList", parentHash=str(size))
    out.append(g)
    ET.indent(out, space="\t")
    os.makedirs(os.path.dirname(rec), exist_ok=True)
    tmp = rec + ".tmp"
    with open(tmp, "wb") as f:
        f.write(('<?xml version="1.0"?>\n' + ET.tostring(out, encoding="unicode") + "\n").encode())
    os.replace(tmp, rec)
    print(f"play stats: {rel}: played {num('playcount')}x, {num('gametime')} s, last {g.findtext('lastplayed')}")


if __name__ == "__main__":
    main()
