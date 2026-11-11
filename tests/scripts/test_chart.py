"""scripts/chart.py: benchmark CSVs (one per workload) -> markdown tables and a chart.

Contract checked here (standard library only, so CI needs no plotting package):

  chart.load_csvs(paths) -> rows
      reads the harness CSVs (RFC 4180, quoted fields with commas) in the
      order given; workloads are ordered by first appearance.
  chart.render_markdown(rows) -> str
      three sections, separated by one blank line, ending in one newline:
        ### Time (x plain)                 ratio column, every library but plain
        ### Resident memory growth (MiB)   peak_memory_growth_bytes / 2**20
        ### Heap reserved (MiB)            peak_heap_reserved_bytes / 2**20
      each a table `| library | <workload> ... |`, alignment row
      `|---|---:|...|`, one row per library, cells with two decimals, "n/a"
      where a library has no row for that workload. Library order: the
      README results table's (results_table.LIBRARY_NAMES: plain, memory
      tables only, then xad-codegen, xad, xad-default, adept, cppad,
      autodiff, fd), then any other library in order of first
      appearance. No markers or footnotes, whatever the library_version says.
  chart.render_svg(rows, theme="light") -> str
      an SVG document of the speed-up over bump & reval: (2 x inputs + 1) x the
      plain row's time of the same workload (results_table.WORKLOAD_INPUTS),
      divided by the library's time; one panel per workload (workload
      order), each with a <line class="ref" data-workload=...> at 1x and one
      <rect class="bar"> per library but plain and fd, carrying data-library,
      data-workload and data-speedup (two decimals). Bars are horizontal on one
      log scale shared by all panels: they start at the 1x line and run right
      for a ratio above 1, left below 1, with width proportional to
      |log10(ratio)|; top to bottom in the README table's library order.
      theme="dark" gives the same chart (text and geometry) in the dark
      colours. Bar fills: one colour for xad and xad-default, another for
      xad-codegen, a third (muted) for every other library.
  python3 scripts/chart.py --markdown OUT.md --svg OUT.svg --svg-dark DARK.svg CSV...
      writes exactly render_markdown / render_svg / render_svg(theme="dark")
      of load_csvs(CSV...).
  Output is deterministic: no dates, no randomness.
  results/amd/speedup-light.svg and speedup-dark.svg are exactly the
  render_svg of results/amd/<workload>.csv (lmm, heston, cva, sabr), and
  README.md shows them in a <picture> by colour scheme. Neither the README
  nor the SVGs carry a footnote dagger or an instruction-set name (AVX).

The fixtures are trimmed from a development run (four libraries per
workload); the sabr memory figures are edited and "mylib" is a made-up extra
column present only in sabr.
"""

import importlib.util
import math
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = pathlib.Path(__file__).resolve().parent / "fixtures"
CSVS = [FIXTURES / f"{w}.csv" for w in ("lmm", "heston", "cva", "sabr")]
SCRIPT = ROOT / "scripts" / "chart.py"


def load_chart():
    spec = importlib.util.spec_from_file_location("chart", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


EXPECTED_MD = """\
### Time (x plain)

| library | lmm | heston | cva | sabr |
|---|---:|---:|---:|---:|
| xad | 4.48 | 3.57 | 9.15 | 8.52 |
| cppad | 16.55 | 8.33 | 8.74 | 16.90 |
| fd | 319.48 | 13.85 | 79.54 | 29.90 |
| mylib | n/a | n/a | n/a | 3.00 |

### Resident memory growth (MiB)

| library | lmm | heston | cva | sabr |
|---|---:|---:|---:|---:|
| plain | 0.07 | 0.36 | 0.38 | 0.00 |
| xad | 0.91 | 0.75 | 1.61 | 0.30 |
| cppad | 0.45 | 1.04 | 2.21 | 0.00 |
| fd | 0.13 | 0.36 | 0.39 | 0.00 |
| mylib | n/a | n/a | n/a | 1.00 |

### Heap reserved (MiB)

| library | lmm | heston | cva | sabr |
|---|---:|---:|---:|---:|
| plain | 0.00 | 0.00 | 0.19 | 0.00 |
| xad | 192.02 | 192.01 | 192.30 | 192.00 |
| cppad | 0.55 | 0.43 | 1.52 | 0.00 |
| fd | 0.01 | 0.00 | 0.19 | 0.00 |
| mylib | n/a | n/a | n/a | 3.00 |
"""


def test_markdown_tables_match_the_fixture():
    chart = load_chart()
    assert chart.render_markdown(chart.load_csvs(CSVS)) == EXPECTED_MD


def test_markdown_orders_libraries_like_the_readme_table():
    chart = load_chart()
    rows = chart.load_csvs(CSVS)
    xad = next(r for r in rows if r["library"] == "xad" and r["workload"] == "lmm")
    rows += [dict(xad, library=l) for l in ("autodiff", "adept", "xad-codegen")]
    time_table = chart.render_markdown(rows).split("\n\n")[1]
    libraries = [line.split(" | ")[0].lstrip("| ") for line in time_table.splitlines()[2:]]
    assert libraries == ["xad-codegen", "xad", "adept", "cppad", "autodiff", "fd", "mylib"]


NS = "{http://www.w3.org/2000/svg}"


def bars(svg_text):
    root = ET.fromstring(svg_text)
    assert root.tag == "{http://www.w3.org/2000/svg}svg"
    return [e for e in root.iter("{http://www.w3.org/2000/svg}rect") if e.get("class") == "bar"]


def _with_faster_than_plain(rows):
    # an extra library at half the plain time on lmm, to get a bar below 1x
    plain = next(r for r in rows if r["library"] == "plain" and r["workload"] == "lmm")
    return rows + [dict(plain, library="xad-codegen", median_s=repr(float(plain["median_s"]) / 2))]


def test_svg_bars_show_the_speed_up_over_bump_and_reval_on_one_log_scale():
    chart = load_chart()
    rows = _with_faster_than_plain(chart.load_csvs(CSVS))
    svg = chart.render_svg(rows)
    assert svg == chart.render_svg(rows)  # deterministic

    root = ET.fromstring(svg)
    ref = {e.get("data-workload"): float(e.get("x1")) for e in root.iter(f"{NS}line")
           if e.get("class") == "ref"}
    b = bars(svg)
    by_workload = {}
    for e in sorted(b, key=lambda e: float(e.get("y"))):
        by_workload.setdefault(e.get("data-workload"), []).append(e.get("data-library"))
    assert list(by_workload) == ["lmm", "heston", "cva", "sabr"] == list(ref)
    assert by_workload["lmm"] == ["xad-codegen", "xad", "cppad"]  # no bar for finite differences
    assert by_workload["sabr"] == ["xad", "cppad", "mylib"]

    # bump & reval = central differences, (2 x inputs + 1) plain evaluations: LMM 159, Heston 7, CVA 39, SABR 15 inputs
    speedup = {(w, lib): r for w, lib, r in [
        ("lmm", "xad-codegen", "638.00"), ("lmm", "xad", "71.26"), ("lmm", "cppad", "19.28"),
        ("heston", "xad", "4.20"), ("heston", "cppad", "1.80"),
        ("cva", "xad", "8.63"), ("cva", "cppad", "9.04"),
        ("sabr", "xad", "3.64"), ("sabr", "cppad", "1.83"), ("sabr", "mylib", "10.33")]}
    assert {(e.get("data-workload"), e.get("data-library")): e.get("data-speedup") for e in b} == speedup
    per_decade = []
    for e in b:
        r, x, width = float(e.get("data-speedup")), float(e.get("x")), float(e.get("width"))
        one = ref[e.get("data-workload")]
        assert (x == one) if r > 1 else (abs(x + width - one) < 1e-6)
        if abs(math.log10(r)) > 0.1:  # data-speedup has two decimals: too coarse to check widths near 1x
            per_decade.append(width / abs(math.log10(r)))
    assert max(per_decade) / min(per_decade) < 1.01  # one scale, width proportional to |log10(speed-up)|
    assert "bump &amp; reval" in svg and "Higher is better" in svg


def test_dark_theme_is_the_same_chart_in_other_colours():
    chart = load_chart()
    rows = _with_faster_than_plain(chart.load_csvs(CSVS))
    light, dark = chart.render_svg(rows), chart.render_svg(rows, theme="dark")
    colour = re.compile(r'#[0-9a-f]{6}')
    assert colour.sub("#", light) == colour.sub("#", dark)

    def fills(svg):
        fill = {e.get("data-library"): e.get("fill") for e in bars(svg)}
        return [fill["xad-codegen"], fill["xad"], fill["cppad"]], fill

    (addon, xad, other), fill = fills(light)
    assert len({addon, xad, other}) == 3 and fill["mylib"] == other
    (d_addon, d_xad, d_other), _ = fills(dark)
    assert len({d_addon, d_xad, d_other}) == 3 and d_xad != xad


def test_command_line_writes_the_same_markdown_and_svg(tmp_path):
    chart = load_chart()
    md, svg, dark = tmp_path / "out.md", tmp_path / "out.svg", tmp_path / "dark.svg"
    subprocess.run([sys.executable, str(SCRIPT), "--markdown", str(md), "--svg", str(svg),
                    "--svg-dark", str(dark), *map(str, CSVS)], check=True)
    rows = chart.load_csvs(CSVS)
    assert md.read_text() == chart.render_markdown(rows)
    assert svg.read_text() == chart.render_svg(rows)
    assert dark.read_text() == chart.render_svg(rows, theme="dark")


def test_restructured_rows_get_no_marker_or_footnote():
    chart = load_chart()
    rows = chart.load_csvs(CSVS)
    plain_md, plain_svg = chart.render_markdown(rows), chart.render_svg(rows)
    next(r for r in rows if r["library"] == "mylib")["library_version"] += ", restructured"
    assert chart.render_markdown(rows) == plain_md == EXPECTED_MD
    assert chart.render_svg(rows) == plain_svg
    assert "\u2020" not in plain_svg


def test_published_chart_is_rendered_from_the_published_results():
    chart = load_chart()
    published = ROOT / "results" / "amd"
    rows = chart.load_csvs([published / f"{w}.csv" for w in ("lmm", "heston", "cva", "sabr")])
    for theme in ("light", "dark"):
        svg = published / f"speedup-{theme}.svg"
        assert svg.read_text() == chart.render_svg(rows, theme), (
            f"{svg} is stale: python3 scripts/chart.py --svg results/amd/speedup-light.svg "
            "--svg-dark results/amd/speedup-dark.svg results/amd/{lmm,heston,cva,sabr}.csv")
    readme = (ROOT / "README.md").read_text()
    assert "\u2020" not in readme
    assert all("\u2020" not in (published / f"speedup-{t}.svg").read_text() for t in ("light", "dark"))
    assert '<source media="(prefers-color-scheme: dark)" srcset="results/amd/speedup-dark.svg">' in readme
    assert '<img src="results/amd/speedup-light.svg"' in readme


def test_readme_and_chart_name_no_instruction_set():
    for path in [ROOT / "README.md", *(ROOT / "results" / "amd").glob("speedup-*.svg")]:
        assert "avx" not in path.read_text().lower(), path


def test_a_workload_run_at_another_size_gets_no_panel():
    chart = load_chart()
    rows = chart.load_csvs(CSVS)
    for r in rows:
        if r["workload"] == "lmm":
            r["size"] = "16"
    svg = chart.render_svg(rows)
    assert {e.get("data-workload") for e in bars(svg)} == {"heston", "cva", "sabr"}
    assert ">LMM<" not in svg


def test_a_library_slower_than_bump_and_reval_gets_a_bar_left_of_1x_labelled_at_its_end():
    chart = load_chart()
    rows = chart.load_csvs(CSVS)
    plain = next(r for r in rows if r["library"] == "plain" and r["workload"] == "sabr")
    # 31 plain evaluations (15 inputs) take 31 x plain; 62 x plain is half as fast
    rows.append(dict(plain, library="slowlib", median_s=repr(62 * float(plain["median_s"]))))
    svg = chart.render_svg(rows)
    root = ET.fromstring(svg)
    one = next(float(e.get("x1")) for e in root.iter(f"{NS}line")
               if e.get("class") == "ref" and e.get("data-workload") == "sabr")
    bar = next(e for e in bars(svg) if e.get("data-library") == "slowlib")
    assert bar.get("data-speedup") == "0.50"
    assert float(bar.get("x")) < one and abs(float(bar.get("x")) + float(bar.get("width")) - one) < 1e-6
    label = next(e for e in root.iter(f"{NS}text") if e.text == "0.50×")
    assert label.get("text-anchor") == "end" and float(label.get("x")) < float(bar.get("x"))
