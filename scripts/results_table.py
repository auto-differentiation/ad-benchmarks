#!/usr/bin/env python3
"""Combined benchmark CSVs (one per workload) -> the README results table.

Usage: results_table.py [--readme README.md [--check]] CSV...

Headers: the workload and what one evaluation computes. Cells: median time and its overhead over the plain double evaluation of the
same workload, "× plain" = time / double time (time only where the workload
has no plain row).
Without --readme the table goes to stdout; with it, the lines between
BEGIN and END in that file are replaced by the table; with --check as well,
nothing is written and the exit code is 1 if the block differs. Standard
library only.
"""

import argparse
import csv
import pathlib
import sys

LIBRARY_NAMES = {
    "plain": "double (no AD)",
    "xad-codegen": "XAD + codegen (commercial add-on)",
    "xad": "XAD",
    "xad-default": "XAD, default options",
    "adept": "Adept 2",
    "cppad": "CppAD",
    "autodiff": "autodiff",
    "fd": "finite differences",
}
WORKLOAD_NAMES = {"lmm": "LMM", "heston": "Heston", "cva": "CVA", "sabr": "SABR"}
# The published sizes (the CSV size column: LMM forwards, Heston and CVA paths, SABR quotes); the input
# counts and headers below hold only there. CVA's --size small keeps the path column, so it cannot be told
# apart when run with --paths 500.
PUBLISHED_SIZE = {"lmm": "80", "heston": "10000", "cva": "500", "sabr": "100"}
# Differentiated inputs per workload at the published size (SABR: parameters of its 100 x 15 Jacobian).
WORKLOAD_INPUTS = {"lmm": 159, "heston": 7, "cva": 39, "sabr": 15}
# What one timed evaluation computes (the inputs listed in the README's workload table).
WORKLOAD_COMPUTES = {
    "lmm": "price + 159 sensitivities",
    "heston": "book value + 7 sensitivities",
    "cva": "CVA + 39 sensitivities",
    "sabr": "100 residuals + 1,500 sensitivities",
}
REFERENCE = "plain"
BEGIN = "<!-- results:begin -->"
END = "<!-- results:end -->"


def _ordered(values):
    return list(dict.fromkeys(values))


def _time(seconds):
    """Three significant digits in the largest unit the rounded value reaches (whole seconds above 999)."""
    if seconds >= 999.5:
        return f"{seconds:.0f} s"
    for unit, scale in (("s", 1.0), ("ms", 1e-3), ("µs", 1e-6)):
        if float(f"{seconds / scale:.3g}") >= 1:
            break
    return f"{seconds / scale:#.3g}".rstrip(".") + f" {unit}"


def _times_plain(ratio):
    """Overhead over plain double: two decimals below 10x, else three significant digits."""
    if round(ratio, 2) < 10:
        return f"{ratio:.2f}×"
    return (f"{ratio:#.3g}".rstrip(".") if ratio < 999.5 else f"{ratio:.0f}") + "×"


def at_published_size(rows, workload):
    """True if every row of the workload has the published size, so WORKLOAD_INPUTS and WORKLOAD_COMPUTES hold."""
    sizes = {r.get("size") for r in rows if r["workload"] == workload}
    return workload in PUBLISHED_SIZE and sizes == {PUBLISHED_SIZE[workload]}


def _header(workload, rows):
    name = WORKLOAD_NAMES.get(workload, workload)
    return f"{name}<br>{WORKLOAD_COMPUTES[workload]}" if at_published_size(rows, workload) else name


def render_markdown(rows):
    workloads = _ordered(r["workload"] for r in rows)
    median = {(r["library"], r["workload"]): float(r["median_s"]) for r in rows}
    present = _ordered(r["library"] for r in rows)
    libraries = [l for l in LIBRARY_NAMES if l in present] + [l for l in present if l not in LIBRARY_NAMES]
    lines = ["| Library | " + " | ".join(_header(w, rows) for w in workloads) + " |",
             "|---|" + "---:|" * len(workloads)]
    for lib in libraries:
        cells = []
        for w in workloads:
            t = median.get((lib, w))
            ref = median.get((REFERENCE, w))
            cell = "n/a" if t is None else _time(t) + (f" ({_times_plain(t / ref)})" if ref else "")
            cells.append(cell)
        lines.append(f"| {LIBRARY_NAMES.get(lib, lib)} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def load_csvs(paths):
    rows = []
    for p in paths:
        with open(p, newline="") as f:
            rows.extend(csv.DictReader(f))
    return rows


def replace_block(text, table):
    head, begin, rest = text.partition(BEGIN + "\n")
    _, end, tail = rest.partition(END + "\n")
    if not begin or not end:
        raise ValueError(f"no {BEGIN} ... {END} block")
    return head + begin + table + end + tail


def main(argv=None):
    p = argparse.ArgumentParser(description="README results table from combined benchmark CSVs.")
    p.add_argument("--readme", type=pathlib.Path, help="file whose results block is replaced")
    p.add_argument("--check", action="store_true", help="only check the README block (exit 1 if stale)")
    p.add_argument("csv", nargs="+", help="combined benchmark CSVs, one per workload")
    a = p.parse_args(argv)
    table = render_markdown(load_csvs(a.csv))
    if a.readme is None:
        sys.stdout.write(table)
        return 0
    text = a.readme.read_text()
    new = replace_block(text, table)
    if a.check:
        if new != text:
            print(f"{a.readme}: results table differs from the CSVs; rerun without --check", file=sys.stderr)
            return 1
        return 0
    a.readme.write_text(new)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
