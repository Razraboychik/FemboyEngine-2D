#!/usr/bin/env python3
"""Counts source lines per module, with and without the vendored libraries.

The engine ships its dependencies inside `third_party/`, so a single "lines of
code" number is meaningless: the engine proper is a small tree and the vendored
libraries are an order of magnitude larger. This script reports both, plus the
per-module breakdown, and separates code from comments and blank lines.

Usage:
  python Source/Tools/Dev/CountLines.py [--json]

Rules:
  * a line is counted once per file, in its own file's bucket;
  * `code` is a line that still has a non-space character after the comment
    scanner removes `//` and `/* */` comments (string literals are respected);
  * `comment` is a line whose only content is a comment;
  * `blank` is an empty or whitespace-only line;
  * generated build output and the OS temp directory never enter the count.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[3]
SOURCE = ROOT / "Source"
THIRD_PARTY = ROOT / "third_party"

# Extensions that count as engine source. Headers, C, C++ and Objective-C++.
SUFFIXES = {".hpp": "C++", ".cpp": "C++", ".h": "C/C++", ".c": "C", ".mm": "Objective-C++"}

MODULES = (
    "Engine",
    "Editor",
    "Apps",
    "Plugins",
    "Tools",
    "Tests",
)

# Directories that are generated or are not source at all.
SKIP = {"build", "Saved", "Projects", "third_party"}


def StripComments(text: str) -> tuple[str, int, int, int]:
    """Returns (code-without-comments, code lines, comment lines, blank lines)."""
    code_lines = 0
    comment_lines = 0
    blank_lines = 0
    in_block = False
    for raw in text.splitlines():
        line = raw
        if not line.strip() and not in_block:
            blank_lines += 1
            continue
        visible = []
        had_comment = in_block
        index = 0
        length = len(line)
        while index < length:
            if in_block:
                end = line.find("*/", index)
                if end < 0:
                    index = length
                else:
                    in_block = False
                    index = end + 2
                continue
            character = line[index]
            next_character = line[index + 1] if index + 1 < length else ""
            if character == "/" and next_character == "/":
                had_comment = True
                index = length
                continue
            if character == "/" and next_character == "*":
                in_block = True
                had_comment = True
                index += 2
                continue
            if character in "\"'":
                quote = character
                visible.append(character)
                index += 1
                while index < length:
                    visible.append(line[index])
                    if line[index] == "\\":
                        index += 2
                        continue
                    if line[index] == quote:
                        index += 1
                        break
                    index += 1
                continue
            visible.append(character)
            index += 1
        if "".join(visible).strip():
            code_lines += 1
        elif had_comment:
            comment_lines += 1
        else:
            blank_lines += 1
    return "", code_lines, comment_lines, blank_lines


def CountTree(root: pathlib.Path, skip_build_dirs: bool = True) -> dict:
    """Counts one directory tree and per-language totals."""
    totals = {"files": 0, "lines": 0, "code": 0, "comment": 0, "blank": 0}
    languages: dict[str, dict] = {}
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        # The vendored libraries are counted whole: a nested "build" directory
        # there is vendored source, not this repository's output.
        if skip_build_dirs and any(part in SKIP or part.startswith("build") for part in relative.parts):
            continue
        language = SUFFIXES.get(path.suffix)
        if language is None:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = path.read_text(encoding="utf-8", errors="replace")
        _, code, comment, blank = StripComments(text)
        lines = code + comment + blank
        entry = languages.setdefault(language, {"files": 0, "lines": 0, "code": 0, "comment": 0, "blank": 0})
        for bucket in (entry, totals):
            bucket["files"] += 1
            bucket["lines"] += lines
            bucket["code"] += code
            bucket["comment"] += comment
            bucket["blank"] += blank
    return {"totals": totals, "languages": languages}


def CountModule(name: str) -> dict:
    directory = SOURCE / name
    if not directory.is_dir():
        return {"totals": {"files": 0, "lines": 0, "code": 0, "comment": 0, "blank": 0}, "languages": {}}
    return CountTree(directory)


def main() -> int:
    parser = argparse.ArgumentParser(description="Count engine and vendored source lines")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    arguments = parser.parse_args()

    modules = {name: CountModule(name) for name in MODULES}
    engine_source = CountTree(SOURCE)
    libraries = CountTree(THIRD_PARTY, skip_build_dirs=False)

    without_libraries = engine_source["totals"]
    with_libraries = {
        key: without_libraries[key] + libraries["totals"][key]
        for key in ("files", "lines", "code", "comment", "blank")
    }

    if arguments.json:
        print(
            json.dumps(
                {
                    "modules": {name: modules[name]["totals"] for name in MODULES},
                    "source": without_libraries,
                    "third_party": libraries["totals"],
                    "with_libraries": with_libraries,
                },
                indent=2,
            )
        )
        return 0

    header = f"{'Scope':<22}{'Files':>8}{'Lines':>12}{'Code':>12}{'Comment':>10}{'Blank':>9}"
    print(header)
    print("-" * len(header))
    for name in MODULES:
        totals = modules[name]["totals"]
        print(
            f"Source/{name:<15}{totals['files']:>8}{totals['lines']:>12}{totals['code']:>12}"
            f"{totals['comment']:>10}{totals['blank']:>9}"
        )
    print("-" * len(header))
    print(
        f"{'Source (engine)':<22}{without_libraries['files']:>8}{without_libraries['lines']:>12}"
        f"{without_libraries['code']:>12}{without_libraries['comment']:>10}{without_libraries['blank']:>9}"
    )
    print(
        f"{'third_party':<22}{libraries['totals']['files']:>8}{libraries['totals']['lines']:>12}"
        f"{libraries['totals']['code']:>12}{libraries['totals']['comment']:>10}{libraries['totals']['blank']:>9}"
    )
    print(
        f"{'Total':<22}{with_libraries['files']:>8}{with_libraries['lines']:>12}"
        f"{with_libraries['code']:>12}{with_libraries['comment']:>10}{with_libraries['blank']:>9}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
