#!/usr/bin/env python3
"""Static Pixelify Sans instances for the rnds engine, with the GPOS kerning as a legacy 'kern' table.

    python3 gen_fonts.py PixelifySans[wght].ttf outdir
    python3 gen_fonts.py --glyphs PixelifySans-Regular.ttf PixelifySans-Medium.ttf    (only the redrawn glyphs, in place)

Chrome shapes Pixelify Sans with HarfBuzz, which applies GPOS kerning. FreeType's FT_Get_Kerning only reads the
legacy 'kern' table, so the pairs that GPOS defines for the characters the engine draws (Latin-1, Latin Extended-A
and general punctuation) are flattened into one, per weight (the variable font's kerning varies with wght).

Four glyphs are redrawn for legibility (issue #34): Pixelify Sans's 5 has a rounded top and no stem, so "05" read as
"0S"; its 2 and Z are near twins, and its B reads as G or 8 ("Pokémon Glack"). The new ones sit on the font's own 5x7
pixel grid (taken from the zero's bounds) with the same advance widths, so spacing and kerning stay as they were.
"""
import os, sys
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0
from fontTools.varLib import instancer
from fontTools.pens.ttGlyphPen import TTGlyphPen

WEIGHTS = {"Regular": 400, "Medium": 500}
CHARS = [c for c in range(0x20, 0x250)] + [c for c in range(0x2000, 0x2070)] + [0x20AC, 0x2122]


CLEARER = {
    "five": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "two":  [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "Z":    ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    "B":    ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
}


def clearer_glyphs(font):
    """Redraw CLEARER's glyphs on the font's 5x7 grid: one clockwise rectangle per run of pixels in a row."""
    glyf, cmap = font["glyf"], font.getBestCmap()
    zero = glyf[cmap[ord("0")]]
    zero.recalcBounds(glyf)
    x0, x1, y0, y1 = zero.xMin, zero.xMax, zero.yMin, zero.yMax
    cw, rh = (x1 - x0) / 5, (y1 - y0) / 7
    for name, rows in CLEARER.items():
        pen = TTGlyphPen(None)
        for r, row in enumerate(rows):
            top, bot = round(y1 - r * rh), round(y1 - (r + 1) * rh)
            c = 0
            while c < 5:
                if row[c] != "#":
                    c += 1
                    continue
                e = c
                while e < 5 and row[e] == "#":
                    e += 1
                left, right = round(x0 + c * cw), round(x0 + e * cw)
                pen.moveTo((left, bot)); pen.lineTo((left, top)); pen.lineTo((right, top)); pen.lineTo((right, bot))
                pen.closePath()
                c = e
        g = pen.glyph()
        glyf[name] = g
        g.recalcBounds(glyf)
        font["hmtx"][name] = (font["hmtx"][name][0], g.xMin)


def pair_values(font):
    """{(left glyph, right glyph): x advance adjustment} from GPOS PairPos (formats 1 and 2), kern feature only."""
    gpos = font["GPOS"].table
    order = font.getGlyphOrder()
    lookups = set()
    for fr in gpos.FeatureList.FeatureRecord:
        if fr.FeatureTag == "kern":
            lookups.update(fr.Feature.LookupListIndex)
    out = {}
    for li in sorted(lookups):
        lk = gpos.LookupList.Lookup[li]
        subs = lk.SubTable
        if lk.LookupType == 9:
            subs = [s.ExtSubTable for s in subs]
        for st in subs:
            if st.LookupType != 2:
                continue
            cov = st.Coverage.glyphs
            if st.Format == 1:
                for i, first in enumerate(cov):
                    for pvr in st.PairSet[i].PairValueRecord:
                        v = getattr(pvr.Value1, "XAdvance", 0) if pvr.Value1 else 0
                        out.setdefault((first, pvr.SecondGlyph), v)
            elif st.Format == 2:
                c1 = st.ClassDef1.classDefs if st.ClassDef1 else {}
                c2 = st.ClassDef2.classDefs if st.ClassDef2 else {}
                for first in cov:
                    k1 = c1.get(first, 0)
                    row = st.Class1Record[k1].Class2Record
                    for second in order:
                        k2 = c2.get(second, 0)
                        v = row[k2].Value1
                        v = getattr(v, "XAdvance", 0) if v else 0
                        if v:
                            out.setdefault((first, second), v)
    return out


def main():
    if sys.argv[1] == "--glyphs":
        for path in sys.argv[2:]:
            f = TTFont(path)
            clearer_glyphs(f)
            f.save(path)
            print(path, "glyphs redrawn:", " ".join(CLEARER))
        return
    src, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    for style, wght in WEIGHTS.items():
        vf = TTFont(src)
        inst = instancer.instantiateVariableFont(vf, {"wght": wght})
        cmap = inst.getBestCmap()
        wanted = {cmap[c] for c in CHARS if c in cmap}
        pairs = {k: v for k, v in pair_values(inst).items() if v and k[0] in wanted and k[1] in wanted}
        kern = newTable("kern")
        kern.version = 0
        kern.kernTables = []
        items = sorted(pairs.items())
        for i in range(0, len(items), 10000):          # format 0 holds up to 10920 pairs per subtable
            st = KernTable_format_0()
            st.version, st.coverage, st.format, st.apple = 0, 1, 0, False
            st.kernTable = dict(items[i:i + 10000])
            kern.kernTables.append(st)
        if kern.kernTables:
            inst["kern"] = kern
        clearer_glyphs(inst)
        name = inst["name"]
        for rec in name.names:
            if rec.nameID in (2, 17):
                rec.string = style
            elif rec.nameID in (4,):
                rec.string = "Pixelify Sans " + style
            elif rec.nameID in (6,):
                rec.string = "PixelifySans-" + style
        path = os.path.join(outdir, f"PixelifySans-{style}.ttf")
        inst.save(path)
        print(path, len(pairs), "kerning pairs")


if __name__ == "__main__":
    main()
