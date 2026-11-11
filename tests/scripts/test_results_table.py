"""scripts/results_table.py: combined CSVs -> the README results table.

Contract checked here (standard library only):

  results_table.render_markdown(rows) -> str
      one table `| Library | LMM<br>price + 159 sensitivities | ... |` (workloads
      in order of first appearance, each with what one evaluation computes), one row per library; each cell is the median time
      with three significant digits in s, ms or µs, then its overhead over
      the plain double evaluation of that workload (median / the "plain"
      row's median; two decimals below 10×, else three significant digits),
      e.g. "313 ms (3.85×)", "21.3 ms (10.3×)"; the plain row
      itself shows "(1.00×)"; the time alone where the workload has no plain
      row; "n/a" where a library has no row. No markers or footnotes, whatever
      the library_version says.
  python3 scripts/results_table.py --readme README.md CSV...
      replaces the lines between `<!-- results:begin -->` and
      `<!-- results:end -->` in README.md with render_markdown of the CSVs.
  python3 scripts/results_table.py --readme README.md --check CSV...
      changes nothing; exit 1 with a message on stderr if the block differs.
  The repository's README.md holds the table of results/amd/<workload>.csv.
"""

import csv
import importlib.util
import pathlib
import subprocess
import sys

SCRIPT = pathlib.Path(__file__).resolve().parents[2] / "scripts" / "results_table.py"
FIELDS = ["library", "workload", "median_s", "library_version", "size"]
PUBLISHED_SIZE = {"lmm": "80", "heston": "10000", "cva": "500", "sabr": "100"}


def _module():
    spec = importlib.util.spec_from_file_location("results_table", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _row(library, workload, median_s, version="v1", size=None):
    size = PUBLISHED_SIZE.get(workload, "1") if size is None else size
    return dict(zip(FIELDS, [library, workload, repr(median_s), version, size]))


def test_cells_show_median_with_unit_and_overhead_over_plain():
    rows = [_row("plain", "lmm", 0.0814254), _row("xad", "lmm", 0.3131888),
            _row("xad-codegen", "lmm", 0.0731726),
            _row("plain", "sabr", 1.46e-06), _row("xad", "sabr", 5.33e-06)]
    table = _module().render_markdown(rows)
    assert table.splitlines() == [
        "| Library | LMM<br>price + 159 sensitivities | SABR<br>100 residuals + 1,500 sensitivities |",
        "|---|---:|---:|",
        "| double (no AD) | 81.4 ms (1.00×) | 1.46 µs (1.00×) |",
        "| XAD + codegen (commercial add-on) | 73.2 ms (0.90×) | n/a |",
        "| XAD | 313 ms (3.85×) | 5.33 µs (3.65×) |",
    ]


def test_restructured_cell_has_no_marker_or_footnote():
    rows = [_row("plain", "sabr", 1.46e-06),
            _row("xad-codegen", "sabr", 1.33e-06, "commercial add-on, restructured")]
    lines = _module().render_markdown(rows).splitlines()
    assert lines[-1] == "| XAD + codegen (commercial add-on) | 1.33 µs (0.91×) |"


def _csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    return str(path)


def _cli(*args):
    return subprocess.run([sys.executable, str(SCRIPT), *args], capture_output=True, text=True)


def test_readme_block_is_replaced_by_the_table(tmp_path):
    rows = [_row("adept", "lmm", 0.4)]
    csv_path = _csv(tmp_path / "lmm.csv", rows)
    readme = tmp_path / "README.md"
    readme.write_text("intro\n<!-- results:begin -->\nold\n<!-- results:end -->\nafter\n")
    assert _cli("--readme", str(readme), csv_path).returncode == 0
    assert readme.read_text() == ("intro\n<!-- results:begin -->\n" + _module().render_markdown(rows)
                                  + "<!-- results:end -->\nafter\n")


def test_check_fails_on_a_stale_block_and_leaves_it(tmp_path):
    csv_path = _csv(tmp_path / "lmm.csv", [_row("adept", "lmm", 0.4)])
    readme = tmp_path / "README.md"
    stale = "<!-- results:begin -->\nold\n<!-- results:end -->\n"
    readme.write_text(stale)
    r = _cli("--readme", str(readme), "--check", csv_path)
    assert r.returncode == 1 and "results table" in r.stderr
    assert readme.read_text() == stale
    assert _cli("--readme", str(readme), csv_path).returncode == 0
    assert _cli("--readme", str(readme), "--check", csv_path).returncode == 0


def test_repository_readme_matches_the_published_results():
    root = SCRIPT.parents[1]
    csvs = [str(root / "results" / "amd" / f"{w}.csv") for w in ("lmm", "heston", "cva", "sabr")]
    r = _cli("--readme", str(root / "README.md"), "--check", *csvs)
    assert r.returncode == 0, r.stderr


def test_cells_without_a_plain_row_show_the_time_only():
    table = _module().render_markdown([_row("xad", "lmm", 0.3131888), _row("fd", "lmm", 25.5)])
    assert table.splitlines()[2:] == ["| XAD | 313 ms |", "| finite differences | 25.5 s |"]


def test_times_that_round_up_to_the_next_unit_use_it():
    mod = _module()
    assert [mod._time(t) for t in (0.9996, 999.6e-6, 1234.0)] == ["1.00 s", "1.00 ms", "1234 s"]


def test_overheads_keep_two_decimals_below_ten_and_three_significant_digits_above():
    mod = _module()
    assert [mod._times_plain(r) for r in (0.8986, 1.0, 9.994, 10.26, 34.0, 73.04, 99.96, 352.94)] == [
        "0.90×", "1.00×", "9.99×", "10.3×", "34.0×", "73.0×", "100×", "353×"]


def test_published_results_show_only_the_xad_options_used():
    root = SCRIPT.parents[1]
    published = root / "results" / "amd"
    for path in [root / "README.md", *published.glob("speedup-*.svg")]:
        text = path.read_text()
        assert "XAD, default options" not in text and "xad-default" not in text, path
    for path in published.glob("**/*.csv"):
        with open(path, newline="") as f:
            assert all(r["library"] != "xad-default" for r in csv.DictReader(f)), path


def test_published_label_of_the_add_on_is_xad_plus_codegen():
    root = SCRIPT.parents[1]
    for path in [root / "README.md", *(root / "results" / "amd").glob("speedup-*.svg")]:
        text = path.read_text()
        assert "XAD + codegen (commercial add-on)" in text and "XAD + xad-codegen" not in text, path


def test_each_workload_header_says_what_one_evaluation_computes():
    rows = [_row("plain", w, 1.0) for w in ("lmm", "heston", "cva", "sabr")]
    header = _module().render_markdown(rows).splitlines()[0]
    assert header == ("| Library | LMM<br>price + 159 sensitivities | Heston<br>book value + 7 sensitivities "
                      "| CVA<br>CVA + 39 sensitivities | SABR<br>100 residuals + 1,500 sensitivities |")


def test_published_xad_rows_name_the_release_and_options():
    published = SCRIPT.parents[1] / "results" / "amd"
    for path in published.glob("**/*.csv"):
        with open(path, newline="") as f:
            versions = {r["library_version"] for r in csv.DictReader(f) if r["library"] == "xad"}
        assert versions == {"v2.2.0, SI+NTL+LS, LTO"}, path


def test_a_workload_run_at_another_size_gets_no_sensitivity_count_in_its_header():
    rows = [_row("plain", "lmm", 1.0, size="16"), _row("xad", "lmm", 2.0, size="16"), _row("plain", "sabr", 1.0)]
    header = _module().render_markdown(rows).splitlines()[0]
    assert header == "| Library | LMM | SABR<br>100 residuals + 1,500 sensitivities |"
