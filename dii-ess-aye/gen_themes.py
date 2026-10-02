#!/usr/bin/env python3
"""Build the two selectable themes from overlay/theme-rgds.xml and gen_skin.py.

    python3 dii-ess-aye/gen_themes.py /path/to/upstream-dii-ess-aye

The upstream checkout is only needed for assets/fonts/dsi_font.otf (glyphs become
paths, so the font is not copied into these folders). Install still downloads
upstream; these folders replace theme-rgds.xml and the SVG skin.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_XML = os.path.join(HERE, "overlay", "theme-rgds.xml")
BATTERY = os.path.join(HERE, "overlay", "assets", "images", "common")

# Hex colours in theme-rgds.xml, without the alpha byte. 3A3C40 is the name
# printed on the white cartridge label and stays in both themes. FFFFFF is the
# selection / scroll-thumb text, which sits on the blue plate in both.
DARK = {
    "E8E9EB": "E7EEF6",
    "A9AFB8": "93A0B0",
    "F2F3F5": "E7EEF6",
    "8FC8FF": "7EC8EE",
    "8A9098": "93A0B0",
    "A9AEB5": "93A0B0",
    "C9CDD2": "A8B4C0",
    "6E747D": "7D8B9C",
    "2B2E33": "12181F",
    "5D626A": "3D4A5A",
    "2B8FE6": "1A6EA3",
    "3C4047": "243040",
    "23252A": "1A222C",
}
LIGHT = {
    "E8E9EB": "1A2330",
    "A9AFB8": "3E5164",
    "F2F3F5": "121820",
    "8FC8FF": "0C4F86",
    "8A9098": "4A5E72",
    "A9AEB5": "3E5164",
    "C9CDD2": "243444",
    "6E747D": "4A5E72",
    "2B2E33": "F4F7FB",
    "5D626A": "8AA0B4",
    "2B8FE6": "1A6EA3",
    "3C4047": "D0DCE6",
    "23252A": "E6EEF5",
}

# Light status bar is a pale strip, so the light-grey battery body would vanish.
LIGHT_BATTERY = {
    "rgb(235,235,235)": "rgb(26,35,48)",
    "rgb(200,203,208)": "rgb(74,94,114)",
}


def colorize(xml, mapping):
    for src, dst in mapping.items():
        if src == dst:
            continue
        if src not in xml:
            sys.exit(f"colour {src} is not in theme-rgds.xml")
        xml = xml.replace(src, dst)
    return xml


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gen_themes.py /path/to/upstream-theme")
    font = os.path.join(sys.argv[1], "assets", "fonts", "dsi_font.otf")
    if not os.path.isfile(font):
        sys.exit(f"no DSi font at {font}")
    base = open(SRC_XML, encoding="utf-8").read()
    for name, palette, mapping in (
        ("rocknixds-dark", "dark", DARK),
        ("rocknixds-light", "light", LIGHT),
    ):
        dest = os.path.join(HERE, "themes", name)
        if os.path.isdir(dest):
            shutil.rmtree(dest)
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "assets", "fonts"))
            os.symlink(font, os.path.join(tmp, "assets", "fonts", "dsi_font.otf"))
            subprocess.check_call([sys.executable, os.path.join(HERE, "gen_skin.py"), tmp, palette])
            shutil.copytree(os.path.join(tmp, "assets"), os.path.join(dest, "assets"))
            # The font was only there so glyphs could be traced. Install symlinks the upstream copy.
            shutil.rmtree(os.path.join(dest, "assets", "fonts"), ignore_errors=True)
        xml = colorize(base, mapping)
        note = ("    <!-- Colours for the %s theme. Layout, features and storyboards match overlay/theme-rgds.xml.\n"
                "         Regenerate with dii-ess-aye/gen_themes.py after editing that file. -->\n" % name)
        xml = xml.replace("<theme defaultTransition=\"instant\" defaultView=\"gamecarousel\">\n",
                          "<theme defaultTransition=\"instant\" defaultView=\"gamecarousel\">\n" + note, 1)
        os.makedirs(dest, exist_ok=True)
        with open(os.path.join(dest, "theme-rgds.xml"), "w", encoding="utf-8") as f:
            f.write(xml)
        if name == "rocknixds-light":
            common = os.path.join(dest, "assets", "images", "common")
            for fn in os.listdir(BATTERY):
                if not fn.startswith("battery_icon_") or not fn.endswith(".svg"):
                    continue
                text = open(os.path.join(BATTERY, fn), encoding="utf-8").read()
                for src, dst in LIGHT_BATTERY.items():
                    text = text.replace(src, dst)
                with open(os.path.join(common, fn), "w", encoding="utf-8") as f:
                    f.write(text)
        print(name)


if __name__ == "__main__":
    main()
