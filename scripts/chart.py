#!/usr/bin/env python3
"""Benchmark CSVs (one per workload) -> markdown tables and an SVG bar chart.

Usage: chart.py [--markdown OUT.md] [--svg OUT.svg] [--svg-dark DARK.svg] CSV...

Standard library only. Tables: time as x plain (the ratio column), resident
memory growth and heap reserved in MiB, one column per workload (in order of
first appearance), one row per library.
Chart: the speed-up over bump & reval, estimated as central differences
((2 x inputs + 1) plain double evaluations), divided by the time for value and gradient; one panel per workload,
horizontal bars from a 1x line on one log scale, in light or dark colours; the
README's chart is results/amd/speedup-{light,dark}.svg.
"""

import argparse
import csv
import importlib.util
import math
import pathlib
from xml.sax.saxutils import escape

MIB = 2.0**20


def load_csvs(paths):
    """Rows (dicts) of the harness CSVs, in the order given."""
    rows = []
    for p in paths:
        with open(p, newline="") as f:
            rows.extend(csv.DictReader(f))
    return rows


def _ordered(values):
    return list(dict.fromkeys(values))


def _libraries(rows):
    """Libraries in the README results table's order, then the others as they appear."""
    present = _ordered(r["library"] for r in rows)
    known = _RT.LIBRARY_NAMES
    return [l for l in known if l in present] + [l for l in present if l not in known]


def _table(title, rows, value, with_plain):
    workloads = _ordered(r["workload"] for r in rows)
    cell = {(r["library"], r["workload"]): value(r) for r in rows}
    lines = [
        f"### {title}",
        "",
        "| library | " + " | ".join(workloads) + " |",
        "|---|" + "---:|" * len(workloads),
    ]
    for lib in _libraries(rows):
        if lib == "plain" and not with_plain:
            continue
        cells = [f"{cell[lib, w]:.2f}" if (lib, w) in cell else "n/a" for w in workloads]
        lines.append(f"| {lib} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def render_markdown(rows):
    """Time, resident memory growth and heap reserved tables, as markdown."""
    return "\n".join([
        _table("Time (x plain)", rows, lambda r: float(r["ratio"]), False),
        _table("Resident memory growth (MiB)", rows,
               lambda r: int(r["peak_memory_growth_bytes"]) / MIB, True),
        _table("Heap reserved (MiB)", rows, lambda r: int(r["peak_heap_reserved_bytes"]) / MIB, True),
    ])


def _results_table():
    path = pathlib.Path(__file__).with_name("results_table.py")
    spec = importlib.util.spec_from_file_location("results_table", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_RT = _results_table()

# Colour roles per theme. XAD in the first categorical hue, the add-on in the
# second, every other library in a recessive gray (labels carry identity).
THEMES = {
    "light": {"surface": "#fcfcfb", "ink": "#0b0b0b", "ink2": "#52514e", "grid": "#e1e0d9",
              "axis": "#52514e", "xad": "#2a78d6", "addon": "#eb6834", "other": "#a3a29c"},
    "dark": {"surface": "#1a1a19", "ink": "#f0efec", "ink2": "#c3c2b7", "grid": "#2c2c2a",
             "axis": "#c3c2b7", "xad": "#3987e5", "addon": "#d95926", "other": "#64635e"},
}
ROLE = {"xad": "xad", "xad-default": "xad", "xad-codegen": "addon"}
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"
SUBTITLE = ("Time for bump & reval ÷ time for value and gradient with AD. Bump & reval is estimated as central "
            "differences: two plain double evaluations per input plus the base value. Higher is better; log scale. XAD + codegen "
            "evaluates vectorised; the other libraries and the bump & reval estimate are scalar.")
KEY = [("xad", "XAD"), ("addon", "XAD + codegen (commercial add-on)"), ("other", "Other AD libraries")]

# Layout (px): one column of panels, each with its own row labels, so the
# chart stays narrow enough to read on a phone.
_W = 600
_PAD = 16
_LABEL_W = 164
_PLOT_L = _PAD + _LABEL_W
_PLOT_W = _W - _PLOT_L - _PAD - 8
_ROW, _BAR = 22, 13
_DECADES = (-1, 3)  # x axis from 0.1x to 1000x
_CHAR_W = 6.4  # rough width of one 13.5 px character, for wrapping


def _wrap(text, size):
    """Lines of at most about (_W - 2 * _PAD) px of text at font size `size`."""
    width = (_W - 2 * _PAD) / (_CHAR_W * size / 13.5)
    lines, line = [], ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > width:
            lines.append(line)
            line = word
        else:
            line = f"{line} {word}" if line else word
    return lines + [line]


def _fmt(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


NOT_CHARTED = ("plain", "fd")  # the reference and finite differences, which bump & reval stands for


def _bump_evaluations(workload):
    """Plain evaluations of bump & reval by central differences: two per input, plus the base value."""
    return 2 * _RT.WORKLOAD_INPUTS[workload] + 1


def _bump_times(rows):
    """{workload: (2 x inputs + 1) x plain time}, central differences, for workloads with a plain row and a
    known input count, i.e. run at the published size."""
    return {r["workload"]: _bump_evaluations(r["workload"]) * float(r["median_s"])
            for r in rows if r["library"] == "plain" and _RT.at_published_size(rows, r["workload"])}


def _speedups(rows):
    """{(library, workload): bump & reval time / time}, plain and fd excluded."""
    bump = _bump_times(rows)
    return {(r["library"], r["workload"]): bump[r["workload"]] / float(r["median_s"])
            for r in rows if r["library"] not in NOT_CHARTED and r["workload"] in bump}


def _svg_libraries(rows):
    present = _ordered(r["library"] for r in rows if r["library"] not in NOT_CHARTED)
    names = _RT.LIBRARY_NAMES
    return [l for l in names if l in present] + [l for l in present if l not in names]


def _bar(x0, x1, y, h, fill, attrs):
    """A horizontal bar from x0 (baseline, square) to x1 (data end, 4px round)."""
    left, width = min(x0, x1), abs(x1 - x0)
    r = min(4.0, width / 2)
    cap_x = x0 - r if x1 < x0 else x0
    return (f'<rect class="bar" x="{left:.2f}" y="{y:.2f}" width="{width:.2f}" height="{h}" rx="{r:.2f}" '
            f'fill="{fill}" {attrs}/>'
            f'<rect x="{cap_x:.2f}" y="{y:.2f}" width="{r:.2f}" height="{h}" fill="{fill}"/>')


def render_svg(rows, theme="light"):
    """Small multiples of the speed-up over bump & reval, one panel per workload; SVG text."""
    c = THEMES[theme]
    ratio = _speedups(rows)
    bump = _bump_times(rows)
    workloads = [w for w in _ordered(r["workload"] for r in rows) if w in bump]
    libs = _svg_libraries(rows)
    per_decade = _PLOT_W / (_DECADES[1] - _DECADES[0])

    def x_of(v):
        return _PLOT_L + (math.log10(v) - _DECADES[0]) * per_decade

    head = [f'<text x="{_PAD}" y="30" font-size="18" font-weight="600">'
            f"AD speed-up over bump &amp; reval</text>"]
    y = 32
    for line in _wrap(SUBTITLE, 13.5):
        y += 20
        head.append(f'<text x="{_PAD}" y="{y}" font-size="13.5" fill="{c["ink2"]}">{escape(line)}</text>')
    y += 14
    kx = _PAD
    for role, text in KEY:
        head.append(f'<rect x="{kx}" y="{y}" width="12" height="12" rx="3" fill="{c[role]}"/>')
        head.append(f'<text x="{kx + 17}" y="{y + 10.5}" font-size="12.5">{escape(text)}</text>')
        kx += 17 + 6.4 * len(text) + 18
    top = y + 30
    panel_h = 26 + len(libs) * _ROW + 20 + 16

    body = []
    for i, w in enumerate(workloads):
        y0 = top + i * panel_h
        title = escape(_RT.WORKLOAD_NAMES.get(w, w))
        sub = (f"bump & reval: {_bump_evaluations(w)} evaluations, {_RT._time(bump[w])}"
               if w in bump else "")
        body.append(f'<text x="{_PAD}" y="{y0 + 14}" font-size="15" font-weight="600">{title}'
                    f'<tspan dx="8" font-size="12.5" font-weight="400" fill="{c["ink2"]}">{escape(sub)}</tspan>'
                    f"</text>")
        plot_top, plot_bot = y0 + 24, y0 + 24 + len(libs) * _ROW
        for d in range(_DECADES[0], _DECADES[1] + 1):
            x = x_of(10.0**d)
            body.append(f'<line x1="{x:.2f}" x2="{x:.2f}" y1="{plot_top}" y2="{plot_bot}" stroke="{c["grid"]}"/>')
            body.append(f'<text x="{x:.2f}" y="{plot_bot + 15}" font-size="12" text-anchor="middle" '
                        f'fill="{c["ink2"]}">{_fmt(10.0**d)}×</text>')
        one = x_of(1.0)
        for k, lib in enumerate(libs):
            yr = plot_top + k * _ROW
            yb = yr + (_ROW - _BAR) / 2
            name = _RT.LIBRARY_NAMES.get(lib, lib)
            main, _, extra = name.partition(" (")
            if extra:
                body.append(f'<text x="{_PLOT_L - 12}" y="{yr + 9:.2f}" font-size="14" text-anchor="end">'
                            f"{escape(main)}</text>")
                body.append(f'<text x="{_PLOT_L - 12}" y="{yr + 21:.2f}" font-size="11" text-anchor="end" '
                            f'fill="{c["ink2"]}">({escape(extra)}</text>')
            else:
                body.append(f'<text x="{_PLOT_L - 12}" y="{yr + 15.5:.2f}" font-size="14" text-anchor="end">'
                            f"{escape(name)}</text>")
            if (lib, w) not in ratio:
                continue
            v = ratio[lib, w]
            xe = x_of(v)
            label = _RT._times_plain(v)
            attrs = f'data-library="{escape(lib)}" data-workload="{escape(w)}" data-speedup="{v:.2f}"'
            body.append(_bar(one, xe, yb, _BAR, c[ROLE.get(lib, "other")], attrs))
            anchor, lx = ("start", xe + 5) if v >= 1 else ("end", xe - 5)
            body.append(f'<text x="{lx:.2f}" y="{yb + _BAR - 2:.2f}" font-size="13" text-anchor="{anchor}">'
                        f"{label}</text>")
        body.append(f'<line class="ref" data-workload="{escape(w)}" x1="{one:.2f}" x2="{one:.2f}" '
                    f'y1="{plot_top - 3}" y2="{plot_bot + 2}" stroke="{c["axis"]}" stroke-width="1.5"/>')
    height = top + len(workloads) * panel_h - 6 + 18
    return "\n".join([
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{_W}" height="{height}" viewBox="0 0 {_W} {height}" '
        f'font-family="{FONT}" font-size="13" fill="{c["ink"]}">',
        f'<rect width="{_W}" height="{height}" rx="8" fill="{c["surface"]}"/>',
        *head, *body, "</svg>"]) + "\n"


def main(argv=None):
    p = argparse.ArgumentParser(description="Benchmark CSVs -> markdown tables and SVG chart.")
    p.add_argument("--markdown", help="write the markdown tables here")
    p.add_argument("--svg", help="write the SVG chart (light colours) here")
    p.add_argument("--svg-dark", help="write the SVG chart in dark colours here")
    p.add_argument("csv", nargs="+", help="benchmark CSV files, one per workload")
    a = p.parse_args(argv)
    if not (a.markdown or a.svg or a.svg_dark):
        p.error("nothing to write: give --markdown, --svg and/or --svg-dark")
    rows = load_csvs(a.csv)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(render_markdown(rows))
    for path, theme in ((a.svg, "light"), (a.svg_dark, "dark")):
        if path:
            with open(path, "w") as f:
                f.write(render_svg(rows, theme))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
