#!/usr/bin/env python3
"""The run record of a bench_all.sh run.

Usage: run_record.py BUILD_DIR OUT_DIR ROUNDS
       run_record.py --write-manifest

The first writes OUT_DIR/run-record.txt. Its "suite" line checks the source
tree against scripts/release.sha256, the checksums of the release, so it
works without .git: "MODIFIED" with the paths when a released file changed or
is missing, or a file was added at the top level or anywhere under a
top-level directory that holds released files; else "unmodified". Files added
under a new top-level directory (such as build/) are not seen. Without that
file the tree is "not a release". Covered are all files except results/,
tmp/, the docs and the CI files (EXCLUDED_*); tests/ and smoke/ count, as
their CMake code runs in the same configure as the benchmarks. The record
ends with the sha256 of every results CSV under OUT_DIR and of the record
itself (all bytes before its last line).

Release steps: set `version` in CITATION.cff, run
`run_record.py --write-manifest` on the clean release checkout, commit
scripts/release.sha256 and tag that commit. Do not rerun it between
releases: later changes must read as MODIFIED. git's export-subst
(.gitattributes) fills in scripts/commit, so any `git archive` or GitHub
tarball knows its commit; a checkout asks git.
Standard library only.
"""

import csv
import datetime
import hashlib
import os
import pathlib
import platform
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = "scripts/release.sha256"
COMMIT = "scripts/commit"
EXCLUDED_DIRS = ("results", "tmp", ".github", "__pycache__")
EXCLUDED_FILES = ("README.md", "LICENSE", "CITATION.cff", ".gitlab-ci.yml", ".gitignore", ".gitattributes", ".git",
                  MANIFEST, COMMIT)
LIBRARIES = {"xad": "XAD", "adept": "Adept", "cppad": "CppAD", "autodiff": "autodiff", "eigen": "Eigen"}
XAD_PIN = ("ADB_XAD_COMMIT", "ADB_XAD_SHA256", "ADB_XAD_URL")  # the libraries line names the version
IN_REPO = ("plain", "xad", "cppad", "adept", "autodiff", "fd")  # harness/builtin_columns.hpp


def _covered(path):
    return path not in EXCLUDED_FILES and not set(path.split("/")[:-1]) & set(EXCLUDED_DIRS)


def _sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _git(*args):
    """git's output in ROOT, "" when ROOT is not the top of a git checkout."""
    def run(*a):
        r = subprocess.run(["git", "-C", str(ROOT), *a], capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""
    return run(*args) if run("rev-parse", "--show-toplevel") == str(ROOT) else ""


def _version():
    m = re.search(r'^version:\s*"?([^"\n]+)"?\s*$', (ROOT / "CITATION.cff").read_text(), re.M)
    return m.group(1) if m else "unknown"


def write_manifest():
    paths = sorted(p for p in _git("ls-files", "-z").split("\0") if p and _covered(p))
    lines = [f"# release {_version()}"]
    lines += [f"{_sha256(ROOT / p)}  {p}" for p in paths]
    (ROOT / MANIFEST).write_text("\n".join(lines) + "\n")


def _tree_files(released):
    """Covered files at the top level and under the top-level directories of the released files."""
    tops = {p.split("/")[0] for p in released if "/" in p}
    files = [f for f in ROOT.iterdir() if f.is_file()]
    files += [f for d in tops for f in (ROOT / d).rglob("*") if f.is_file()]
    return {p for p in (f.relative_to(ROOT).as_posix() for f in files) if _covered(p)}


def _commit():
    recorded = (ROOT / COMMIT).read_text().strip() if (ROOT / COMMIT).is_file() else ""
    commit = recorded if re.fullmatch(r"[0-9a-f]{40}", recorded) else _git("rev-parse", "HEAD")
    return commit[:7] or "unknown"


def _suite():
    """(the suite line's value, the paths that differ from the release)."""
    manifest = ROOT / MANIFEST
    if not manifest.is_file():
        return f"{_version()} ({_commit()}), not a release: no release checksums", []
    header, entries = {}, {}
    for line in manifest.read_text().splitlines():
        if line.startswith("# "):
            key, _, value = line[2:].partition(" ")
            header[key] = value
        elif line:
            digest, path = line.split("  ", 1)
            entries[path] = digest
    differ = sorted(p for p, d in entries.items() if not (ROOT / p).is_file() or _sha256(ROOT / p) != d)
    differ += sorted(_tree_files(entries) - set(entries))
    files = "1 file differs" if len(differ) == 1 else f"{len(differ)} files differ"
    state = f"MODIFIED: {files} from the release" if differ else "unmodified: source tree matches the release"
    return f"{header.get('release', 'unknown')} ({_commit()}), {state}", differ


def _field(label, value):
    return f"{label:<13}{value}"


def _machine(csvs):
    """The CPU label the results' cpu column shares (harness/bench_env.hpp cpu_label), the vCPU count and
    the OS with its kernel's major.minor version only."""
    cpus = set()
    for path in csvs:
        with open(path, newline="") as f:
            cpus.update(row.get("cpu", "") for row in csv.DictReader(f))
    cpu = cpus.pop() if len(cpus) == 1 and "" not in cpus else "unknown CPU"
    m = re.match(r"\d+\.\d+", platform.release())
    version = m.group() if m else platform.release().split("-")[0]
    return f"{cpu}, {os.cpu_count()} vCPU, {platform.system()} {version}"


def _build_versions(build):
    """The strings of the build's generated/versions.hpp, None without it."""
    header = build / "generated" / "versions.hpp"
    return dict(re.findall(r'const char\* (\w+) = "([^"]*)";', header.read_text())) if header.is_file() else None


def _cmake_entries(build):
    """(name, type, value) of each CMakeCache.txt entry, None without the file."""
    cache = build / "CMakeCache.txt"
    return re.findall(r"^(\w+):(\w+)=(.*)$", cache.read_text(), re.M) if cache.is_file() else None


def _cmake_cache(build):
    return {name: value for name, _, value in _cmake_entries(build) or []}


def _options(build):
    """The XAD_* and ADB_* build options, without paths, internal entries and the XAD source pin."""
    entries = _cmake_entries(build)
    if entries is None:
        return "unknown (no CMakeCache.txt in the build)"
    options = sorted(f"{name}={value}" for name, kind, value in entries
                     if name.startswith(("XAD_", "ADB_")) and kind not in ("PATH", "FILEPATH", "INTERNAL", "STATIC")
                     and name not in XAD_PIN)
    return ", ".join(options) or "none"


def _compiler(build):
    """Compiler, its flags for the build type plus the suite's (ADB_CXX_FLAGS), and LTO."""
    found = {}
    for f in build.glob("CMakeFiles/*/CMakeCXXCompiler.cmake"):
        found.update(re.findall(r'^set\(CMAKE_CXX_COMPILER_(ID|VERSION) "([^"]*)"\)', f.read_text(), re.M))
    cache = _cmake_cache(build)
    build_type = cache.get("CMAKE_BUILD_TYPE", "").upper()
    flags = [cache.get("CMAKE_CXX_FLAGS", ""), cache.get(f"CMAKE_CXX_FLAGS_{build_type}", ""),
             (_build_versions(build) or {}).get("cxx_flags", "")]
    parts = [f"{found.get('ID', 'unknown')} {found.get('VERSION', '')}".strip(), " ".join(f for f in flags if f)]
    if cache.get("CMAKE_INTERPROCEDURAL_OPTIMIZATION", "").upper() in ("ON", "TRUE", "1"):
        parts.append("LTO")
    return ", ".join(p for p in parts if p)


def _libraries(build):
    """The AD libraries and their versions from the build's generated/versions.hpp."""
    found = _build_versions(build)
    if found is None:
        return "unknown (no generated/versions.hpp in the build)"
    return ", ".join(f"{name} {found[key]}" for key, name in LIBRARIES.items() if key in found)


def _not_in_repo(csvs):
    """Columns of the results that the repo does not build: ADB_EXTRA_DIR columns and xad-default."""
    found = {}
    for path in csvs:
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                if row.get("library") and row["library"] not in IN_REPO:
                    found.setdefault(row["library"], row.get("library_version", ""))
    return ", ".join(f"{lib} {version}".strip() for lib, version in found.items()) or "none"


def _natural(path):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", path)]


def record(build, out, rounds):
    """The record's text. Its last line is the sha256 of all bytes before it."""
    suite, differ = _suite()
    lines = ["ad-benchmarks run record", _field("suite", suite)]
    if differ:
        lines.append(_field("", ", ".join(differ)))
    lines.append(_field("date", datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")))
    csvs = sorted((p.relative_to(out).as_posix() for p in out.rglob("*.csv")), key=_natural)
    lines.append(_field("machine", _machine(out / c for c in csvs)))
    lines.append(_field("compiler", _compiler(build)))
    lines.append(_field("options", _options(build)))
    lines.append(_field("threads", "1 (each program pinned to one CPU)"))
    lines.append(_field("libraries", _libraries(build)))
    lines.append(_field("not in repo", _not_in_repo(out / c for c in csvs)))
    # bench_all.sh runs no gradient check: the programs only time (ADB_GRAD_OUT is unset)
    lines.append(_field("method", f"{rounds} round{'' if rounds == '1' else 's'}, rotated workload order, median; "
                                  "gradient checks: not run by bench_all.sh (ctest -L check)"))
    lines += [_field("results" if i == 0 else "", f"{c}  sha256 {_sha256(out / c)}") for i, c in enumerate(csvs)]
    text = "\n".join(lines) + "\n"
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    return text + _field("record", f"sha256 {digest}") + "\n"


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv == ["--write-manifest"]:
        write_manifest()
        return 0
    if len(argv) != 3:
        print("usage: run_record.py BUILD_DIR OUT_DIR ROUNDS | --write-manifest", file=sys.stderr)
        return 2
    build, out = pathlib.Path(argv[0]), pathlib.Path(argv[1])
    (out / "run-record.txt").write_text(record(build, out, argv[2]), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
