"""scripts/bench_all.sh: the timing run of bench.yml, interleaved over rounds.

Contract checked here:

  scripts/bench_all.sh BUILD_DIR OUT_DIR ROUNDS
      runs ROUNDS rounds; round k (1-based) runs the four bench programs one
      after the other, one process at a time, starting at workload
      (k - 1) mod 4 of lmm, heston, cva, sabr and wrapping round, so no
      workload always runs first:
        BUILD_DIR/workloads/lmm/bench_lmm, BUILD_DIR/workloads/heston/bench_heston,
        BUILD_DIR/workloads/cva/bench_cva, BUILD_DIR/workloads/sabr/bench_sabr;
      each as `<program> $BENCH_<W>_ARGS --out OUT_DIR/round_<k>/<w>.csv`
      (BENCH_LMM_ARGS, BENCH_HESTON_ARGS, BENCH_CVA_ARGS, BENCH_SABR_ARGS:
      optional, split on whitespace), pinned to one CPU (taskset -c), so the
      program sees exactly one allowed CPU; creates OUT_DIR/round_<k>.
      Stops with a non-zero exit as soon as a program fails.
  BENCH_XAD_DEFAULT_BUILD=<dir> (optional; a build with XAD at its default
      options): right after each main program, <dir>'s program of the same
      workload runs `--column xad` with the same args on the same CPU; its
      xad row joins OUT_DIR/round_<k>/<w>.csv as library "xad-default",
      library_version "<version> (default options)", plain_median_s the main
      run's plain row and ratio = median_s / plain_median_s.
  ADB_GRAD_OUT never reaches a program: writing gradients would be timed.
The test runs it on fake programs that log their arguments and allowed CPUs.
"""

import csv
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "bench_all.sh"
PROGRAMS = {"lmm": "workloads/lmm/bench_lmm", "heston": "workloads/heston/bench_heston",
            "cva": "workloads/cva/bench_cva", "sabr": "workloads/sabr/bench_sabr"}

FAKE = """#!/bin/sh
cpus=$(grep '^Cpus_allowed_list' /proc/self/status | cut -f2)
echo "{w} $cpus $*" >> "{log}"
echo "grad_out=$ADB_GRAD_OUT" >> "{log}.env"
out=""
while [ $# -gt 0 ]; do [ "$1" = --out ] && out=$2; shift; done
[ -n "{fail}" ] && exit 3
echo "library,workload" > "$out"
"""


def make_build(tmp_path, fail=""):
    build, log = tmp_path / "build", tmp_path / "calls.log"
    for w, rel in PROGRAMS.items():
        p = build / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(FAKE.format(w=w, log=log, fail="1" if w == fail else ""))
        p.chmod(0o755)
    return build, log


def run(build, out, rounds, **env):
    e = dict(os.environ, **env)
    return subprocess.run(["bash", str(SCRIPT), str(build), str(out), str(rounds)],
                          capture_output=True, text=True, env=e)


def test_rounds_rotate_the_workload_order_on_one_cpu(tmp_path):
    build, log = make_build(tmp_path)
    out = tmp_path / "out"
    r = run(build, out, 5, BENCH_SABR_ARGS="--warmup 2  --iterations 7", BENCH_LMM_ARGS="--iters 9")
    assert r.returncode == 0, r.stderr

    calls = [l.split(" ", 2) for l in log.read_text().splitlines()]
    order = ["lmm", "heston", "cva", "sabr"]
    expected = [(k, order[(k - 1 + i) % 4]) for k in range(1, 6) for i in range(4)]
    assert [c[0] for c in calls] == [w for _, w in expected]
    extra = {"lmm": "--iters 9 ", "heston": "", "cva": "", "sabr": "--warmup 2 --iterations 7 "}
    for (k, w), (name, cpus, args) in zip(expected, calls):
        assert "," not in cpus and "-" not in cpus, f"{w} not pinned to one CPU: {cpus}"
        assert args == f"{extra[w]}--out {out}/round_{k}/{w}.csv"
        assert (out / f"round_{k}" / f"{w}.csv").is_file()


def test_grad_out_never_reaches_a_program(tmp_path):
    build, log = make_build(tmp_path)
    r = run(build, tmp_path / "out", 1, ADB_GRAD_OUT=str(tmp_path / "gradient.txt"))
    assert r.returncode == 0, r.stderr
    assert pathlib.Path(f"{log}.env").read_text().splitlines() == ["grad_out="] * 4


def test_a_failing_program_stops_the_run(tmp_path):
    build, log = make_build(tmp_path, fail="heston")
    r = run(build, tmp_path / "out", 3)
    assert r.returncode != 0
    assert [l.split(" ")[0] for l in log.read_text().splitlines()] == ["lmm", "heston"]


# A fake bench program writing real rows: plain (median 1.0) and xad
# (median 3.0) for a full run; for `--column xad` only the xad row (median
# MEDIAN), with plain_median_s the --plain-median value if given, else its
# own plain (2.0), and ratio = median_s / plain_median_s.
FAKE_ROWS = """#!/bin/sh
cpus=$(grep '^Cpus_allowed_list' /proc/self/status | cut -f2)
echo "{w} $cpus $*" >> "{log}"
out="" column="" pm=""
while [ $# -gt 0 ]; do
  case "$1" in --out) out=$2;; --column) column=$2;; --plain-median) pm=$2;; esac; shift
done
h="library,workload,median_s,plain_median_s,ratio,library_version"
if [ -z "$column" ]; then
  printf '%s\\n%s\\n%s\\n' "$h" "plain,{w},1.0,1.0,1.0,n/a" "xad,{w},3.0,1.0,3.0,main@abc1234" > "$out"
else
  [ -n "$pm" ] || pm=2.0
  printf '%s\\n%s\\n' "$h" "$column,{w},{median},$pm,$(awk "BEGIN {{ print {median} / $pm }}"),main@abc1234" > "$out"
fi
"""


def make_row_build(tmp_path, name, median, log):
    build = tmp_path / name
    for w, rel in PROGRAMS.items():
        p = build / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(FAKE_ROWS.format(w=w, log=log, median=median))
        p.chmod(0o755)
    return build


def test_xad_default_build_adds_a_labelled_xad_default_row_on_the_same_baseline(tmp_path):
    # BENCH_XAD_DEFAULT_BUILD=<dir>: in every round, each workload's program
    # of <dir> (XAD at its default options) runs `--column xad` with the
    # workload's args, right after the main program, on the same CPU; its xad
    # row is added to round_<k>/<w>.csv as library "xad-default",
    # library_version "<its version> (default options)", plain_median_s the
    # main run's plain and ratio = median_s / that plain. The main rows stay.
    log = tmp_path / "calls.log"
    build = make_row_build(tmp_path, "build", 3.0, log)
    default = make_row_build(tmp_path, "build-xad-default", 4.0, log)
    out = tmp_path / "out"
    r = run(build, out, 2, BENCH_XAD_DEFAULT_BUILD=str(default), BENCH_CVA_ARGS="--size small")
    assert r.returncode == 0, r.stderr

    order = ["lmm", "heston", "cva", "sabr"]
    calls = [l.split(" ", 2) for l in log.read_text().splitlines()]
    assert [c[0] for c in calls] == [order[(k - 1 + i) % 4] for k in (1, 2) for i in range(4) for _ in (0, 1)]
    for main, extra in zip(calls[0::2], calls[1::2]):
        assert "--column" not in main[2].split(), main
        words = extra[2].split()
        assert "--column" in words and words[words.index("--column") + 1] == "xad", extra
        assert extra[1] == main[1] and "," not in extra[1] and "-" not in extra[1], (main, extra)
        if extra[0] == "cva":
            assert "--size small" in extra[2], extra

    for k in (1, 2):
        for w in order:
            with open(out / f"round_{k}" / f"{w}.csv", newline="") as f:
                rows = {row["library"]: row for row in csv.DictReader(f)}
            assert set(rows) == {"plain", "xad", "xad-default"}, (k, w, rows)
            assert rows["xad"]["library_version"] == "main@abc1234"
            d = rows["xad-default"]
            assert d["workload"] == w
            assert d["library_version"] == "main@abc1234 (default options)"
            assert float(d["median_s"]) == 4.0
            assert float(d["plain_median_s"]) == 1.0
            assert abs(float(d["ratio"]) - 4.0) < 1e-9
