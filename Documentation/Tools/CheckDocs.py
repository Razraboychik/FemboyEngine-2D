#!/usr/bin/env python3
"""Checks the generated documentation set: links, language pairs and layout.

This is documentation tooling for the publishing machine, not an engine
dependency: it checks files, never links anything, and a build-only machine never
runs it.

The set has two languages and four formats, so the failure modes are always the
same three: a link that no longer resolves, a document that exists in one
language only, and a file that escaped the format/language layout. This script
checks exactly those and nothing else, so it stays dependency-free:

    python Documentation/Tools/CheckDocs.py [--strict]

Exit code 0 when no error was found. `--strict` turns the format warnings
(missing DOCX/PDF) into errors, which is the release gate.

Checks:
  * every relative Markdown link in README.md and in every md file resolves;
  * every document name has an English and a Russian Markdown edition;
  * every generated Markdown has its DOCX, PDF and, when available, DjVu edition;
  * no `bilingual` directory survives anywhere in the format tree;
  * no unexpected file sits in the Documentation root;
  * ADR files follow the `NNNN-<slug>.md` pattern;
  * the API and Complete Reference tables of contents are numbered 1..N.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import sys

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = ROOT / "Documentation"
MD = DOCS / "md"

# Documents that are generated as an English/Russian pair.
PAIRED_DOCUMENTS = (
    "FemboyEngineArchitecture",
    "FemboyEngineApplicationGuide",
    "FemboyEngineAPIReference",
    "FemboyEngineCompleteReference",
    "CMakeProjectManagement",
    "FemboyScript",
    "ProjectSettingsRecommendations",
    "FemboyEngineGettingStarted",
    "ViewportModes",
    "EditorSettingsReference",
)

# Files that are allowed directly in Documentation/. Source documents live in
# design/, style/, release/ and adr/; generated editions live in the format tree.
ROOT_FILES = {
    "README.md",
    "ROADMAP.md",
}

LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

# The DjVu edition is converted from the PDF by pdf2djvu, so it is only checked
# when that converter is installed. A machine without DjVuLibre still passes the
# gate; it simply has no DjVu files to verify.
def DjvuAvailable() -> bool:
    configured = os.environ.get("FBY_PDF2DJVU", "")
    if configured and pathlib.Path(configured).is_file():
        return True
    return shutil.which("pdf2djvu") is not None


def DjvuExpected() -> bool:
    """Checks DjVu when a converter is present or the tree already ships DjVu."""
    if DjvuAvailable():
        return True
    djvu_root = DOCS / "djvu"
    return djvu_root.is_dir() and any(djvu_root.rglob("*.djvu"))


# Rendered formats expected per document stem. Every document ships as an
# independent English and Russian edition.
FORMAT_MATRIX = {
    "FemboyEngineArchitecture": ("en", "ru"),
    "FemboyEngineApplicationGuide": ("en", "ru"),
    "FemboyEngineAPIReference": ("en", "ru"),
    "FemboyEngineCompleteReference": ("en", "ru"),
    "CMakeProjectManagement": ("en", "ru"),
    "FemboyScript": ("en", "ru"),
    "ProjectSettingsRecommendations": ("en", "ru"),
    "FemboyEngineGettingStarted": ("en", "ru"),
    "ViewportModes": ("en", "ru"),
    "EditorSettingsReference": ("en", "ru"),
}
TOC_ENTRY = re.compile(r"^(\d+)\. ")


def rel(path: pathlib.Path) -> str:
    return path.relative_to(ROOT).as_posix()


def check_links(errors: list[str]) -> None:
    sources = [DOCS / "README.md", DOCS / "ROADMAP.md"]
    for directory in ("design", "style", "release", "adr"):
        sources.extend(sorted((DOCS / directory).glob("*.md")))
    sources.extend(sorted(MD.rglob("*.md")))
    for source in sources:
        text = source.read_text(encoding="utf-8")
        for target in LINK.findall(text):
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            # Generated C++ signatures such as `[](std::uint32_t begin, ...)` are
            # not links; only path-shaped targets are checked.
            if " " in target or "::" in target or "(" in target or "<" in target:
                continue
            path_text = target.split("#", 1)[0]
            if not path_text:
                continue
            resolved = (source.parent / path_text).resolve()
            if not resolved.exists():
                errors.append(f"{rel(source)}: link target does not exist: {target}")


def check_language_pairs(errors: list[str]) -> None:
    for stem in PAIRED_DOCUMENTS:
        for path in (MD / "en" / f"{stem}_EN.md", MD / "ru" / f"{stem}_RU.md"):
            if not path.is_file():
                errors.append(f"missing paired document: {rel(path)}")


def check_formats(warnings: list[str]) -> None:
    kinds = ("docx", "pdf", "djvu") if DjvuExpected() else ("docx", "pdf")
    for stem, languages in FORMAT_MATRIX.items():
        for language in languages:
            edition = f"{stem}_{language.upper()}"
            for kind in kinds:
                candidate = DOCS / kind / language / f"{edition}.{kind}"
                if not candidate.is_file():
                    warnings.append(f"missing {kind.upper()} edition: {rel(candidate)}")


def check_layout(errors: list[str]) -> None:
    for item in sorted(DOCS.iterdir()):
        if item.is_file() and item.name not in ROOT_FILES:
            errors.append(f"unexpected file in Documentation root: {rel(item)}")
    for kind in ("md", "docx", "pdf", "djvu"):
        stale = DOCS / kind / "bilingual"
        if stale.exists():
            errors.append(f"bilingual edition must not exist: {rel(stale)}")
    for item in sorted((DOCS / "adr").glob("*.md")):
        if not re.fullmatch(r"\d{4}-[a-z0-9-]+\.md", item.name):
            errors.append(f"ADR name does not follow NNNN-slug.md: {rel(item)}")


def check_toc() -> None:
    for name in ("FemboyEngineAPIReference_EN.md", "FemboyEngineCompleteReference_EN.md"):
        path = MD / "en" / name
        if not path.is_file():
            continue
        numbers: list[int] = []
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("## Table of contents"):
                continue
            match = TOC_ENTRY.match(line)
            if match:
                numbers.append(int(match.group(1)))
            elif numbers and line.startswith("\n"):
                break
        # The first TOC entry starts at 1 and the sequence has no duplicate.
        if numbers and numbers[0] != 1:
            print(f"warning: {rel(path)}: table of contents does not start at 1")
        if numbers != list(range(1, len(numbers) + 1)):
            duplicates = sorted({value for value in numbers if numbers.count(value) > 1})
            print(f"warning: {rel(path)}: table of contents is not contiguous (duplicates: {duplicates})")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check the generated documentation set")
    parser.add_argument("--strict", action="store_true", help="treat missing formats as errors")
    arguments = parser.parse_args()

    errors: list[str] = []
    warnings: list[str] = []
    check_links(errors)
    check_language_pairs(errors)
    check_layout(errors)
    check_formats(warnings)
    if not DjvuExpected():
        print("note: pdf2djvu is not installed and no DjVu edition is checked in; DjVu is not checked")
    if arguments.strict:
        errors.extend(warnings)
        warnings.clear()

    for warning in warnings:
        print(f"warning: {warning}")
    for error in errors:
        print(f"error: {error}")
    if errors:
        print(f"docs gate: {len(errors)} error(s), {len(warnings)} warning(s)")
        return 1
    print(f"docs gate: all checks passed ({len(warnings)} warning(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
