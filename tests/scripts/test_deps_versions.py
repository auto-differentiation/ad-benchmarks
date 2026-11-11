"""cmake/deps.cmake: the library labels in build/generated/versions.hpp.

Contract checked here:

  Every pinned library gets the label "<tag>@<short sha>" of its pin
  (adb_pin), which goes into versions.hpp and from there into every CSV row's
  library_version. When FetchContent's standard override
  -DFETCHCONTENT_SOURCE_DIR_ADB_<NAME>=<dir> replaces the pinned source, the
  label must not be the pin's: for a git checkout it is
  "<branch>@<short sha>" of <dir> (git rev-parse); for any other dir (e.g. a
  `git archive` copy) it is "local@<basename of dir>". An explicit
  -DADB_<NAME>_VERSION_LABEL=<text> wins over every automatic label.

The XAD pin is a parameter, set in one place: the cache variables
ADB_XAD_COMMIT (default the pinned 40-hex SHA), ADB_XAD_SHA256 (default the
pinned archive hash) and ADB_XAD_URL (default the GitHub archive URL of
ADB_XAD_COMMIT). Another commit with its archive and hash is downloaded,
checked and labelled "main@<short sha>"; a wrong hash fails the configure.

The project configured here is a minimal one that only includes
cmake/deps.cmake, with every library but XAD off and XAD overridden by a tiny
fake source tree, so nothing is compiled or downloaded from the network (the
pin parameter test fetches a local archive of the fake tree).
"""

import hashlib
import pathlib
import re
import shutil
import subprocess
import tarfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PINNED_XAD_SHORT = "aaf4e9c"
OFF = ["CPPAD", "ADEPT", "AUTODIFF"]

pytestmark = pytest.mark.skipif(not (shutil.which("cmake") and shutil.which("git")),
                                reason="needs cmake and git")


def git(cwd, *args):
    return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@example.com",
                           "-c", "init.defaultBranch=main", *args], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout.strip()


def fake_xad_tree(path):
    """A stand-in XAD source tree (no git): one INTERFACE target XAD::xad."""
    path.mkdir()
    (path / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(fake_xad LANGUAGES NONE)\n"
        "add_library(xad INTERFACE)\n"
        "add_library(XAD::xad ALIAS xad)\n")


def fake_xad(path):
    """A git checkout of a stand-in XAD (fake_xad_tree)."""
    fake_xad_tree(path)
    git(path, "init", "-q")
    git(path, "switch", "-q", "-c", "feature/faster-tape")
    git(path, "add", ".")
    git(path, "commit", "-q", "-m", "fake xad")
    return git(path, "rev-parse", "--short=7", "HEAD")


def configure(tmp_path, *defs):
    project = tmp_path / "project"
    project.mkdir()
    (project / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(adb_deps_test LANGUAGES NONE)\n"
        f'include("{ROOT / "cmake" / "deps.cmake"}")\n')
    build = tmp_path / "build"
    run = subprocess.run(["cmake", "-S", str(project), "-B", str(build),
                          f"-DFETCHCONTENT_BASE_DIR={tmp_path / 'deps'}",
                          "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
                          *[f"-DADB_WITH_{name}=OFF" for name in OFF], *defs],
                         capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    return (build / "generated" / "versions.hpp").read_text()


def label(header, name):
    m = re.search(rf'\b{name} = "([^"]*)";', header)
    assert m, header
    return m.group(1)


def test_overridden_xad_git_checkout_is_labelled_by_its_branch_and_commit(tmp_path):
    src = tmp_path / "xad-branch"
    short = fake_xad(src)
    assert short != PINNED_XAD_SHORT

    xad = label(configure(tmp_path, f"-DFETCHCONTENT_SOURCE_DIR_ADB_XAD={src}"), "xad")

    assert PINNED_XAD_SHORT not in xad
    assert xad == f"feature/faster-tape@{short}"


def test_overridden_xad_non_git_dir_is_labelled_local_and_by_its_dir_name(tmp_path):
    src = tmp_path / "xad-archive-1a2b3c4"
    fake_xad_tree(src)

    xad = label(configure(tmp_path, f"-DFETCHCONTENT_SOURCE_DIR_ADB_XAD={src}"), "xad")

    assert PINNED_XAD_SHORT not in xad
    assert xad == "local@xad-archive-1a2b3c4"


def test_overridden_xad_non_git_dir_inside_another_git_work_tree_is_labelled_local(tmp_path):
    outer = tmp_path / "outer"
    outer.mkdir()
    git(outer, "init", "-q")
    git(outer, "switch", "-q", "-c", "outer-branch")
    (outer / "README").write_text("outer repo\n")
    git(outer, "add", ".")
    git(outer, "commit", "-q", "-m", "outer")
    outer_short = git(outer, "rev-parse", "--short=7", "HEAD")
    src = outer / "results" / "xad-archive-5d6e7f8"
    src.parent.mkdir()
    fake_xad_tree(src)

    xad = label(configure(tmp_path, f"-DFETCHCONTENT_SOURCE_DIR_ADB_XAD={src}"), "xad")

    assert "outer-branch" not in xad
    assert outer_short not in xad
    assert xad == "local@xad-archive-5d6e7f8"


def test_explicit_version_label_wins_over_the_automatic_label(tmp_path):
    src = tmp_path / "xad-archive-1a2b3c4"
    fake_xad_tree(src)

    xad = label(configure(tmp_path, f"-DFETCHCONTENT_SOURCE_DIR_ADB_XAD={src}",
                          "-DADB_XAD_VERSION_LABEL=perf/tape-optimisations@0f32ec7"), "xad")

    assert xad == "perf/tape-optimisations@0f32ec7"


PINNED_XAD_COMMIT = "aaf4e9cad59cf1bfe9728b491f95f011db687f18"


def cache(build, name):
    m = re.search(rf"^{name}:[A-Z]+=(.*)$", (build / "CMakeCache.txt").read_text(), re.M)
    assert m, f"{name} not in the cache"
    return m.group(1)


def test_xad_pin_is_a_parameter_with_the_pinned_commit_as_default(tmp_path):
    # Default: the cache holds the pin (source overridden, so nothing is downloaded).
    src = tmp_path / "xad-src"
    fake_xad_tree(src)
    (tmp_path / "default").mkdir()
    configure(tmp_path / "default", f"-DFETCHCONTENT_SOURCE_DIR_ADB_XAD={src}")
    build = tmp_path / "default" / "build"
    assert cache(build, "ADB_XAD_COMMIT") == PINNED_XAD_COMMIT
    assert PINNED_XAD_COMMIT in cache(build, "ADB_XAD_URL")
    assert re.fullmatch(r"[0-9a-f]{64}", cache(build, "ADB_XAD_SHA256"))

    # Another commit: its archive (here a local one) is fetched, checked, labelled.
    commit = "0123456789abcdef0123456789abcdef01234567"
    archive = tmp_path / f"xad-{commit}.tar.gz"
    with tarfile.open(archive, "w:gz") as t:
        t.add(src, arcname=f"xad-{commit}")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    other = tmp_path / "other"
    other.mkdir()
    pins = [f"-DADB_XAD_COMMIT={commit}", f"-DADB_XAD_URL={archive.as_uri()}",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=OFF"]
    xad = label(configure(other, *pins, f"-DADB_XAD_SHA256={digest}"), "xad")
    assert xad == "main@0123456"

    # The pinned commit is labelled by its release tag.
    tagged = tmp_path / f"xad-{PINNED_XAD_COMMIT}.tar.gz"
    with tarfile.open(tagged, "w:gz") as t:
        t.add(src, arcname=f"xad-{PINNED_XAD_COMMIT}")
    release = tmp_path / "release"
    release.mkdir()
    xad = label(configure(release, f"-DADB_XAD_URL={tagged.as_uri()}", "-DFETCHCONTENT_FULLY_DISCONNECTED=OFF",
                          f"-DADB_XAD_SHA256={hashlib.sha256(tagged.read_bytes()).hexdigest()}"), "xad")
    assert xad == "v2.2.0@aaf4e9c"

    # A wrong hash fails the configure.
    bad = tmp_path / "bad"
    bad.mkdir()
    with pytest.raises(AssertionError):
        configure(bad, *pins, "-DADB_XAD_SHA256=" + "0" * 64)
