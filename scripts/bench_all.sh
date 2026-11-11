#!/usr/bin/env bash
# Timing run of the four benchmarks, interleaved over rounds.
#
# Usage: bench_all.sh BUILD_DIR OUT_DIR ROUNDS
#
# Round k (1-based) runs lmm, heston, cva and sabr one after the other,
# starting at workload (k - 1) mod 4 and wrapping, so no workload always runs
# first. Each program runs pinned to one CPU (BENCH_CPU, default the first CPU
# this script may use) as
#   <program> $BENCH_<W>_ARGS --out OUT_DIR/round_<k>/<w>.csv
# with BENCH_LMM_ARGS, BENCH_HESTON_ARGS, BENCH_CVA_ARGS, BENCH_SABR_ARGS
# optional and split on whitespace.
#
# BENCH_XAD_DEFAULT_BUILD=<dir> (optional, a build with XAD at its default
# options): right after each program, <dir>'s program of the same workload
# runs `--column xad --plain-median <the main run's plain median>` with the
# same args on the same CPU, and its row joins the CSV as library
# "xad-default", library_version "<version> (default options)".
set -euo pipefail
unset ADB_GRAD_OUT  # writing gradients would be timed

if [ $# -ne 3 ]; then
    echo "usage: $0 BUILD_DIR OUT_DIR ROUNDS" >&2
    exit 2
fi
build=$1 out=$2 rounds=$3

workloads=(lmm heston cva sabr)
declare -A program=(
    [lmm]=workloads/lmm/bench_lmm
    [heston]=workloads/heston/bench_heston
    [cva]=workloads/cva/bench_cva
    [sabr]=workloads/sabr/bench_sabr
)
cpu=${BENCH_CPU:-$(taskset -cp $$ | sed 's/.*: //; s/[,-].*//')}

for ((k = 1; k <= rounds; k++)); do
    mkdir -p "$out/round_$k"
    for ((i = 0; i < 4; i++)); do
        w=${workloads[(k - 1 + i) % 4]}
        var="BENCH_${w^^}_ARGS"
        read -r -a args <<< "${!var:-}"
        csv="$out/round_$k/$w.csv"
        taskset -c "$cpu" "$build/${program[$w]}" "${args[@]}" --out "$csv"
        if [ -n "${BENCH_XAD_DEFAULT_BUILD:-}" ]; then
            extra="$out/round_$k/.$w.xad-default.csv"
            plain=$(python3 -c 'import csv, sys
print(next(r["plain_median_s"] for r in csv.DictReader(open(sys.argv[1], newline="")) if r["library"] == "plain"))' "$csv")
            taskset -c "$cpu" "$BENCH_XAD_DEFAULT_BUILD/${program[$w]}" "${args[@]}" \
                --column xad --plain-median "$plain" --out "$extra"
            python3 -c 'import csv, sys
main, extra = sys.argv[1:]
with open(main, newline="") as f:
    fields = csv.DictReader(f).fieldnames
with open(extra, newline="") as f:
    rows = [r for r in csv.DictReader(f) if r["library"] == "xad"]
with open(main, "a", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
    for r in rows:
        r["library"] = "xad-default"
        r["library_version"] += " (default options)"
        w.writerow({k: r.get(k, "") for k in fields})' "$csv" "$extra"
            rm -f "$extra"
        fi
    done
done
