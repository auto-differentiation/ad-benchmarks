"""scripts/run_record.py on its own, outside bench_all.sh: the cases the
acceptance tests in test_run_record.py leave open."""

import datetime
import importlib.util
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def suite_line(tree, tmp_path):
    out = tmp_path / "out"
    out.mkdir()
    subprocess.run([sys.executable, str(tree / "scripts" / "run_record.py"), str(tmp_path / "build"), str(out), "1"],
                   check=True)
    return next(l for l in (out / "run-record.txt").read_text().splitlines() if l.startswith("suite "))


def test_a_tree_without_release_checksums_is_recorded_as_not_a_release(tmp_path):
    tree = tmp_path / "tree"
    (tree / "scripts").mkdir(parents=True)
    shutil.copy(ROOT / "scripts" / "run_record.py", tree / "scripts")
    (tree / "CITATION.cff").write_text('cff-version: 1.2.0\nversion: "9.8"\n')

    line = suite_line(tree, tmp_path)

    assert line.split(None, 1)[1] == "9.8 (unknown), not a release: no release checksums"


def test_a_git_archive_of_any_commit_knows_its_commit(tmp_path):
    tree = tmp_path / "tree"
    tree.mkdir()
    archive = subprocess.run(["git", "archive", "HEAD"], cwd=ROOT, check=True, capture_output=True).stdout
    subprocess.run(["tar", "-x", "-C", str(tree)], input=archive, check=True)
    head = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT, check=True,
                          capture_output=True, text=True).stdout.strip()

    line = suite_line(tree, tmp_path)

    assert f"({head})," in line, line


def record_field(build, tmp_path, name):
    out = tmp_path / "out"
    out.mkdir()
    subprocess.run([sys.executable, str(ROOT / "scripts" / "run_record.py"), str(build), str(out), "3"], check=True)
    lines = [l for l in (out / "run-record.txt").read_text().splitlines() if l.startswith(name + " ")]
    assert len(lines) == 1, lines
    return lines[0][len(name):].strip()


def test_the_compiler_line_names_the_compiler_its_flags_and_lto(tmp_path):
    build = tmp_path / "build"
    (build / "CMakeFiles" / "3.28.3").mkdir(parents=True)
    (build / "CMakeFiles" / "3.28.3" / "CMakeCXXCompiler.cmake").write_text(
        'set(CMAKE_CXX_COMPILER "/usr/bin/g++")\nset(CMAKE_CXX_COMPILER_ID "GNU")\n'
        'set(CMAKE_CXX_COMPILER_VERSION "15.2.0")\n')
    (build / "CMakeCache.txt").write_text(
        "CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_CXX_FLAGS:STRING=\n"
        "CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG\nCMAKE_INTERPROCEDURAL_OPTIMIZATION:BOOL=ON\n")
    (build / "generated").mkdir()
    (build / "generated" / "versions.hpp").write_text(
        'inline constexpr const char* cxx_flags = "-ffp-contract=off -march=x86-64-v3";\n')

    assert record_field(build, tmp_path, "compiler") == "GNU 15.2.0, -O3 -DNDEBUG -ffp-contract=off -march=x86-64-v3, LTO"


def test_the_method_line_gives_the_rounds_order_statistic_and_where_gradients_are_checked(tmp_path):
    assert record_field(tmp_path / "build", tmp_path, "method") == (
        "3 rounds, rotated workload order, median; gradient checks: not run by bench_all.sh (ctest -L check)")


def run_record_module():
    spec = importlib.util.spec_from_file_location("run_record", ROOT / "scripts" / "run_record.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def machine_line(tmp_path, monkeypatch, cpus, release="7.0.0-1014-generic"):
    """The machine field of the record of a run whose CSVs hold the cpu values cpus, one list per CSV,
    on a 32 vCPU Linux machine with kernel release release."""
    out = tmp_path / "out"
    out.mkdir(exist_ok=True)
    for i, rows in enumerate(cpus):
        (out / f"w{i}.csv").write_text("library,cpu\n" + "".join(f"plain,{c}\n" for c in rows))
    mod = run_record_module()
    monkeypatch.setattr(mod.os, "cpu_count", lambda: 32)
    monkeypatch.setattr(mod.platform, "system", lambda: "Linux")
    monkeypatch.setattr(mod.platform, "release", lambda: release)
    line = next(l for l in mod.record(tmp_path / "build", out, "1").splitlines() if l.startswith("machine "))
    return line[len("machine"):].strip()


def test_the_machine_line_names_the_cpu_of_the_results_its_cpu_count_and_the_os_version(tmp_path, monkeypatch):
    value = machine_line(tmp_path, monkeypatch, [["AMD EPYC (Zen 5)"] * 2, ["AMD EPYC (Zen 5)"]])

    assert value == "AMD EPYC (Zen 5), 32 vCPU, Linux 7.0"


def test_the_date_line_is_the_time_of_the_run_in_utc(tmp_path):
    before = datetime.datetime.now(datetime.timezone.utc).replace(second=0, microsecond=0)

    value = record_field(tmp_path / "build", tmp_path, "date")

    when = datetime.datetime.strptime(value, "%Y-%m-%d %H:%M UTC").replace(tzinfo=datetime.timezone.utc)
    assert before <= when <= datetime.datetime.now(datetime.timezone.utc)


def test_the_threads_line_says_one_thread_pinned_to_one_cpu(tmp_path):
    assert record_field(tmp_path / "build", tmp_path, "threads") == "1 (each program pinned to one CPU)"


def test_the_method_line_says_round_for_a_single_round(tmp_path):
    build, out = tmp_path / "build", tmp_path / "out"
    out.mkdir()
    subprocess.run([sys.executable, str(ROOT / "scripts" / "run_record.py"), str(build), str(out), "1"], check=True)

    assert "method       1 round, rotated workload order" in (out / "run-record.txt").read_text()


def test_the_options_line_lists_the_xad_and_adb_build_options_but_no_paths_or_pins(tmp_path):
    build = tmp_path / "build"
    build.mkdir()
    (build / "CMakeCache.txt").write_text(
        "CMAKE_BUILD_TYPE:STRING=Release\nXAD_USE_STRONG_INLINE:BOOL=ON\nXAD_NO_THREADLOCAL:BOOL=ON\n"
        "XAD_POSITION_INDEPENDENT_CODE:BOOL=OFF\nADB_ARCH_FLAGS:STRING=-march=native\n"
        "ADB_EXTRA_DIR:PATH=/opt/col\nADB_CPPAD_LIB:FILEPATH=/x/libcppad.so\nXAD_SOME_STATE:INTERNAL=1\n"
        "XAD_USE_STRONG_INLINE-ADVANCED:INTERNAL=1\nADB_XAD_COMMIT:STRING=" + "a" * 40 + "\n"
        "ADB_XAD_SHA256:STRING=" + "b" * 64 + "\nADB_XAD_URL:STRING=https://example.org/xad.tar.gz\n")

    assert record_field(build, tmp_path, "options") == (
        "ADB_ARCH_FLAGS=-march=native, XAD_NO_THREADLOCAL=ON, XAD_POSITION_INDEPENDENT_CODE=OFF, "
        "XAD_USE_STRONG_INLINE=ON")


def test_the_machine_line_has_an_unknown_cpu_when_the_results_disagree_on_it(tmp_path, monkeypatch):
    value = machine_line(tmp_path, monkeypatch, [["AMD EPYC (Zen 5)"], ["AMD EPYC (Zen 5)", "Intel Xeon (Ice Lake)"]])

    assert value == "unknown CPU, 32 vCPU, Linux 7.0"


def test_the_machine_line_has_an_unknown_cpu_when_a_result_row_has_none(tmp_path, monkeypatch):
    value = machine_line(tmp_path, monkeypatch, [[""]])

    assert value == "unknown CPU, 32 vCPU, Linux 7.0"



def test_the_machine_line_has_an_unknown_cpu_when_a_result_has_no_cpu_column(tmp_path, monkeypatch):
    (tmp_path / "out").mkdir()
    (tmp_path / "out" / "lmm.csv").write_text("library,workload\nplain,lmm\n")

    value = machine_line(tmp_path, monkeypatch, [])

    assert value == "unknown CPU, 32 vCPU, Linux 7.0"


def test_the_machine_line_keeps_a_release_without_a_dotted_version_up_to_its_first_dash(tmp_path, monkeypatch):
    value = machine_line(tmp_path, monkeypatch, [["AMD EPYC (Zen 5)"]], release="7-rc1-custom")

    assert value == "AMD EPYC (Zen 5), 32 vCPU, Linux 7"
