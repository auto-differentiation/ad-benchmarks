"""Concurrent runs of a bench test binary must not interfere.

Several ctest runs can share one machine and one TMPDIR (four build dirs
tested in parallel, or one build dir tested twice). The bench tests run a
bench program with --out <file> and read the CSV back; that file must belong
to the one test process: a second run must neither overwrite, truncate nor
delete it.

    concurrent_bench.py TEST_BINARY WORK_DIR TEST [TEST ...]
                        [--copies N] [--repeat R] [--stagger S]

For each TEST (a gtest name), starts N copies (default 3) of TEST_BINARY with
--gtest_filter=TEST --gtest_repeat=R (default 1), S seconds apart (default
0.5), all with TMPDIR=WORK_DIR/tmp (a fresh, shared directory). A test whose
bench run is slow overlaps the next copy with one repeat and a stagger; a
quick one is made to overlap by repeating it in every copy, started at once.
Fails unless every process exits 0; prints the output of the failing ones.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("work", type=pathlib.Path)
    ap.add_argument("tests", nargs="+")
    ap.add_argument("--copies", type=int, default=3)
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--stagger", type=float, default=0.5)
    a = ap.parse_args()

    tmp = a.work / "tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    env = dict(os.environ, TMPDIR=str(tmp))

    failed = 0
    for test in a.tests:
        procs = []
        for _ in range(a.copies):
            procs.append(subprocess.Popen([a.binary, "--gtest_filter=" + test,
                                           f"--gtest_repeat={a.repeat}"], env=env,
                                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                          text=True))
            time.sleep(a.stagger)
        for k, p in enumerate(procs):
            out, _ = p.communicate()
            if p.returncode != 0:
                failed += 1
                print(f"FAILED: {test} copy {k + 1} of {a.copies} (exit {p.returncode}), "
                      f"shared TMPDIR={tmp}")
                print(out)
            else:
                print(f"ok: {test} copy {k + 1} of {a.copies}")
    print(f"{failed} of {a.copies * len(a.tests)} concurrent runs failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
