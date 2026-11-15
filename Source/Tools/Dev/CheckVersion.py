#!/usr/bin/env python3
"""Checks that Version.txt is the single source of the engine version.

CMake, the documentation generators, the release and packaging scripts and the
plugin fixtures all read the version from Version.txt. This gate fails when a
second copy appears or when README and the release manifest disagree with it.

    python Source/Tools/Dev/CheckVersion.py
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[3]
VERSION = (ROOT / "Version.txt").read_text(encoding="utf-8").strip()
SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-(?:alpha|beta|rc)\.[0-9]+)?$")
VERSION_LITERAL = re.compile(r'"[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z]+\.[0-9]+)?"')
ERRORS: list[str] = []


def Read(Relative: str) -> str:
    return (ROOT / Relative).read_text(encoding="utf-8")


def Require(Relative: str, Needle: str) -> None:
    if Needle not in Read(Relative):
        ERRORS.append(f"{Relative}: expected '{Needle}'")


def Main() -> int:
    if not SEMVER.match(VERSION):
        ERRORS.append(f"Version.txt: '{VERSION}' is not a numeric MAJOR.MINOR.PATCH version")

    sys.path.insert(0, str(ROOT / "Documentation" / "Tools"))
    import DocVersion  # noqa: E402

    if DocVersion.VERSION != VERSION:
        ERRORS.append(f"Documentation/Tools/DocVersion.py: '{DocVersion.VERSION}' != '{VERSION}'")

    # CMake must read the file and keep the numeric project version in sync.
    Cmake = Read("CMakeLists.txt")
    if "Version.txt" not in Cmake:
        ERRORS.append("CMakeLists.txt: does not read Version.txt")
    if "VERSION ${FBY_VERSION_NUMERIC}" not in Cmake:
        ERRORS.append("CMakeLists.txt: project() does not use FBY_VERSION_NUMERIC")
    if re.search(r"project\(FemboyEngine2D[\s\S]{0,160}VERSION [0-9]+\.[0-9]+\.[0-9]+", Cmake):
        ERRORS.append("CMakeLists.txt: project(VERSION) hardcodes a version number")

    # The release and packaging tools must derive the version from the file.
    Require("Source/Tools/Dev/ReleaseChecklist.py", "Version.txt")
    Require("Source/Tools/Dev/PackageDesktop.py", "Version.txt")

    # No workflow may pin a version on the command line.
    for Workflow in sorted((ROOT / ".github" / "workflows").glob("*.yml")):
        Text = Workflow.read_text(encoding="utf-8")
        for Match in re.finditer(r"--version[ =]\"?[0-9]", Text):
            Line = Text.count("\n", 0, Match.start()) + 1
            ERRORS.append(f".github/workflows/{Workflow.name}:{Line}: pins a literal version")

    # Plugin fixtures take the version from the FBY_ENGINE_VERSION definition.
    for Plugin in sorted((ROOT / "Source" / "Plugins").rglob("*.cpp")):
        Text = Plugin.read_text(encoding="utf-8")
        for Match in VERSION_LITERAL.finditer(Text):
            Line = Text.count("\n", 0, Match.start()) + 1
            ERRORS.append(
                f"{Plugin.relative_to(ROOT).as_posix()}:{Line}: "
                f"hardcodes {Match.group(0)}; use FBY_ENGINE_VERSION"
            )

    # Public entry points must show the current version.
    Badge = VERSION.replace("-", "--")
    Require("README.md", f"Status: {VERSION}")
    Require("README.md", f"status-{Badge}-blue.svg")
    Require("READMEs/README_RU.md", f"Статус: {VERSION}")
    Require("Documentation/README.md", f"**{VERSION}**")
    Require("Documentation/README_RU.md", f"**{VERSION}**")

    for Error in ERRORS:
        print(f"version gate: {Error}", file=sys.stderr)
    if ERRORS:
        return 1
    print(f"version gate: {VERSION} is consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(Main())
