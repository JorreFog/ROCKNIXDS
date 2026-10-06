#!/usr/bin/env python3
"""gamelist.py add|remove <dir>: Döda Kvarter's entry in an EmulationStation game list (gamelist.xml in <dir>: its own
system's folder, or Ports), with its name, description and pictures. Other entries are left as they are. Run while ES
is stopped (ES writes the file back when it quits)."""
import os
import sys
import xml.etree.ElementTree as ET

PATH = "./Doda Kvarter.sh"
FIELDS = [
    ("name", "Döda Kvarter"),
    ("desc", "A zombie roguelike across both screens, in a Swedish suburb at night. Survive round after round "
             "as in Call of Duty Zombies: earn kronor, clear barricades into new districts (miljonprogram yards, "
             "the centrum and its tunnelbana, Falu red villas, gamla stan, allotments, the church), find the power, "
             "buy Julmust and Snabbkaffe, try Lådan (beware the Dalahäst) and upgrade at Smedjan. Every town is new; "
             "the loot gets better every round. Top screen: the game. Bottom screen: your inventory, health, ammo "
             "and the map."),
    ("image", "./images/dodakvarter-image.png"),
    ("thumbnail", "./images/dodakvarter-thumb.png"),
    ("marquee", "./images/dodakvarter-marquee.png"),
    ("releasedate", "20261006T000000"),
    ("developer", "ROCKNIXDS"),
    ("publisher", "ROCKNIXDS"),
    ("genre", "Shooter / Roguelike"),
    ("players", "1"),
]


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("add", "remove"):
        sys.exit("usage: gamelist.py add|remove <dir>")
    gl = os.path.join(sys.argv[2], "gamelist.xml")
    if os.path.exists(gl):
        try:
            tree = ET.parse(gl)
            root = tree.getroot()
        except ET.ParseError:
            sys.exit(f"{gl} doesn't parse: left alone")
    else:
        root = ET.Element("gameList")
        tree = ET.ElementTree(root)
    for g in root.findall("game"):
        p = g.find("path")
        if p is not None and p.text and p.text.strip() in (PATH, PATH[2:]):
            root.remove(g)
    if sys.argv[1] == "add":
        g = ET.SubElement(root, "game")
        ET.SubElement(g, "path").text = PATH
        for k, v in FIELDS:
            ET.SubElement(g, k).text = v
    if hasattr(ET, "indent"):
        ET.indent(tree, space="\t")
    tmp = gl + ".tmp"
    tree.write(tmp, encoding="utf-8", xml_declaration=True)
    os.replace(tmp, gl)


if __name__ == "__main__":
    main()
