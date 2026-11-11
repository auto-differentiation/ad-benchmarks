"""CMakePresets.json: the published run's build options as a preset, so the README shows one short command.

Contract: preset "published" configures a Ninja Release build in build/ with LTO, -march=native and the XAD
options of the published run; the README's build section names `cmake --preset published` and does not
spell the options out.
"""

import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _presets():
    return {p["name"]: p for p in json.loads((ROOT / "CMakePresets.json").read_text())["configurePresets"]}


def test_published_preset_holds_the_published_build_options():
    p = _presets()["published"]
    assert p["generator"] == "Ninja" and p["binaryDir"] == "${sourceDir}/build"
    assert p["cacheVariables"] == {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
        "ADB_ARCH_FLAGS": "-march=native",
        "XAD_USE_STRONG_INLINE": "ON",
        "XAD_NO_THREADLOCAL": "ON",
        "XAD_LINEAR_SLOTS": "ON",
        "XAD_POSITION_INDEPENDENT_CODE": "OFF",
    }


def test_readme_names_the_preset_instead_of_the_options():
    text = (ROOT / "README.md").read_text()
    assert "cmake --preset published" in text
    assert "-DXAD_LINEAR_SLOTS=ON" not in text
