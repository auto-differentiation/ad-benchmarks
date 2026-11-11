""".github/workflows/ci.yml: build and correctness tests on every push and PR, no timings.

Contract checked here (needs actionlint and PyYAML; these tests are ctest test
workflows_pytest, label lint, registered only where both are installed):
  - actionlint reports nothing for the file;
  - triggered by push and pull_request;
  - every job runs on ubuntu-24.04;
  - one job configures with CMake, builds, and runs `ctest ... -L "unit|check"`
    (the slow label and every bench_* program stay out: no timings in CI);
  - it also runs `ctest ... -L lint --no-tests=error`, so CI fails when the
    lint tests were not registered;
  - PyYAML (python3-yaml) and actionlint are installed before the configure
    step; actionlint comes from a tagged release tarball
    (releases/download/vX.Y.Z/) whose SHA-256 is checked (sha256sum -c);
  - pytest is installed before the CMake configure step, so the scripts_pytest
    test (label unit) is registered;
  - the FetchContent downloads are cached: an actions/cache step whose path
    holds the directory the configure step passes as -DFETCHCONTENT_BASE_DIR=,
    keyed on hashFiles(...) of cmake/deps.cmake, placed before the configure.
"""

import pathlib
import re
import shutil
import subprocess

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
CI = ROOT / ".github" / "workflows" / "ci.yml"


def load(path):
    # PyYAML reads the bare key `on` as True.
    doc = yaml.safe_load(path.read_text())
    if True in doc:
        doc["on"] = doc.pop(True)
    return doc


def steps(doc):
    return [s for job in doc["jobs"].values() for s in job.get("steps", [])]


ACTIONLINT_RELEASE = re.compile(r"https://github\.com/rhysd/actionlint/releases/download/"
                                r"v(\d+\.\d+\.\d+)/actionlint_\1_linux_amd64\.tar\.gz")


def configures(run):  # a `cmake ... -B/-S ...` line that is not `cmake --build`
    return any(re.search(r"\bcmake\b", l) and re.search(r"\s-[BS]", l) and "--build" not in l
               for l in run.replace("\\\n", " ").splitlines())


def is_lint_run(run):
    return re.search(r"""\bctest\b.*-L\s+["']?lint["']?(\s|$)""", run) is not None


def test_ci_runs_the_lint_tests_and_fails_without_them():
    runs = [st.get("run", "") for st in steps(load(CI))]
    lint = [r for r in runs if is_lint_run(r)]
    assert lint, "no ctest -L lint step"
    assert all("--no-tests=error" in r for r in lint), lint


def test_ci_installs_the_lint_tools_pinned_before_configure():
    runs = [st.get("run", "") for st in steps(load(CI))]
    cfg = next(i for i, r in enumerate(runs) if configures(r))
    before = "\n".join(runs[:cfg])
    assert "python3-yaml" in before, "PyYAML not installed before configure"
    assert ACTIONLINT_RELEASE.search(before), "actionlint not downloaded from a tagged release before configure"
    assert re.search(r"\b[0-9a-f]{64}\b", before) and "sha256sum -c" in before, "actionlint download not checked"


def test_ci_builds_and_runs_unit_and_check_tests_with_cached_downloads():
    assert shutil.which("actionlint"), "actionlint not installed"
    lint = subprocess.run(["actionlint", str(CI)], capture_output=True, text=True)
    assert lint.returncode == 0, lint.stdout + lint.stderr

    doc = load(CI)
    on = doc["on"]
    triggers = set(on) if isinstance(on, (dict, list)) else {on}
    assert {"push", "pull_request"} <= triggers
    for name, job in doc["jobs"].items():
        assert job["runs-on"] == "ubuntu-24.04", name

    s = steps(doc)
    runs = [(i, st.get("run", "")) for i, st in enumerate(s)]
    configure = [i for i, r in runs if configures(r)]
    assert configure, "no CMake configure step"
    cfg = configure[0]
    assert any(re.search(r"cmake\s+--build", r) for _, r in runs), "no build step"
    ctest = [r for _, r in runs if re.search(r"\bctest\b", r) and not is_lint_run(r)]
    assert ctest and all(re.search(r"""-L\s+["']unit\|check["']""", r) for r in ctest), ctest
    assert not any(re.search(r"\bbench_\w+", r) for _, r in runs), "CI must not run benchmarks"

    assert any("pytest" in r for i, r in runs if i < cfg), "pytest not installed before configure"

    m = re.search(r"-DFETCHCONTENT_BASE_DIR=(\S+)", s[cfg]["run"].replace("\\\n", " "))
    assert m, "configure step does not set FETCHCONTENT_BASE_DIR"
    base = m.group(1).strip("\"'")
    caches = [(i, st) for i, st in enumerate(s) if st.get("uses", "").startswith("actions/cache@")]
    assert caches, "no actions/cache step"
    ok = [i for i, st in caches
          if base in str(st["with"]["path"]).split()
          and "hashFiles(" in st["with"]["key"] and "cmake/deps.cmake" in st["with"]["key"]]
    assert ok and ok[0] < cfg, "FetchContent dir not cached (keyed on cmake/deps.cmake) before configure"

