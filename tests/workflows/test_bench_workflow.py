""".github/workflows/bench.yml: timing run on a GitHub-hosted runner, manual dispatch only.

Contract checked here (needs actionlint and PyYAML, label lint like
test_ci_workflow.py):
  - actionlint reports nothing for the file;
  - triggered only by workflow_dispatch, with an input `rounds`;
  - exactly one job, no matrix, on ubuntu-24.04 (one runner type for all
    columns);
  - steps in this order: a Release CMake configure (-DCMAKE_BUILD_TYPE=Release)
    and `cmake --build`; `scripts/bench_all.sh <build> <out> <rounds>` with
    <rounds> taken from the `rounds` input, BENCH_LMM_ARGS, BENCH_HESTON_ARGS,
    BENCH_CVA_ARGS and BENCH_SABR_ARGS set (iteration counts) and ADB_GIT_SHA
    set from github.sha (env of the step, job or workflow); then
    `scripts/combine_rounds.py <out>`; then `scripts/chart.py` with --markdown
    and --svg; then actions/upload-artifact with a path holding <out>;
  - the runner CPU is recorded: some run step writes `lscpu` output into <out>.
  - inputs xad_commit and xad_sha256 (strings, default ""): the configure
    passes -DADB_XAD_COMMIT / -DADB_XAD_SHA256 (cmake/deps.cmake) only when
    set, so the default run benchmarks the deps.cmake pin; the dependency
    cache key includes the xad_commit input. Checked by running the
    configure step's script with a stub cmake (configure_args).
  - the configure builds XAD with strong inlining and no thread-local tape
    (the published run also had linear slots on and PIC off; the workflow
    does not pass them): -DXAD_USE_STRONG_INLINE=ON -DXAD_NO_THREADLOCAL=ON
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON.
  - input native (boolean, default false): when true the configure adds
    -DADB_ARCH_FLAGS=-march=native, else no ADB_ARCH_FLAGS (the portable
    x86-64-v3 default); the dependency cache key includes inputs.native.
  - a second configure step (its own build dir, a step of its own) for the
    "xad-default" reference row: the main configure's arguments for any
    inputs, minus XAD_USE_STRONG_INLINE / XAD_NO_THREADLOCAL (XAD's defaults;
    IPO, arch flags and pin as in the main build), optionally with other
    libraries off (-DADB_WITH_<X>=OFF); built before the benchmark step,
    which sets BENCH_XAD_DEFAULT_BUILD to its dir.
"""

import os
import pathlib
import re
import shlex
import shutil
import subprocess
import tempfile

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCH = ROOT / ".github" / "workflows" / "bench.yml"


def load(path):
    doc = yaml.safe_load(path.read_text())
    if True in doc:  # PyYAML reads the bare key `on` as True
        doc["on"] = doc.pop(True)
    return doc


def joined(run):
    return run.replace("\\\n", " ")


def first(steps, pred, what):
    for i, st in enumerate(steps):
        if pred(st):
            return i
    raise AssertionError(f"no step that {what}")


def test_bench_runs_rotating_rounds_and_uploads_csvs_and_chart():
    assert shutil.which("actionlint"), "actionlint not installed"
    lint = subprocess.run(["actionlint", str(BENCH)], capture_output=True, text=True)
    assert lint.returncode == 0, lint.stdout + lint.stderr

    doc = load(BENCH)
    on = doc["on"]
    assert isinstance(on, dict) and set(on) == {"workflow_dispatch"}, on
    assert "rounds" in (on["workflow_dispatch"] or {}).get("inputs", {})

    assert len(doc["jobs"]) == 1
    job = next(iter(doc["jobs"].values()))
    assert job["runs-on"] == "ubuntu-24.04"
    assert "matrix" not in job.get("strategy", {})
    steps = job["steps"]
    run = lambda st: joined(st.get("run", ""))

    cfg = first(steps, lambda st: re.search(r"\bcmake\b[^\n]*\s-[BS]", run(st))
                and "-DCMAKE_BUILD_TYPE=Release" in run(st), "configures a Release build")
    build = first(steps, lambda st: re.search(r"cmake\s+--build", run(st)), "builds")
    bench = first(steps, lambda st: "scripts/bench_all.sh" in run(st), "runs scripts/bench_all.sh")
    line = next(l for l in run(steps[bench]).splitlines() if "scripts/bench_all.sh" in l)
    words = shlex.split(line)
    args = words[words.index(next(w for w in words if w.endswith("scripts/bench_all.sh"))) + 1:]
    assert len(args) >= 3, line
    out, rounds = args[1], args[2]
    assert re.fullmatch(r"\$\{\{\s*(github\.event\.)?inputs\.rounds\s*\}\}", rounds), rounds

    env = {**doc.get("env", {}), **job.get("env", {}), **steps[bench].get("env", {})}
    for w in ("LMM", "HESTON", "CVA", "SABR"):
        assert str(env.get(f"BENCH_{w}_ARGS", "")).strip(), f"BENCH_{w}_ARGS not set"
    assert re.fullmatch(r"\$\{\{\s*github\.sha\s*\}\}", str(env.get("ADB_GIT_SHA", ""))), env.get("ADB_GIT_SHA")

    combine = first(steps, lambda st: re.search(r"scripts/combine_rounds\.py\s+" + re.escape(out), run(st)),
                    "runs combine_rounds.py on the output dir")
    chart = first(steps, lambda st: "scripts/chart.py" in run(st) and "--markdown" in run(st)
                  and "--svg" in run(st), "runs chart.py with --markdown and --svg")
    upload = first(steps, lambda st: st.get("uses", "").startswith("actions/upload-artifact@")
                   and out in str(st.get("with", {}).get("path", "")), "uploads the output dir")
    assert cfg < build < bench < combine < chart < upload, (cfg, build, bench, combine, chart, upload)

    first(steps, lambda st: re.search(r"\blscpu\b[^\n]*>\s*\S*" + re.escape(out), run(st)),
          "writes lscpu output into the output dir")


INPUT_EXPR = re.compile(r"\$\{\{\s*(?:github\.event\.)?inputs\.([A-Za-z_][A-Za-z0-9_]*)\s*\}\}")


def configure_args(inputs, which=0):
    """cmake arguments of the workflow's `which`-th configure step (cmake -S/-B),
    its run script executed by bash with a stub `cmake` that prints its
    arguments, ${{ inputs.<name> }} (also in the step's env) replaced by
    `inputs` (missing names: the input's default). Only plain input
    expressions are supported in that step."""
    doc = load(BENCH)
    declared = doc["on"]["workflow_dispatch"]["inputs"]
    values = {k: v.get("default", "") for k, v in declared.items()}
    values.update(inputs)

    def text(v):
        return "true" if v is True else "false" if v is False else str(v)

    def subst(s):
        assert "${{" not in INPUT_EXPR.sub("", s).replace("${{ github.", ""), s
        return INPUT_EXPR.sub(lambda m: text(values[m.group(1)]), s)

    job = next(iter(doc["jobs"].values()))
    steps = [st for st in job["steps"] if re.search(r"\bcmake\b[^\n]*\s-[BS]", joined(st.get("run", "")))]
    assert len(steps) > which, f"no configure step #{which + 1} (cmake -S/-B), {len(steps)} found"
    st = steps[which]
    with tempfile.TemporaryDirectory() as d:
        stub = pathlib.Path(d) / "cmake"
        stub.write_text('#!/bin/sh\nfor a in "$@"; do printf "%s\\n" "$a"; done\n')
        stub.chmod(0o755)
        env = dict(os.environ, PATH=f"{d}:{os.environ['PATH']}")
        env.update({k: subst(str(v)) for k, v in {**job.get("env", {}), **st.get("env", {})}.items()})
        out = subprocess.run(["bash", "-e", "-c", subst(st["run"])], env=env, cwd=d,
                             capture_output=True, text=True, check=True).stdout
    return out.splitlines()


def test_bench_takes_the_xad_pin_as_optional_inputs():
    # Inputs xad_commit and xad_sha256, default "": the configure passes them
    # as -DADB_XAD_COMMIT / -DADB_XAD_SHA256 when set, and leaves the
    # cmake/deps.cmake pin alone when they are empty.
    inputs = load(BENCH)["on"]["workflow_dispatch"]["inputs"]
    for name in ("xad_commit", "xad_sha256"):
        assert name in inputs, f"no input {name}"
        assert inputs[name].get("type") == "string" and inputs[name].get("default", "") == "", inputs[name]

    default = configure_args({})
    assert not [a for a in default if a.startswith("-DADB_XAD_")], default

    commit, digest = "0123456789abcdef0123456789abcdef01234567", "ab" * 32
    args = configure_args({"xad_commit": commit, "xad_sha256": digest})
    assert f"-DADB_XAD_COMMIT={commit}" in args, args
    assert f"-DADB_XAD_SHA256={digest}" in args, args

    # The dependency cache must not serve another commit's XAD.
    job = next(iter(load(BENCH)["jobs"].values()))
    keys = [str(st.get("with", {}).get("key", "")) for st in job["steps"]
            if st.get("uses", "").startswith("actions/cache@")]
    assert keys and all(re.search(r"inputs\.xad_commit\b", k) for k in keys), keys


def test_bench_configures_xad_with_the_suite_flags():
    args = configure_args({})
    for flag in ("-DXAD_USE_STRONG_INLINE=ON", "-DXAD_NO_THREADLOCAL=ON",
                 "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"):
        assert flag in args, (flag, args)


def test_bench_has_a_native_switch_default_off():
    # -march=native only on request: by default the suite's portable
    # ADB_ARCH_FLAGS (x86-64-v3) stay. The cached dependency builds are
    # compiled with the arch flags, so the cache key includes the switch.
    native = load(BENCH)["on"]["workflow_dispatch"]["inputs"].get("native")
    assert native and native.get("type") == "boolean" and native.get("default") is False, f"input native: {native}"

    assert not [a for a in configure_args({}) if a.startswith("-DADB_ARCH_FLAGS")]
    assert "-DADB_ARCH_FLAGS=-march=native" in configure_args({"native": True})

    job = next(iter(load(BENCH)["jobs"].values()))
    keys = [str(st.get("with", {}).get("key", "")) for st in job["steps"]
            if st.get("uses", "").startswith("actions/cache@")]
    assert keys and all(re.search(r"inputs\.native\b", k) for k in keys), keys


XAD_OPTIONS = ("-DXAD_USE_STRONG_INLINE", "-DXAD_NO_THREADLOCAL")


def test_bench_builds_an_xad_default_reference_and_runs_it_in_the_rounds():
    # A second configure step (own build dir) for the "xad-default" row: the
    # same arguments as the main configure, whatever the inputs, except that
    # XAD's own options (strong inlining, thread-local tape) are left at
    # their defaults; it may switch other libraries off (-DADB_WITH_<X>=OFF).
    # It is built before the benchmark, which gets it as
    # BENCH_XAD_DEFAULT_BUILD (scripts/bench_all.sh).
    commit, digest = "0123456789abcdef0123456789abcdef01234567", "ab" * 32
    for inputs in ({}, {"native": True}, {"xad_commit": commit, "xad_sha256": digest}):
        best, ref = configure_args(inputs, 0), configure_args(inputs, 1)
        best_dir, ref_dir = best[best.index("-B") + 1], ref[ref.index("-B") + 1]
        assert ref_dir != best_dir, ref
        assert not [a for a in ref if a.startswith(XAD_OPTIONS)], (inputs, ref)
        same = [a for a in best if not a.startswith(XAD_OPTIONS)]
        rest = [a for a in ref if not re.fullmatch(r"-DADB_WITH_[A-Z]+=OFF", a)]
        assert [ref_dir if a == best_dir else a for a in same] == rest, (inputs, best, ref)

    doc = load(BENCH)
    job = next(iter(doc["jobs"].values()))
    steps = job["steps"]
    run = lambda st: joined(st.get("run", ""))
    ref_dir = configure_args({}, 1)[configure_args({}, 1).index("-B") + 1]
    built = first(steps, lambda st: re.search(r"cmake\s+--build\s+" + re.escape(ref_dir) + r"(\s|$)", run(st)),
                  f"builds {ref_dir}")
    bench = first(steps, lambda st: "scripts/bench_all.sh" in run(st), "runs scripts/bench_all.sh")
    assert built < bench
    env = {**doc.get("env", {}), **job.get("env", {}), **steps[bench].get("env", {})}
    assert str(env.get("BENCH_XAD_DEFAULT_BUILD", "")) == ref_dir, env.get("BENCH_XAD_DEFAULT_BUILD")
