"""scripts/combine_rounds.py: one CSV per workload from the rounds of bench_all.sh.

Contract checked here:

  python3 scripts/combine_rounds.py OUT_DIR
      reads OUT_DIR/round_<k>/<w>.csv for every round directory and workload
      file present, and writes OUT_DIR/<w>.csv per workload: the same header,
      one row per library in round_1's row order, where
        median_s, iqr_s, plain_median_s, ratio = the median over rounds of
            that field (statistics.median, written with repr(float));
        peak_memory_bytes, peak_memory_growth_bytes, peak_heap_reserved_bytes
            = the maximum over rounds (integers);
        every other field (library_version, compiler, cpu, date, git_sha, ...)
            = round_1's value, quoted as needed (csv module).
"""

import csv
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "combine_rounds.py"
FIXTURES = pathlib.Path(__file__).resolve().parent / "fixtures"
TIMING = ("median_s", "iqr_s", "plain_median_s", "ratio")
MEMORY = ("peak_memory_bytes", "peak_memory_growth_bytes", "peak_heap_reserved_bytes")


def read(path):
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        return r.fieldnames, list(r)


def write(path, header, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=header, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def make_rounds(out):
    """Three rounds of the heston and sabr fixtures with scaled timings/memory."""
    scale = {1: (1.0, 1), 2: (3.0, 2), 3: (2.0, 1)}  # round -> (time factor, memory factor)
    for w in ("heston", "sabr"):
        header, rows = read(FIXTURES / f"{w}.csv")
        for k, (t, m) in scale.items():
            rk = []
            for r in rows:
                r = dict(r)
                for f in TIMING:
                    r[f] = repr(float(r[f]) * t * (1.0 if f != "ratio" else 1.0 + 0.1 * k))
                for f in MEMORY:
                    r[f] = str(int(r[f]) * m)
                r["date"] = f"2026-10-2{k}T00:00:00Z"
                rk.append(r)
            write(out / f"round_{k}" / f"{w}.csv", header, rk)
    return header


def test_rounds_combine_to_medians_and_peak_memory(tmp_path):
    header = make_rounds(tmp_path)
    r = subprocess.run([sys.executable, str(SCRIPT), str(tmp_path)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr

    for w in ("heston", "sabr"):
        got_header, got = read(tmp_path / f"{w}.csv")
        assert got_header == header
        _, base = read(FIXTURES / f"{w}.csv")
        assert [g["library"] for g in got] == [b["library"] for b in base]
        for g, b in zip(got, base):
            for f in TIMING:
                # time factors 1, 3, 2 -> median is round 3; the ratio is also
                # scaled by 1.1, 1.2, 1.3: 1.1, 3.6, 2.6 -> median is round 3 too
                want = float(b[f]) * 2.0 * (1.0 if f != "ratio" else 1.0 + 0.1 * 3)
                assert float(g[f]) == want, (w, g["library"], f)
            for f in MEMORY:
                assert g[f] == str(int(b[f]) * 2), (w, g["library"], f)
            for f in header:
                if f not in TIMING + MEMORY and f != "date":
                    assert g[f] == b[f], (w, g["library"], f)
            assert g["date"] == "2026-10-21T00:00:00Z"
