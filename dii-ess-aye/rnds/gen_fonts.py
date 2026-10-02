#!/usr/bin/env python3
"""Static Pixelify Sans instances for the rnds engine, with the GPOS kerning as a legacy 'kern' table.

    python3 gen_fonts.py PixelifySans[wght].ttf outdir

Chrome shapes Pixelify Sans with HarfBuzz, which applies GPOS kerning. FreeType's FT_Get_Kerning only reads the
legacy 'kern' table, so the pairs that GPOS defines for the characters the engine draws (Latin-1, Latin Extended-A
and general punctuation) are flattened into one, per weight (the variable font's kerning varies with wght).
"""
import os, sys
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0
from fontTools.varLib import instancer

WEIGHTS = {"Regular": 400, "Medium": 500}
CHARS = [c for c in range(0x20, 0x250)] + [c for c in range(0x2000, 0x2070)] + [0x20AC, 0x2122]


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
