#!/usr/bin/env python3
"""One CSV per workload from the rounds of bench_all.sh.

Usage: combine_rounds.py OUT_DIR

Reads OUT_DIR/round_<k>/<w>.csv and writes OUT_DIR/<w>.csv: the same header,
one row per library in round_1's order (the first round present). Timings
(median_s, iqr_s, plain_median_s, ratio) are the median over rounds, memory
fields the maximum, every other field round_1's value. Standard library only.
"""

import csv
import pathlib
import re
import statistics
import sys

TIMING = ("median_s", "iqr_s", "plain_median_s", "ratio")
MEMORY = ("peak_memory_bytes", "peak_memory_growth_bytes", "peak_heap_reserved_bytes")


def _read(path):
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        return r.fieldnames, list(r)


def combine(rounds):
    """rounds: list of (header, rows) of one workload, first round first."""
    header, first = rounds[0]
    out = []
    for row in first:
        lib = row["library"]
        same = [r for _, rows in rounds for r in rows if r["library"] == lib]
        c = dict(row)
        for f in TIMING:
            c[f] = repr(statistics.median(float(r[f]) for r in same))
        for f in MEMORY:
            c[f] = str(max(int(r[f]) for r in same))
        out.append(c)
    return header, out


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print("usage: combine_rounds.py OUT_DIR", file=sys.stderr)
        return 2
    out = pathlib.Path(argv[0])
    dirs = sorted((d for d in out.glob("round_*") if re.fullmatch(r"round_\d+", d.name) and d.is_dir()),
                  key=lambda d: int(d.name[6:]))
    if not dirs:
        print(f"combine_rounds.py: no round_<k> directories in {out}", file=sys.stderr)
        return 1
    workloads = list(dict.fromkeys(p.stem for d in dirs for p in sorted(d.glob("*.csv"))))
    for w in workloads:
        rounds = [_read(d / f"{w}.csv") for d in dirs if (d / f"{w}.csv").is_file()]
        if any(h != rounds[0][0] for h, _ in rounds):
            print(f"combine_rounds.py: {w}: CSV headers differ between rounds", file=sys.stderr)
            return 1
        header, rows = combine(rounds)
        with open(out / f"{w}.csv", "w", newline="") as f:
            wr = csv.DictWriter(f, fieldnames=header, lineterminator="\n")
            wr.writeheader()
            wr.writerows(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
