#!/usr/bin/env python3
"""uvdtb.py: reads and rewrites the CPU and GPU OPP voltages in the RG DS's device tree (a flattened DTB), in place.

ROCKNIX's RG DS and RG DS Plus DTBs carry mainline rk3568.dtsi's OPP tables. Those are Rockchip's worst-case
voltages: the vendor kernel bins every chip at boot (L0..L3, from its PVTM ring oscillator and leakage fuses) and
runs a better chip lower; mainline has no binning, so every chip runs at L0. The presets here are Rockchip's own
tables for each bin (vendor rk3568.dtsi, develop-5.10), which are also what Anbernic's firmware applies.

Only the first two cells of opp-microvolt (target and min) change; max, opp-hz and everything else stay. The value
is the same length, so the DTB's size and layout don't move. No dtc needed: python3 is all ROCKNIX has.

  uvdtb.py show DTB                     the tables: "cpu 1992 1100  (stock 1150, -50)" (MHz, mV), one OPP per line
  uvdtb.py check DTB                    exit 0 if this is a DTB this tool knows how to patch
  uvdtb.py isstock DTB                  exit 0 if its voltages are all stock
  uvdtb.py lowered DTB                  the CPU OPPs under stock: "1992 1100000" (MHz, uV), slowest first
  uvdtb.py patch IN OUT SPEC...         write OUT: IN with SPEC applied; prints the new tables
                                        SPEC: [stock|l1|l2|l3] [gpu=stock|l1|l2|l3] [cpu1992=1000 gpu800=925 ...]
  uvdtb.py plan IN SPEC...              the same, without writing anything
"""
import struct
import sys

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9

STEP = 12500            # both regulators (SYR827 vdd_cpu, RK817 DCDC2 vdd_gpu) step in 12.5 mV
CPU_FLOOR = 800000      # below Rockchip's lowest CPU OPP voltage (850) with room for the tinkerers; GammaOS uses 825
GPU_FLOOR = 825000      # vdd_gpu's regulator-min-microvolt in the RG DS DTS: the kernel can't go lower

# mainline rk3568.dtsi = vendor L0; L1..L3 are the vendor's opp-microvolt-L1..L3 (MHz: mV)
PRESETS = {
    "stock": {
        "cpu": {408: 850, 600: 850, 816: 850, 1104: 900, 1416: 1025, 1608: 1100, 1800: 1150, 1992: 1150},
        "gpu": {200: 850, 300: 850, 400: 850, 600: 900, 700: 950, 800: 1000},
    },
    "l1": {
        "cpu": {408: 850, 600: 850, 816: 850, 1104: 850, 1416: 975, 1608: 1050, 1800: 1100, 1992: 1150},
        "gpu": {200: 850, 300: 850, 400: 850, 600: 875, 700: 925, 800: 975},
    },
    "l2": {
        "cpu": {408: 850, 600: 850, 816: 850, 1104: 850, 1416: 950, 1608: 1025, 1800: 1075, 1992: 1125},
        "gpu": {200: 850, 300: 850, 400: 850, 600: 850, 700: 900, 800: 950},
    },
    "l3": {
        "cpu": {408: 850, 600: 850, 816: 850, 1104: 850, 1416: 925, 1608: 1000, 1800: 1050, 1992: 1100},
        "gpu": {200: 850, 300: 850, 400: 850, 600: 850, 700: 875, 800: 925},
    },
}
STOCK = PRESETS["stock"]


class DtbError(Exception):
    pass


def _cstr(b, off):
    end = b.index(b"\0", off)
    return b[off:end].decode("ascii", "replace"), end


class Fdt:
    """The nodes of a DTB: path -> {property: (value offset, length)}, enough to read and patch values in place."""

    def __init__(self, data):
        self.data = bytearray(data)
        if len(data) < 40:
            raise DtbError("too short for a DTB")
        magic, total, off_struct, off_strings = struct.unpack_from(">4I", data, 0)
        if magic != FDT_MAGIC:
            raise DtbError("not a DTB (bad magic)")
        if total != len(data):
            raise DtbError("DTB size field %d != file size %d" % (total, len(data)))
        size_struct = struct.unpack_from(">I", data, 36)[0]
        self.nodes = {}
        stack = []
        p, end = off_struct, off_struct + size_struct
        while p < end:
            tok = struct.unpack_from(">I", data, p)[0]
            p += 4
            if tok == FDT_BEGIN_NODE:
                name, q = _cstr(data, p)
                p = (q + 1 + 3) & ~3
                stack.append(name)
                path = "/" + "/".join(stack[1:])
                self.nodes[path] = {}
            elif tok == FDT_END_NODE:
                stack.pop()
            elif tok == FDT_PROP:
                ln, nameoff = struct.unpack_from(">2I", data, p)
                p += 8
                pname, _ = _cstr(data, off_strings + nameoff)
                self.nodes["/" + "/".join(stack[1:])][pname] = (p, ln)
                p = (p + ln + 3) & ~3
            elif tok == FDT_NOP:
                pass
            elif tok == FDT_END:
                break
            else:
                raise DtbError("bad token %d at %d" % (tok, p - 4))

    def prop(self, path, name):
        off, ln = self.nodes[path][name]
        return bytes(self.data[off:off + ln])

    def cells(self, path, name):
        v = self.prop(path, name)
        return list(struct.unpack(">%dI" % (len(v) // 4), v))

    def phandle_path(self, ph):
        for path, props in self.nodes.items():
            for k in ("phandle", "linux,phandle"):
                if k in props and self.cells(path, k) == [ph]:
                    return path
        raise DtbError("no node with phandle %d" % ph)


def _table(fdt, owner):
    """{MHz: (path, [microvolt cells])} for the OPP table owner's operating-points-v2 points at."""
    tpath = fdt.phandle_path(fdt.cells(owner, "operating-points-v2")[0])
    out = {}
    for path, props in fdt.nodes.items():
        if path.rsplit("/", 1)[0] != tpath or "opp-hz" not in props or "opp-microvolt" not in props:
            continue
        hi, lo = fdt.cells(path, "opp-hz")[:2]
        mhz = ((hi << 32) | lo) // 1000000
        out[mhz] = (path, fdt.cells(path, "opp-microvolt"))
    return out


def tables(fdt):
    """{"cpu": {MHz: (path, cells)}, "gpu": {...}}: the CPU table is the one cpu@0 uses, the GPU's the one the
    Mali node (gpu@...) uses. Raises DtbError unless both have exactly the RG DS's frequencies."""
    cpu = [p for p in fdt.nodes if p.startswith("/cpus/cpu@") and "operating-points-v2" in fdt.nodes[p]]
    gpu = [p for p in fdt.nodes if p.rsplit("/", 1)[-1].startswith("gpu@") and "operating-points-v2" in fdt.nodes[p]]
    if not cpu or len(gpu) != 1:
        raise DtbError("no CPU or GPU OPP table")
    t = {"cpu": _table(fdt, sorted(cpu)[0]), "gpu": _table(fdt, gpu[0])}
    for kind in ("cpu", "gpu"):
        if sorted(t[kind]) != sorted(STOCK[kind]):
            raise DtbError("%s OPPs %s are not the RG DS's %s" % (kind, sorted(t[kind]), sorted(STOCK[kind])))
        for mhz, (path, cells) in t[kind].items():
            if len(cells) not in (1, 3):
                raise DtbError("%s opp-microvolt has %d cells" % (path, len(cells)))
    return t


def voltages(fdt):
    """{"cpu": {MHz: mV}, "gpu": {MHz: mV}} (the target voltage; 912.5 for a half step)."""
    return {k: {mhz: c[0] / 1000 for mhz, (_, c) in v.items()} for k, v in tables(fdt).items()}


def parse_spec(args):
    """SPEC: an optional CPU preset (stock, l1, l2, l3; default stock), gpu=PRESET for the GPU (default stock),
    then single OPPs: cpu1992=1000 gpu800=925 (MHz=mV). A CPU preset leaves the GPU at stock: uvstress checks the
    CPU's results, nothing checks the GPU's. Returns {"cpu": {MHz: mV}, "gpu": {MHz: mV}} with every OPP."""
    want = {k: dict(v) for k, v in STOCK.items()}
    seen_opp = False
    for a in args:
        low = a.lower()
        name = low[4:] if low.startswith("gpu=") else low
        if name in PRESETS:
            if seen_opp:
                raise DtbError("presets (%s) go before single OPPs" % a)
            kind = "gpu" if low.startswith("gpu=") else "cpu"
            want[kind] = dict(PRESETS[name][kind])
            continue
        try:
            k, v = low.split("=")
            kind, mhz = k[:3], int(k[3:])
            mv = float(v)
        except ValueError:
            raise DtbError("can't read %r: use a preset (stock, l1, l2, l3), gpu=l3, or cpu1992=1000 / gpu800=925" % a)
        if kind not in want or mhz not in want[kind]:
            raise DtbError("%r: no such OPP (cpu: %s; gpu: %s)" % (a, sorted(STOCK["cpu"]), sorted(STOCK["gpu"])))
        want[kind][mhz] = mv
        seen_opp = True
    return want


def validate(want):
    """Raises DtbError unless every voltage is a 12.5 mV step, at or under stock, at or over the floor, and a
    faster OPP never gets less than a slower one."""
    for kind, floor in (("cpu", CPU_FLOOR), ("gpu", GPU_FLOOR)):
        prev = 0
        for mhz in sorted(want[kind]):
            uv = int(round(want[kind][mhz] * 1000))
            if uv % STEP:
                raise DtbError("%s %d MHz: %g mV is not a 12.5 mV step" % (kind, mhz, want[kind][mhz]))
            if uv > STOCK[kind][mhz] * 1000:
                raise DtbError("%s %d MHz: %g mV is over stock (%d): this only undervolts"
                               % (kind, mhz, want[kind][mhz], STOCK[kind][mhz]))
            if uv < floor:
                raise DtbError("%s %d MHz: %g mV is under the %g mV floor" % (kind, mhz, want[kind][mhz], floor / 1000))
            if uv < prev:
                raise DtbError("%s %d MHz: %g mV is lower than a slower OPP's" % (kind, mhz, want[kind][mhz]))
            prev = uv


def patch(data, want):
    """data with the OPP voltages set to want ({kind: {MHz: mV}}): returns the new bytes (same length)."""
    validate(want)
    fdt = Fdt(data)
    for kind, t in tables(fdt).items():
        for mhz, (path, cells) in t.items():
            uv = int(round(want[kind][mhz] * 1000))
            new = [uv, uv] + cells[2:] if len(cells) == 3 else [uv]
            if len(cells) == 3 and uv > cells[2]:
                raise DtbError("%s: %d uV is over its max %d" % (path, uv, cells[2]))
            off, ln = fdt.nodes[path]["opp-microvolt"]
            fdt.data[off:off + ln] = struct.pack(">%dI" % len(new), *new)
    out = bytes(fdt.data)
    back = {k: {m: int(round(v * 1000)) for m, v in t.items()} for k, t in voltages(Fdt(out)).items()}
    if len(out) != len(data) or back != {k: {m: int(round(v * 1000)) for m, v in want[k].items()} for k in want}:
        raise DtbError("patched DTB doesn't read back as asked")
    return out


def fmt(v, ref=None):
    lines = []
    for kind in ("cpu", "gpu"):
        for mhz in sorted(v[kind]):
            mv = v[kind][mhz]
            s = "%s %4d %g" % (kind, mhz, mv)
            if ref is not None and ref[kind][mhz] != mv:
                s += "  (stock %d, %+g)" % (ref[kind][mhz], round(mv - ref[kind][mhz], 1))
            lines.append(s)
    return "\n".join(lines)


def main(argv):
    if len(argv) < 3 or argv[1] not in ("show", "check", "isstock", "lowered", "patch", "plan"):
        sys.stderr.write(__doc__)
        return 2
    try:
        data = open(argv[2], "rb").read()
        if argv[1] == "check":
            tables(Fdt(data))
            return 0
        if argv[1] == "show":
            print(fmt(voltages(Fdt(data)), STOCK))
            return 0
        if argv[1] == "isstock":
            return 0 if voltages(Fdt(data)) == STOCK else 1
        if argv[1] == "lowered":
            cpu = voltages(Fdt(data))["cpu"]
            for mhz in sorted(cpu):
                if cpu[mhz] < STOCK["cpu"][mhz]:
                    print(mhz, int(round(cpu[mhz] * 1000)))
            return 0
        rest = argv[4:] if argv[1] == "patch" else argv[3:]
        want = parse_spec(rest)
        out = patch(data, want)
        print(fmt(voltages(Fdt(out)), STOCK))
        if argv[1] == "patch":
            with open(argv[3], "wb") as f:
                f.write(out)
        return 0
    except (DtbError, OSError) as e:
        sys.stderr.write("uvdtb: %s\n" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
