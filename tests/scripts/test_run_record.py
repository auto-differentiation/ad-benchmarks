"""scripts/bench_all.sh: the run record, OUT_DIR/run-record.txt.

Contract checked here (README, "Publishing results"):

  Every run of scripts/bench_all.sh BUILD_DIR OUT_DIR ROUNDS ends by writing
  OUT_DIR/run-record.txt. Its first line is "ad-benchmarks run record"; its
  "suite" line names the suite's version (CITATION.cff `version`) and short
  commit, then whether the source tree matches the release: either
  "unmodified: source tree matches the release" or
  "MODIFIED: 1 file differs from the release" (N > 1: "MODIFIED: N files
  differ from the release") followed by the differing paths. The check works on a git checkout and on a download without .git.
  For every results CSV the run wrote it has a line with the CSV's path
  relative to OUT_DIR and its sha256; its last line,
  "record sha256 <hex>", is the sha256 of all bytes of the file before it.
  Its "libraries" line names each AD library with the label of the build
  (BUILD_DIR/generated/versions.hpp). Its "not in repo" line names the
  columns from outside the repo (an ADB_EXTRA_DIR column, the xad-default
  column of BENCH_XAD_DEFAULT_BUILD), or says "none".

A release tree here is HEAD plus the release step, in a scratch clone:
`python3 scripts/run_record.py --write-manifest` writes the release's
checksums, committed as the release commit. That commit is taken two ways: a
`git archive` export (no .git, as a release tarball) and a git clone. The
script runs from that tree, on fake bench programs, so nothing is compiled.
"""

import hashlib
import io
import os
import pathlib
import re
import subprocess
import sys
import tarfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PROGRAMS = {"lmm": "workloads/lmm/bench_lmm", "heston": "workloads/heston/bench_heston",
            "cva": "workloads/cva/bench_cva", "sabr": "workloads/sabr/bench_sabr"}

# A fake bench program: writes a plain and an xad row to its --out CSV; the
# plain row carries the CSV's path, so no two CSVs of a run are equal.
FAKE = """#!/bin/sh
out=""
while [ $# -gt 0 ]; do [ "$1" = --out ] && out=$2; shift; done
printf '%s\\n%s\\n%s\\n' "library,workload,median_s,plain_median_s,ratio,library_version" \\
  "plain,{w},1.0,1.0,1.0,$out" "xad,{w},3.0,1.0,3.0,v2.2.0@aaf4e9c" > "$out"
"""


def git(cwd, *args):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.com",
                           "-c", "core.hooksPath=/dev/null", *args], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout.strip()


def citation_version():
    m = re.search(r'^version:\s*"?([^"\n]+)"?\s*$', (ROOT / "CITATION.cff").read_text(), re.M)
    return m.group(1)


def release_tree(tmp_path, kind):
    """A release made from HEAD, as a `git archive` export or a git clone.

    Returns the tree and the release commit's short sha."""
    src = tmp_path / "release-src"
    git(tmp_path, "clone", "-q", str(ROOT), str(src))
    git(src, "checkout", "-q", git(ROOT, "rev-parse", "HEAD"))
    subprocess.run([sys.executable, "scripts/run_record.py", "--write-manifest"], cwd=src,
                   check=True, capture_output=True, text=True)
    git(src, "add", "-A")
    git(src, "commit", "-q", "--allow-empty", "-m", "release")
    commit = git(src, "rev-parse", "HEAD")

    tree = tmp_path / "ad-benchmarks"
    if kind == "archive":
        data = subprocess.run(["git", "archive", "--format=tar", commit], cwd=src,
                              check=True, capture_output=True).stdout
        tree.mkdir()
        with tarfile.open(fileobj=io.BytesIO(data)) as tar:
            tar.extractall(tree, filter="tar")
    else:
        git(tmp_path, "clone", "-q", "--no-checkout", str(src), str(tree))
        git(tree, "checkout", "-q", commit)
    return tree, commit[:7]


def make_build(tmp_path):
    build = tmp_path / "build"
    for w, rel in PROGRAMS.items():
        p = build / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(FAKE.format(w=w))
        p.chmod(0o755)
    return build


def run_suite(tree, build, out, rounds=1, **env):
    r = subprocess.run(["bash", str(tree / "scripts" / "bench_all.sh"), str(build), str(out), str(rounds)],
                       capture_output=True, text=True, env=dict(os.environ, **env))
    assert r.returncode == 0, r.stderr
    record = out / "run-record.txt"
    assert record.is_file(), f"no {record}; stderr: {r.stderr}"
    return record.read_text()


def suite_line(record):
    lines = [l for l in record.splitlines() if l.split()[:1] == ["suite"]]
    assert len(lines) == 1, record
    return lines[0]


@pytest.mark.parametrize("kind", ["archive", "clone"])
def test_a_release_tree_is_recorded_as_unmodified_with_its_version_and_commit(tmp_path, kind):
    tree, commit = release_tree(tmp_path, kind)
    build = make_build(tmp_path)

    record = run_suite(tree, build, tmp_path / "out")

    assert record.splitlines()[0] == "ad-benchmarks run record"
    line = suite_line(record)
    assert citation_version() in line, line
    assert re.search(rf"\b{commit}\b", line), line
    assert line.rstrip().endswith("unmodified: source tree matches the release"), line
    assert "MODIFIED" not in record, record


def modified_paths(record):
    """The paths listed after a MODIFIED suite line (comma or space separated)."""
    lines = record.splitlines()
    i = lines.index(suite_line(record))
    paths = []
    for l in lines[i + 1:]:
        if not l.startswith(" "):
            break
        paths += [p for p in re.split(r"[,\s]+", l) if p]
    return paths


@pytest.mark.parametrize("kind", ["archive", "clone"])
@pytest.mark.parametrize("change", ["edit", "add", "remove"])
def test_one_changed_source_file_is_recorded_as_modified_with_its_path(tmp_path, kind, change):
    tree, _ = release_tree(tmp_path, kind)
    if change == "edit":
        path = "workloads/heston/model.hpp"
        with open(tree / path, "a") as f:
            f.write("// local change\n")
    elif change == "remove":
        path = "harness/timer.hpp"
        (tree / path).unlink()
    else:
        path = "harness/extra_timing.hpp"
        (tree / path).write_text("#pragma once\n")
    build = make_build(tmp_path)

    record = run_suite(tree, build, tmp_path / "out")

    line = suite_line(record)
    assert line.rstrip().endswith("MODIFIED: 1 file differs from the release"), line
    assert "unmodified" not in line, line
    assert modified_paths(record) == [path], record


@pytest.mark.parametrize("kind", ["archive", "clone"])
def test_two_changed_source_files_are_counted_in_the_plural_with_both_paths(tmp_path, kind):
    tree, _ = release_tree(tmp_path, kind)
    with open(tree / "workloads/heston/model.hpp", "a") as f:
        f.write("// local change\n")
    (tree / "harness/extra_timing.hpp").write_text("#pragma once\n")
    build = make_build(tmp_path)

    record = run_suite(tree, build, tmp_path / "out")

    line = suite_line(record)
    assert line.rstrip().endswith("MODIFIED: 2 files differ from the release"), line
    assert sorted(modified_paths(record)) == ["harness/extra_timing.hpp", "workloads/heston/model.hpp"], record



@pytest.mark.parametrize("kind", ["archive", "clone"])
def test_edits_to_the_readme_and_citation_leave_the_tree_unmodified(tmp_path, kind):
    # README.md and CITATION.cff are not source: they build and run nothing.
    tree, _ = release_tree(tmp_path, kind)
    for doc in ("README.md", "CITATION.cff"):
        with open(tree / doc, "a") as f:
            f.write("\n# local note\n")
    build = make_build(tmp_path)

    record = run_suite(tree, build, tmp_path / "out")

    assert suite_line(record).rstrip().endswith("unmodified: source tree matches the release"), record


def test_the_record_lists_the_sha256_of_every_results_csv_and_of_itself(tmp_path):
    # The suite writes one CSV per workload and round, OUT_DIR/round_<k>/<w>.csv
    # (not one results.csv, as the README mock-up shows). Each gets a line
    # with its path relative to OUT_DIR and its sha256 (64 hex digits). The
    # last line, "record ... sha256 <hex>", is the sha256 of every byte of
    # the file before that line.
    tree, _ = release_tree(tmp_path, "archive")
    build = make_build(tmp_path)
    out = tmp_path / "out"

    record = run_suite(tree, build, out, rounds=2)

    on_disk = {p.relative_to(out).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
               for p in out.rglob("*.csv")}
    assert len(on_disk) == 8 and len(set(on_disk.values())) == 8, on_disk
    listed = {}
    for line in record.splitlines():
        m = re.search(r"(\S+\.csv)\b.*\bsha256\s+([0-9a-f]{64})\b", line)
        if m:
            assert m.group(1) not in listed, line
            listed[m.group(1)] = m.group(2)
    assert listed == on_disk

    raw = (out / "run-record.txt").read_bytes()
    body, last = raw.rstrip(b"\n").rsplit(b"\n", 1)
    m = re.fullmatch(rb"record\s+sha256\s+([0-9a-f]{64})", last.strip())
    assert m, last
    assert m.group(1).decode() == hashlib.sha256(body + b"\n").hexdigest()


# The AD libraries' labels of a fake build (generated/versions.hpp, as
# cmake/deps.cmake writes it from cmake/versions.hpp.in).
BUILD_VERSIONS = {"xad": "v2.2.0@aaf4e9c", "cppad": "20250000.2@1a2b3c4",
                  "adept": "v2.1.3@5d6e7f8", "autodiff": "v1.1.2@9a8b7c6"}


def make_configured_build(tmp_path, name="build", extra=None):
    """A fake build with generated/versions.hpp and a CMakeCache.txt; with
    extra=(column, version) it is configured with an ADB_EXTRA_DIR and its
    programs write that column's row too."""
    build = tmp_path / name
    for w, rel in PROGRAMS.items():
        p = build / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        text = FAKE.format(w=w)
        if extra:
            text += f'echo "{extra[0]},{w},2.0,1.0,2.0,{extra[1]}" >> "$out"\n'
        p.write_text(text)
        p.chmod(0o755)
    template = (ROOT / "cmake" / "versions.hpp.in").read_text()
    values = {f"ADB_{k.upper()}_VERSION": v for k, v in BUILD_VERSIONS.items()}
    values.update(ADB_EIGEN_VERSION="3.4.0@0f1e2d3", ADB_ARCH_FLAGS="-march=x86-64-v3",
                  ADB_CXX_FLAGS="-ffp-contract=off -march=x86-64-v3")
    (build / "generated").mkdir()
    (build / "generated" / "versions.hpp").write_text(
        re.sub(r"@(ADB_\w+)@", lambda m: values[m.group(1)], template))
    (build / "CMakeCache.txt").write_text(
        "CMAKE_BUILD_TYPE:STRING=Release\n"
        "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++\n"
        f"ADB_EXTRA_DIR:PATH={'/opt/' + extra[0] if extra else ''}\n")
    return build


def field(record, name):
    lines = [l for l in record.splitlines() if l.startswith(name + " ")]
    assert len(lines) == 1, record
    return lines[0][len(name):].strip()


def test_the_record_names_every_library_with_the_builds_version(tmp_path):
    tree, _ = release_tree(tmp_path, "archive")
    build = make_configured_build(tmp_path)

    record = run_suite(tree, build, tmp_path / "out")

    entries = [e.lower() for e in re.split(r",\s*", field(record, "libraries"))]
    for lib, version in BUILD_VERSIONS.items():
        assert any(lib in e and version.lower() in e for e in entries), (lib, version, record)



@pytest.mark.parametrize("outside", ["none", "extra column", "xad-default build"])
def test_not_in_repo_names_the_columns_from_outside_the_repo_or_none(tmp_path, outside):
    tree, _ = release_tree(tmp_path, "archive")
    env = {}
    if outside == "extra column":
        build = make_configured_build(tmp_path, extra=("cg-column", "cg 1.4"))
    else:
        build = make_configured_build(tmp_path)
    if outside == "xad-default build":
        env["BENCH_XAD_DEFAULT_BUILD"] = str(make_configured_build(tmp_path, "build-xad-default"))

    record = run_suite(tree, build, tmp_path / "out", **env)

    value = field(record, "not in repo")
    if outside == "none":
        assert value == "none", record
    elif outside == "extra column":
        assert "cg-column" in value and "cg 1.4" in value, record
    else:
        assert "xad-default" in value, record


@pytest.mark.parametrize("kind", ["archive", "clone"])
def test_a_release_built_and_run_in_place_is_still_unmodified(tmp_path, kind):
    # The README's Build and Run sections build into the tree (build/, the
    # preset's binaryDir; CI caches FetchContent downloads in .deps/) and run
    # `scripts/bench_all.sh build results/run 3` from its top: none of that
    # output is a change to the source, nor is an earlier run's results/ci.
    tree, _ = release_tree(tmp_path, kind)
    build = make_configured_build(tree)
    (build / "_deps" / "xad-src").mkdir(parents=True)
    (build / "_deps" / "xad-src" / "Tape.hpp").write_text("// fetched\n")
    (tree / ".deps" / "adept-subbuild").mkdir(parents=True)
    (tree / ".deps" / "adept-subbuild" / "CMakeLists.txt").write_text("# fetched\n")
    run_suite(tree, build, tree / "results" / "ci")

    r = subprocess.run(["bash", "scripts/bench_all.sh", "build", "results/run", "3"], cwd=tree,
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    record = (tree / "results" / "run" / "run-record.txt").read_text()

    assert suite_line(record).rstrip().endswith("unmodified: source tree matches the release"), record
