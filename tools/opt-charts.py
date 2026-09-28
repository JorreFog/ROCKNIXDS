#!/usr/bin/env python3
"""opt-charts.py: the charts and tables of docs/optimization-1.4.md, from the measurements in docs/data/opt-1.4/.

  python3 tools/opt-charts.py            writes docs/img/opt-1.4/<chart>-light.png and -dark.png, and prints the
                                         tables the document quotes (docs/data/opt-1.4/summary.md)

Data: v1.3/ and v1.4/ come from tools/opt-suite.sh (each version measured right after a reboot into it), dev/ from
the development runs of 2026-09-28 (tools/power.sh, tools/shaders.sh), shaders-*.txt from tools/shaders.sh.
Style: the method in docs (light/dark variants, validated palette: blue #2a78d6 / #3987e5 for 1.4 and the series
that matter, a recessive gray for 1.3; categorical slots 1-4 for parts of a whole).
"""
import glob, gzip, json, os, re, statistics
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.path import Path
from matplotlib.patches import PathPatch

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.environ.get("OPT_DATA") or os.path.join(HERE, "..", "docs", "data", "opt-1.4")
IMG = os.environ.get("OPT_IMG") or os.path.join(HERE, "..", "docs", "img", "opt-1.4")

THEMES = {
    "light": dict(surface="#fcfcfb", ink="#0b0b0b", ink2="#52514e", muted="#898781", grid="#e1e0d9", axis="#c3c2b7",
                  s=["#2a78d6", "#eb6834", "#1baf7a", "#eda100"], before="#c3c2b7"),
    "dark": dict(surface="#1a1a19", ink="#ffffff", ink2="#c3c2b7", muted="#898781", grid="#2c2c2a", axis="#383835",
                 s=["#3987e5", "#d95926", "#199e70", "#c98500"], before="#898781"),
}
plt.rcParams.update({"font.family": ["Noto Sans", "DejaVu Sans"], "font.size": 9, "svg.fonttype": "none"})
DPI = 150
summary = []


def md(*lines):
    summary.extend(lines)


# ---------- data ----------
def jload(p):
    with open(p) as f:
        return json.load(f)


def probes(label, pat):
    return [jload(p) for p in sorted(glob.glob(os.path.join(DATA, label, "probe", f"{label}-{pat}.json")))]


def mean(xs):
    return statistics.mean(xs) if xs else float("nan")


def median(xs):
    return statistics.median(xs) if xs else float("nan")


def frames(txt):
    """presents/s and drops/s from a power.sh result text"""
    m = re.search(r"frames: \d+ s, ([\d.]+) presents/s, ([\d.]+) drops/s", open(txt).read())
    return (float(m.group(1)), float(m.group(2))) if m else (float("nan"), float("nan"))


def proc_pct(d, pattern):
    return sum(p[0] for p in d.get("processes_incl_children", []) if re.search(pattern, p[1]))


def thread_pct(d, comm, name):
    return sum(t[0] for t in d.get("threads", []) if t[1] == comm and t[2] == name)


# ---------- drawing helpers ----------
def setup(ax, t, xgrid=True):
    ax.set_facecolor(t["surface"])
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(t["axis"]); ax.spines["bottom"].set_linewidth(1)
    ax.tick_params(colors=t["muted"], length=0, labelsize=8)
    if xgrid:
        ax.grid(axis="x", color=t["grid"], linewidth=0.7); ax.set_axisbelow(True)


def header(fig, t, head, sub, items):
    """title, subtitle and legend at fixed distances from the top (inches), so they never collide; returns the
    figure fraction where the plot area may start"""
    h = fig.get_figheight()
    fig.text(0.012, 1 - 0.10 / h, head, color=t["ink"], fontsize=11, fontweight="semibold", va="top", ha="left")
    if sub:
        fig.text(0.012, 1 - 0.36 / h, sub, color=t["ink2"], fontsize=8.5, va="top", ha="left")
    y_in = 0.62 if sub else 0.40
    if items:
        legend(fig, t, items, 1 - (y_in + 0.07) / h)
        y_in += 0.3
    return 1 - (y_in + 0.12) / h


def legend(fig, t, items, y):
    """swatch (box, line or marker) + label in text ink, left to right"""
    x = 0.012
    for color, label, kind in items:
        if kind == "line":
            fig.lines.append(plt.Line2D([x, x + 0.03], [y, y], transform=fig.transFigure, color=color, lw=2,
                                        solid_capstyle="round"))
            w = 0.03
        elif kind in ("D", "o"):
            fig.lines.append(plt.Line2D([x + 0.008], [y], transform=fig.transFigure, color=color, marker=kind,
                                        markersize=6, linestyle="none"))
            w = 0.016
        else:
            hh = 0.1 / fig.get_figheight()
            fig.patches.append(plt.Rectangle((x, y - hh / 2), 0.016, hh, transform=fig.transFigure, color=color,
                                             linewidth=0))
            w = 0.016
        tx = fig.text(x + w + 0.012, y, label, color=t["ink2"], fontsize=8.5, va="center", ha="left")
        fig.canvas.draw()
        x = tx.get_window_extent().x1 / fig.bbox.width + 0.035


def hbar(ax, y, x0, x1, h, color, r_px=4):
    """a horizontal bar from x0 to x1, the data end rounded (4 px), square at the baseline"""
    if x1 <= x0:
        return
    fig = ax.figure
    fig.canvas.draw()
    bb = ax.get_window_extent()
    xl, yl = ax.get_xlim(), ax.get_ylim()
    rx = r_px * (xl[1] - xl[0]) / bb.width
    ry = r_px * abs(yl[1] - yl[0]) / bb.height
    rx = min(rx, (x1 - x0) / 2); ry = min(ry, h / 2)
    y0, y1 = y - h / 2, y + h / 2
    verts = [(x0, y0), (x1 - rx, y0), (x1, y0), (x1, y0 + ry), (x1, y1 - ry), (x1, y1), (x1 - rx, y1), (x0, y1), (x0, y0)]
    codes = [Path.MOVETO, Path.LINETO, Path.CURVE3, Path.CURVE3, Path.LINETO, Path.CURVE3, Path.CURVE3, Path.LINETO,
             Path.CLOSEPOLY]
    ax.add_patch(PathPatch(Path(verts, codes), facecolor=color, edgecolor="none", linewidth=0))


def save(fig, name, theme):
    os.makedirs(IMG, exist_ok=True)
    fig.savefig(os.path.join(IMG, f"{name}-{theme}.png"), dpi=DPI, facecolor=THEMES[theme]["surface"])
    plt.close(fig)


def bar_px(ax, px):
    """bar thickness in data units for a thickness in pixels (bars stay <= 24 px)"""
    ax.figure.canvas.draw()
    return px * abs(ax.get_ylim()[1] - ax.get_ylim()[0]) / ax.get_window_extent().height


# ---------- charts ----------
def compare_panels(name, head, sub, panels, theme):
    """small multiples: one panel per measure, a 1.3 bar (gray) and a 1.4 bar (blue) in each, value at the bar end"""
    t = THEMES[theme]
    n = len(panels)
    panel_in, gap_in = 0.62, 0.5
    h = 1.32 + n * panel_in + (n - 1) * gap_in + 0.45      # header, panels with their headings, the last x axis
    fig = plt.figure(figsize=(6, h), facecolor=t["surface"])
    top = header(fig, t, head, sub, [(t["before"], "1.3", "box"), (t["s"][0], "1.4", "box")])
    for k, (label, unit, v13, v14, fmt) in enumerate(panels):
        y1 = top - (0.28 + k * (panel_in + gap_in)) / h
        ax = fig.add_axes([0.13, y1 - panel_in / h, 0.78, panel_in / h])
        setup(ax, t)
        xmax = max(v13, v14) * 1.18
        ax.set_xlim(0, xmax); ax.set_ylim(-0.7, 1.7)
        ax.set_yticks([1, 0]); ax.set_yticklabels(["1.3", "1.4"], color=t["ink2"], fontsize=8.5)
        hh = bar_px(ax, 14)
        hbar(ax, 1, 0, v13, hh, t["before"]); hbar(ax, 0, 0, v14, hh, t["s"][0])
        for y, v in ((1, v13), (0, v14)):
            ax.text(v + xmax * 0.012, y, fmt.format(v), color=t["ink"], fontsize=8.5, va="center", ha="left")
        fig.text(0.13, y1 + 0.06 / h, f"{label} ({unit})", color=t["ink2"], fontsize=8.5, va="bottom", ha="left")
        ax.tick_params(axis="x", labelsize=7.5)
    save(fig, name, theme)


def stacked(name, head, sub, rows, parts, theme, unit="% of one core"):
    """part-to-whole horizontal bars: rows = [(label, [v per part])], parts = [names] (categorical slots 1..n)"""
    t = THEMES[theme]
    h = 1.05 + 0.4 * len(rows) + 0.55
    fig = plt.figure(figsize=(6, h), facecolor=t["surface"])
    top = header(fig, t, head, sub, [(t["s"][i], p, "box") for i, p in enumerate(parts)])
    ax = fig.add_axes([0.25, 0.5 / h, 0.68, top - 0.5 / h])
    setup(ax, t)
    tot = [sum(v) for _, v in rows]
    xmax = max(tot) * 1.12
    ax.set_xlim(0, xmax); ax.set_ylim(len(rows) - 0.4, -0.6)
    ax.set_yticks(range(len(rows))); ax.set_yticklabels([r[0] for r in rows], color=t["ink2"], fontsize=8.5)
    hh = bar_px(ax, 16)
    gap = 2 * (xmax / ax.get_window_extent().width)          # the 2 px surface gap between segments
    for i, (_, vals) in enumerate(rows):
        x = 0
        for k, v in enumerate(vals):
            if v <= 0:
                continue
            last = k == len(vals) - 1 or all(w <= 0 for w in vals[k + 1:])
            if last:
                hbar(ax, i, x, x + v, hh, t["s"][k])
            else:
                ax.add_patch(plt.Rectangle((x, i - hh / 2), max(v - gap, 0), hh, facecolor=t["s"][k], linewidth=0))
            x += v
        ax.text(x + xmax * 0.012, i, f"{x:.0f}%", color=t["ink"], fontsize=8.5, va="center", ha="left")
    ax.set_xlabel(unit, color=t["muted"], fontsize=8)
    save(fig, name, theme)


def lines(name, head, sub, series, xlabel, ylabel, theme, xlim=None, ylim=None, markers=False, points=(),
          end_labels=True, extra_legend=()):
    """series = [(label, xs, ys, slot)] (slot -1: the 1.3 gray): 2 px lines; end labels only where they don't
    collide (end_labels=False when the series converge: the legend carries identity then)"""
    t = THEMES[theme]
    h = 3.3
    fig = plt.figure(figsize=(6, h), facecolor=t["surface"])
    items = [(t["s"][s] if s >= 0 else t["before"], l, "line") for l, _, _, s in series] + list(extra_legend)
    top = header(fig, t, head, sub, items)
    ax = fig.add_axes([0.1, 0.55 / h, 0.8 if end_labels else 0.87, top - 0.55 / h])
    setup(ax, t, xgrid=False)
    ax.grid(axis="y", color=t["grid"], linewidth=0.7); ax.set_axisbelow(True)
    for label, xs, ys, s in series:
        c = t["s"][s] if s >= 0 else t["before"]
        ax.plot(xs, ys, color=c, lw=2, solid_capstyle="round", solid_joinstyle="round",
                marker="o" if markers else None, markersize=7, markeredgecolor=t["surface"], markeredgewidth=2)
        if end_labels:
            ax.text(xs[-1] + (xs[-1] - xs[0]) * 0.015, ys[-1], label, color=t["ink2"], fontsize=8, va="center", ha="left")
    for x, y, slot, label in points:                     # extra results: diamonds in the series' colour
        ax.plot([x], [y], "D", color=t["s"][slot], markersize=7, markeredgecolor=t["surface"], markeredgewidth=2, zorder=5)
    if xlim: ax.set_xlim(*xlim)
    if ylim: ax.set_ylim(*ylim)
    ax.set_xlabel(xlabel, color=t["muted"], fontsize=8); ax.set_ylabel(ylabel, color=t["muted"], fontsize=8)
    save(fig, name, theme)


def dumbbell(name, head, sub, items, theme, unit):
    """before -> after per item: a gray dot (1.3) and a blue dot (1.4) joined by a hairline, values beside"""
    t = THEMES[theme]
    h = 1.05 + 0.3 * len(items) + 0.5
    fig = plt.figure(figsize=(6, h), facecolor=t["surface"])
    top = header(fig, t, head, sub, [(t["before"], "1.3 (frame uploaded, 1.3 shaders)", "o"),
                                     (t["s"][0], "1.4 (frame imported, 1.4 shaders)", "o")])
    ax = fig.add_axes([0.24, 0.5 / h, 0.69, top - 0.5 / h])
    setup(ax, t)
    xmax = max(max(a, b) for _, a, b in items) * 1.25
    ax.set_xlim(0, xmax); ax.set_ylim(len(items) - 0.5, -0.6)
    ax.set_yticks(range(len(items))); ax.set_yticklabels([i[0] for i in items], color=t["ink2"], fontsize=8.5)
    for i, (label, a, b) in enumerate(items):
        ax.plot([a, b], [i, i], color=t["axis"], lw=1.2, zorder=1)
        ax.plot([a], [i], "o", color=t["before"], markersize=8, markeredgecolor=t["surface"], markeredgewidth=2, zorder=2)
        ax.plot([b], [i], "o", color=t["s"][0], markersize=8, markeredgecolor=t["surface"], markeredgewidth=2, zorder=3)
        ax.text(max(a, b) + xmax * 0.025, i, f"{a:.2f} → {b:.2f}", color=t["ink"], fontsize=8, va="center", ha="left")
    ax.set_xlabel(unit, color=t["muted"], fontsize=8)
    save(fig, name, theme)


# ---------- the document's charts ----------
def idle():
    a, b = probes("v1.3", "idle-*"), probes("v1.4", "idle-*")
    if not a or not b:
        return
    m = lambda ds, k: mean([d[k] for d in ds])
    temp = lambda ds: mean([d["temp_c"][1] for d in ds])
    panels = [("CPU used", "% of one core", m(a, "cpu_busy_pct"), m(b, "cpu_busy_pct"), "{:.0f}%"),
              ("Average CPU clock", "MHz", m(a, "cpu_avg_mhz"), m(b, "cpu_avg_mhz"), "{:.0f}"),
              ("Average GPU clock", "MHz", m(a, "gpu_avg_mhz"), m(b, "gpu_avg_mhz"), "{:.0f}")]
    for th in THEMES:
        compare_panels("idle", "The menu left alone", "EmulationStation idle in the system view, 3 x 60 s each, "
                       "after a reboot", panels, th)
    parts = ["Drawing the menu (ES, sway)", "Audio (PipeWire)", "ROCKNIX services", "Kernel and the rest"]
    def split(d):
        draw = proc_pct(d, r"emulationstation|/usr/bin/sway|swaybg")
        audio = proc_pct(d, AUDIO)
        svc = proc_pct(d, SERVICES)
        return [draw, audio, svc, max(d["cpu_busy_pct"] - draw - audio - svc, 0)]
    ra, rb = [split(d) for d in a], [split(d) for d in b]
    avg = lambda rs: [mean([r[i] for r in rs]) for i in range(4)]
    rows = [("1.3", avg(ra)), ("1.4", avg(rb))]
    for th in THEMES:
        stacked("idle-split", "Where an idle menu's CPU goes", "Each process with the processes it starts, "
                "averaged over 3 x 60 s", rows, parts, th)
    md("## Idle menu", "", "| | 1.3 | 1.4 |", "|---|---|---|",
       f"| CPU used (% of one core) | {m(a,'cpu_busy_pct'):.1f} | {m(b,'cpu_busy_pct'):.1f} |",
       f"| Average CPU clock (MHz) | {m(a,'cpu_avg_mhz'):.0f} | {m(b,'cpu_avg_mhz'):.0f} |",
       f"| Average GPU clock (MHz) | {m(a,'gpu_avg_mhz'):.0f} | {m(b,'gpu_avg_mhz'):.0f} |",
       f"| SoC temperature at the end (°C) | {temp(a):.1f} | {temp(b):.1f} |",
       f"| Processes started per second | {m(a,'forks_per_s'):.1f} | {m(b,'forks_per_s'):.1f} |",
       f"| Battery current (mA, on the charger, + = charging) | {m(a,'bat_ma'):+.0f} | {m(b,'bat_ma'):+.0f} |", "")
    md("| Where it goes (% of one core) | 1.3 | 1.4 |", "|---|---|---|",
       *[f"| {p} | {rows[0][1][i]:.1f} | {rows[1][1][i]:.1f} |" for i, p in enumerate(parts)], "")


SERVICES = r"powerstate|battery.led|touchscreen-keyboard|input_sense"
AUDIO = r"^/usr/bin/pipewire|wireplumber"


def game():
    shaders = ["none", "ds-crisp", "sharp-bilinear"]
    have = all(probes(v, f"hg-{s}-*") for v in ("v1.3", "v1.4") for s in shaders)
    if not have:
        return
    parts = ["DraStic + libdsflip", "Audio (PipeWire)", "ROCKNIX services", "Kernel and the rest"]
    rows, table, runs = [], [], []
    for s in shaders:
        for v in ("v1.3", "v1.4"):
            ds = probes(v, f"hg-{s}-*")
            def split(d):
                game = proc_pct(d, r"drastic\.real")
                dl = thread_pct(d, "drastic.real", "data-loop.0")
                audio = dl + proc_pct(d, AUDIO)
                svc = proc_pct(d, SERVICES)
                return [game - dl, audio, svc, max(d["cpu_busy_pct"] - game - (audio - dl) - svc, 0)]
            sp = [split(d) for d in ds]
            med = [median([x[i] for x in sp]) for i in range(4)]
            name = "no shader" if s == "none" else s
            rows.append((f"{name}, {v[1:]}", med))
            txts = sorted(glob.glob(os.path.join(DATA, v, "probe", f"{v}-hg-{s}-*.txt")))
            fr = [frames(p) for p in txts]
            runs.append((name, v[1:], [round(f[1], 2) for f in fr]))
            table.append((name, v[1:], median([d["cpu_busy_pct"] for d in ds]), median([d["cpu_avg_mhz"] for d in ds]),
                          median([d["gpu_avg_mhz"] for d in ds]), median([d["temp_c"][1] - d["temp_c"][0] for d in ds]),
                          median([f[1] for f in fr]), median([d["bat_ma"] for d in ds]), med[1], med[2]))
    for th in THEMES:
        stacked("game-split", "HeartGold at 2x: where the CPU goes",
                "Walking in town, median of 3 x 90 s per setting, the CPU governor ROCKNIX sets for DS games", rows, parts, th)
    md("## HeartGold at 2x (median of 3 runs)", "",
       "| Shader | Version | CPU used (% of one core) | Avg CPU clock (MHz) | Avg GPU clock (MHz) | SoC rise in 90 s (°C) | Drops/s | Audio (% of one core) | ROCKNIX services (% of one core) | Battery (mA) |",
       "|---|---|---|---|---|---|---|---|---|---|",
       *[f"| {a} | {b} | {c:.0f} | {d:.0f} | {e:.0f} | {f:+.1f} | {g:.2f} | {i:.1f} | {j:.1f} | {h:+.0f} |" for a, b, c, d, e, f, g, h, i, j in table], "",
       "Dropped frames per run:", "", *[f"- {a}, {b}: {', '.join(f'{x:.2f}' for x in r)}" for a, b, r in runs], "")
    panels = []
    for s in shaders:
        a = [x for x in table if x[0] == ("no shader" if s == "none" else s)]
        panels.append((("no shader" if s == "none" else s), "average CPU clock, MHz", a[0][3], a[1][3], "{:.0f}"))
    for th in THEMES:
        compare_panels("game-clock", "HeartGold at 2x: the CPU clock",
                       "1.3 runs DS games at 1992 MHz; 1.4's governor picks the clock from the game's load", panels, th)


def stress():
    def series(v):
        p = os.path.join(DATA, v, "logs", f"sg-{v}-stress.log")
        if not os.path.exists(p) and not os.path.exists(p + ".gz"):
            return None
        fps, clock, cur = [], [], None
        for line in (open(p) if os.path.exists(p) else gzip.open(p + ".gz", "rt")):
            m = re.search(r"cpugov\] \d+ -> (\d+) MHz", line)
            if m:
                cur = int(m.group(1))
            m = re.search(r"cpugov\] on: (\d+)\.\.(\d+)", line)
            if m:
                cur = int(m.group(2))
            m = re.search(r"present/s=([\d.]+)", line)
            if m:
                fps.append(float(m.group(1))); clock.append(cur or 1992)
        return fps, clock
    a, b = series("v1.3"), series("v1.4")
    if not a or not b:
        return
    n = min(len(a[0]), len(b[0]))
    xs = list(range(1, n + 1))
    for th in THEMES:
        lines("stress-fps", "The 3D stress ROM: frames per second",
              "Levels 1 to 10, one every 300 emulated frames; 60 fps holds until the load is too heavy for any clock",
              [("1.3", xs, a[0][:n], -1), ("1.4", xs, b[0][:n], 0)], "seconds", "frames per second", th, ylim=(30, 64),
              end_labels=False)
        lines("stress-clock", "The 3D stress ROM: CPU clock",
              "1.3 stays at 1992 MHz; 1.4 starts low on the light levels and reaches 1992 MHz as the load grows",
              [("1.3", xs, [1992] * n, -1), ("1.4", xs, b[1][:n], 0)], "seconds", "MHz", th, ylim=(900, 2100),
              end_labels=False)
    tail = lambda f: mean(f[-30:])
    md("## Stress ROM", "", f"Last 30 s (heaviest levels): 1.3 {tail(a[0][:n]):.1f} fps, 1.4 {tail(b[0][:n]):.1f} fps. "
       f"Average clock: 1.3 1992 MHz, 1.4 {mean(b[1][:n]):.0f} MHz.", "")


def shader_chart():
    def read(p):
        out = {}
        if not os.path.exists(p):
            return out
        for line in open(p):
            f = line.split()
            if len(f) == 3 and re.match(r"^[\d.]+$", f[1]):
                out[f[0]] = (float(f[1]), float(f[2]))
        return out
    a, b = read(os.path.join(DATA, "shaders-v1.3.txt")), read(os.path.join(DATA, "shaders-v1.4.txt"))
    names = [n for n in a if n in b]
    if not names:
        return
    items = sorted([(n, a[n][0], b[n][0]) for n in names], key=lambda x: x[2])
    for th in THEMES:
        dumbbell("shaders", "GPU time per panel at 2x, per shader",
                 "Real frames, GPU at 800 MHz; the null shader is the floor every shader pays", items, th, "ms per 640x480 panel")
    md("## Shaders (ms per panel at 800 MHz)", "", "| Shader | 1.3, 2x | 1.4, 2x | 1.3, 1x | 1.4, 1x |", "|---|---|---|---|---|",
       *[f"| {n} | {a[n][0]:.2f} | {b[n][0]:.2f} | {a[n][1]:.2f} | {b[n][1]:.2f} |" for n, _, _ in items], "")


def sweep():
    p = os.path.join(DATA, "dev", "clock-sweep.json")
    if not os.path.exists(p):
        return
    d = jload(p)
    ser = []
    for i, (label, pts) in enumerate(d["fixed"].items()):
        xs = sorted(int(k) for k in pts)
        ser.append((label, xs, [mean(pts[str(x)]) for x in xs], i))
    gov = [(g["mhz"], g["drops"], list(d["fixed"]).index(label), label) for label, g in d["governor"].items()]
    for th in THEMES:
        lines("clock-sweep", "Dropped frames at a fixed CPU clock", "HeartGold at 2x, walking: the mean of the runs "
              "at each clock, and 1.4's governor at its average clock", ser, "CPU clock (MHz)",
              "dropped frames per second", th, markers=True, points=gov, xlim=(1000, 2080), end_labels=False,
              extra_legend=[(THEMES[th]["muted"], "1.4 governor", "D")])
    md("## Clock sweep (development runs)", "", "| Setting | Clock (MHz) | Drops/s per run |", "|---|---|---|",
       *[f"| {label}, fixed | {x} | {', '.join(f'{v:.2f}' for v in pts[str(x)])} |" for label, pts in d["fixed"].items()
         for x in sorted(int(k) for k in pts)],
       *[f"| {label}, 1.4 governor | {g['mhz']} (average) | {g['drops']:.2f} |" for label, g in d["governor"].items()],
       *[f"| {label}, schedutil | {g['mhz']} (average) | {g['drops']:.2f} |" for label, g in d["schedutil"].items()], "")


def switch():
    """launch/quit milestones (tools/switchtime.sh), median of the cycles"""
    def read(p):
        rows = []
        if os.path.exists(p):
            for line in open(p):
                f = line.split()
                if len(f) == 10 and f[0].isdigit() and f[1] != "|":
                    rows.append([float(x) for x in f[1:5] + f[6:10]])
                elif len(f) == 10 and f[0].isdigit():
                    pass
        return rows
    def parse(p):
        out = []
        if os.path.exists(p):
            for line in open(p):
                m = re.match(r"^(\d+)\s+\|\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+\|\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)", line)
                if m:
                    out.append([float(m.group(i)) for i in range(2, 10)])
        return out
    a = parse(os.path.join(DATA, "v1.3", "v1.3-switch.txt"))
    b = parse(os.path.join(DATA, "v1.4", "v1.4-switch-final.txt"))
    if not a or not b:
        return
    names = ["libdsflip has the display", "first frame", "the menu answers", "the menu is visible"]
    idx = [2, 3, 6, 7]
    med = lambda rows, i: median([r[i] for r in rows])
    md("## Switching (seconds, median of 3 cycles)", "", "| | 1.3 | 1.4 |", "|---|---|---|",
       *[f"| {'Launch' if i < 4 else 'Quit'}: {n} | {med(a, i):.2f} | {med(b, i):.2f} |" for n, i in zip(names, idx)], "")


if __name__ == "__main__":
    for f in (idle, game, stress, shader_chart, sweep, switch):
        f()
    os.makedirs(DATA, exist_ok=True)
    with open(os.path.join(DATA, "summary.md"), "w") as f:
        f.write("\n".join(summary) + "\n")
    print("\n".join(summary))
