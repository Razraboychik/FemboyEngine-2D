#!/usr/bin/env python3
"""Reports which vendored files are compiled, included or unused for one build.

The report is deliberately read-only. It exists so a developer can inspect a
platform's real vendor surface before deciding whether a build needs a slimmer
checkout; it never deletes or rewrites a vendor file.

    python Source/Tools/Dev/ReportVendorUsage.py --build build-dev
    python Source/Tools/Dev/ReportVendorUsage.py --build build-ci --json-output build/vendor.json
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[3]
THIRD = ROOT / "third_party"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inc", ".inl"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
TOKEN_RE = re.compile(r'"[^"]*"|\S+')


@dataclass(frozen=True)
class TranslationUnit:
    source: Path
    directory: Path
    include_dirs: tuple[Path, ...]


def normalize(path: Path) -> str:
    return os.path.normcase(str(path.resolve()))


def tokenize(command: str) -> list[str]:
    return [token.strip('"') for token in TOKEN_RE.findall(command)]


def include_dirs_for(directory: Path, tokens: list[str]) -> tuple[Path, ...]:
    result: list[Path] = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        candidate = ""
        if token in ("-I", "-isystem", "/I", "/external:I"):
            if index + 1 < len(tokens):
                candidate = tokens[index + 1]
                index += 1
        elif token.startswith("-isystem"):
            candidate = token[len("-isystem") :]
        elif token.startswith("-I") and len(token) > 2:
            candidate = token[2:]
        elif token.startswith("/I") and len(token) > 2:
            candidate = token[2:]
        elif token.startswith("/external:I") and len(token) > len("/external:I"):
            candidate = token[len("/external:I") :]
        if candidate:
            path = Path(candidate)
            if not path.is_absolute():
                path = directory / path
            result.append(path.resolve())
        index += 1
    return tuple(dict.fromkeys(result))


def load_commands(path: Path) -> list[TranslationUnit]:
    data = json.loads(path.read_text(encoding="utf-8"))
    units: list[TranslationUnit] = []
    for entry in data:
        directory = Path(entry.get("directory", ".")).resolve()
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        arguments = entry.get("arguments")
        if arguments:
            tokens = [str(item) for item in arguments]
        else:
            tokens = tokenize(entry.get("command", ""))
        units.append(TranslationUnit(source.resolve(), directory, include_dirs_for(directory, tokens)))
    return units


def resolve_include(name: str, current: Path, include_dirs: tuple[Path, ...]) -> Path | None:
    candidate = Path(name)
    candidates = [candidate] if candidate.is_absolute() else [current / candidate]
    candidates.extend(directory / candidate for directory in include_dirs if not candidate.is_absolute())
    for path in candidates:
        if path.is_file():
            resolved = path.resolve()
            if resolved.is_relative_to(ROOT):
                return resolved
    return None


def scan_includes(units: list[TranslationUnit]) -> set[str]:
    included: set[str] = set()
    scanned: set[str] = set()
    for unit in units:
        stack = [unit.source]
        while stack:
            current = stack.pop()
            key = normalize(current)
            if key in scanned:
                continue
            scanned.add(key)
            try:
                text = current.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for line in text.splitlines():
                match = INCLUDE_RE.match(line)
                if match is None:
                    continue
                resolved = resolve_include(match.group(1), current.parent, unit.include_dirs)
                if resolved is None:
                    continue
                if resolved.is_relative_to(THIRD):
                    included.add(normalize(resolved))
                stack.append(resolved)
    return included


def classify_files(units: list[TranslationUnit], included: set[str]) -> dict[str, list[dict[str, object]]]:
    compiled = {normalize(unit.source) for unit in units}
    categories: dict[str, list[dict[str, object]]] = defaultdict(list)
    for path in THIRD.rglob("*"):
        if not path.is_file() or ".git" in path.parts:
            continue
        relative = path.relative_to(THIRD)
        suffix = path.suffix.lower()
        key = normalize(path)
        if suffix in SOURCE_SUFFIXES:
            category = "compiled_source" if key in compiled else "uncompiled_source"
        elif suffix in HEADER_SUFFIXES:
            category = "included_header" if key in included else "uncompiled_header"
        else:
            category = "resource"
        categories[category].append(
            {
                "path": relative.as_posix(),
                "library": relative.parts[0] if relative.parts else "_root",
                "bytes": path.stat().st_size,
            }
        )
    return categories


def platform_name(build: Path, override: str) -> str:
    if override:
        return override
    cache = build / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith("CMAKE_SYSTEM_NAME:"):
                value = line.split("=", 1)[-1].strip().lower()
                if value in ("windows", "win32"):
                    return "Windows"
                if value in ("darwin", "macos", "macosx"):
                    return "macOS"
                if value == "linux":
                    return "Linux"
                return value
    return {"win32": "Windows", "darwin": "macOS", "linux": "Linux"}.get(sys.platform, sys.platform)


def write_markdown(path: Path, build: Path, platform: str, categories: dict[str, list[dict[str, object]]]) -> None:
    totals: dict[str, int] = {name: sum(int(item["bytes"]) for item in items) for name, items in categories.items()}
    libraries: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    for category, items in categories.items():
        for item in items:
            libraries[str(item["library"])][category] += int(item["bytes"])
    lines = [
        "# Vendor usage report",
        "",
        f"Build database: `{build}`",
        f"Platform: `{platform}`",
        "",
        "This report is read-only. It does not delete or modify a vendor file.",
        "",
        "## Summary",
        "",
        "| Category | Files | Bytes |",
        "|---|---:|---:|",
    ]
    for category in ("compiled_source", "included_header", "uncompiled_source", "uncompiled_header", "resource"):
        items = categories.get(category, [])
        lines.append(f"| {category} | {len(items)} | {totals.get(category, 0)} |")
    lines.extend(
        [
            "",
            "## By library",
            "",
            "| Library | Compiled source | Included header | Uncompiled source | Uncompiled header | Resource |",
            "|---|---:|---:|---:|---:|---:|",
        ]
    )
    for library in sorted(libraries):
        values = libraries[library]
        lines.append(
            f"| `{library}` | {values['compiled_source']} | {values['included_header']} | "
            f"{values['uncompiled_source']} | {values['uncompiled_header']} | {values['resource']} |"
        )
    lines.extend(["", "## Uncompiled source files", ""])
    uncompiled = sorted(categories.get("uncompiled_source", []), key=lambda item: int(item["bytes"]), reverse=True)
    if uncompiled:
        lines.extend(["| File | Bytes |", "|---|---:|"])
        lines.extend(f"| `{item['path']}` | {item['bytes']} |" for item in uncompiled)
    else:
        lines.append("None.")
    lines.extend(["", "## Unreferenced headers", ""])
    headers = sorted(categories.get("uncompiled_header", []), key=lambda item: int(item["bytes"]), reverse=True)
    if headers:
        lines.extend(["| File | Bytes |", "|---|---:|"])
        lines.extend(f"| `{item['path']}` | {item['bytes']} |" for item in headers[:200])
        if len(headers) > 200:
            lines.append(f"| ... | {len(headers) - 200} more |")
    else:
        lines.append("None.")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build", help="build directory containing compile_commands.json")
    parser.add_argument("--compile-commands", default="", help="explicit compile_commands.json path")
    parser.add_argument("--platform", default="", help="platform label; read from CMakeCache.txt by default")
    parser.add_argument("--output", default="build/vendor-usage.md", help="Markdown report path")
    parser.add_argument("--json-output", default="", help="optional JSON report path")
    parser.add_argument("--strict", action="store_true", help="fail when the compile database is absent")
    args = parser.parse_args()

    build = Path(args.build).resolve()
    database = Path(args.compile_commands).resolve() if args.compile_commands else build / "compile_commands.json"
    if not database.is_file():
        print(f"vendor report: compile database not found at {database}", file=sys.stderr)
        return 1 if args.strict else 2

    units = load_commands(database)
    included = scan_includes(units)
    categories = classify_files(units, included)
    platform = platform_name(build, args.platform)
    write_markdown(Path(args.output), build, platform, categories)

    if args.json_output:
        payload = {
            "build": str(build),
            "platform": platform,
            "translation_units": len(units),
            "categories": categories,
        }
        output = Path(args.json_output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    print(f"vendor report: {len(units)} translation units, platform {platform}")
    for category in ("compiled_source", "included_header", "uncompiled_source", "uncompiled_header", "resource"):
        items = categories.get(category, [])
        total = sum(int(item["bytes"]) for item in items)
        print(f"  {category}: {len(items)} files, {total} bytes")
    print(f"vendor report: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
