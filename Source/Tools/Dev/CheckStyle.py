#!/usr/bin/env python3
"""FemboyEngine2D release-candidate style gate.

The gate is intentionally dependency-free so it can run on a build agent
without the engine's Python packages:

  * every source file stem is PascalCase;
  * type, alias, namespace and reflected component-field names are PascalCase;
  * no Vec2 / Vec3 / Vec4 identifiers remain;
  * no dynamic_cast / typeid in engine code;
  * no Cyrillic in comments or strings;
  * comments are English ASCII, never trailing, never footer markers and
    never ASCII-art banners;
  * braces follow Allman and if/else/for bodies are braced;
  * no InOut parameter names;
  * scripts and build files use English ASCII comments;
  * the required Source hierarchy and Definitions.hpp contract exist.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
SOURCE = ROOT / "Source"
SUFFIXES = {".hpp", ".cpp", ".mm", ".h", ".c"}
REQUIRED_DIRECTORIES = [
    "Source/Engine/Core",
    "Source/Engine/RHI",
    "Source/Engine/ECS",
    "Source/Engine/Physics",
    "Source/Engine/Audio",
    "Source/Engine/Network",
    "Source/Engine/Assets",
    "Source/Engine/Script",
    "Source/Engine/UI",
    "Source/Engine/Video",
    "Source/Editor/Panels",
    "Source/Editor/Infrastructure",
]
# Extra first-class modules. They are not in the required list because a
# platform-less or weather-less build is valid, but they are allowed to live
# directly under Source/Engine.
OPTIONAL_ENGINE_MODULES = {"Weather", "Platform", "Navigation", "Text"}
EDITOR_MODULES = {"Panels", "Infrastructure"}
JUNK_PATTERNS = (
    "*.obj",
    "*.o",
    "*.pdb",
    "*.ilk",
    "*.idb",
    "*.exp",
    "*.bak",
    "*.tmp",
    "*.dmp",
    "*.user",
    "*.suo",
    "*shot*.png",
    "*.log",
    ".DS_Store",
    "compile_commands.json",
    "CMakeCache.txt",
    "CMakeUserPresets.json",
)
JUNK_DIRECTORIES = {".vs", ".vscode", ".idea", "out", "__pycache__", ".cache", "CMakeFiles"}
SKIP_TREES = {".git", "build", "third_party", "Projects", "Saved"}
SCRIPT_SUFFIXES = {".py", ".ps1", ".sh", ".command", ".cmake", ".yml", ".yaml", ".bat", ".cmd"}
SCRIPT_ROOTS = ("Source", "SetupScripts", "cmake", ".github")


def skip_directory(name: str) -> bool:
    """Build trees are generated, so every build* directory is skipped."""
    return name in SKIP_TREES or name.startswith("build")
NAME = re.compile(r"^[A-Z][A-Za-z0-9]*$")
CYRILLIC = re.compile(r"[\u0400-\u04FF]")
NON_ASCII = re.compile(r"[^\x00-\x7F]")
ASCII_ART = re.compile(r"[-=*#~_]{4,}|[\u2500-\u257F]")
TYPE_DECLARATION = re.compile(
    r"^\s*(?:template\s*<[^>\n]*>\s*)?(?:struct|class|union|concept|enum(?:\s+class)?)\s+([A-Za-z_]\w*)\b(.*)$"
)
ALIAS_DECLARATION = re.compile(r"^(?:template\s*<[^>\n]*>\s*)?using\s+([A-Za-z_]\w*)\s*=")
NAMESPACE_DECLARATION = re.compile(r"^namespace\s+([A-Za-z_]\w*)\b")
REFLECTED_FIELD = re.compile(r"FBY_FIELD\(\s*[^,()]+,\s*([A-Za-z_]\w*)\s*,")
FOOTER_COMMENT = re.compile(
    r"^\s*//\s*End of (?:anonymous )?namespace\b|^\s*//\s*End of (?:FaultCode|file)\s*$"
)


def mask_non_code(text: str) -> str:
    """Blank comments and literals while preserving newlines and offsets."""
    output = list(text)
    index = 0
    length = len(text)
    while index < length:
        character = text[index]
        next_character = text[index + 1] if index + 1 < length else ""
        if character == "/" and next_character == "/":
            end = text.find("\n", index)
            end = length if end < 0 else end
            for position in range(index, end):
                output[position] = " "
            index = end
        elif character == "/" and next_character == "*":
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            for position in range(index, end):
                if output[position] != "\n":
                    output[position] = " "
            index = end
        elif character == "R" and next_character == '"':
            delimiter_end = text.find("(", index + 2)
            if delimiter_end < 0:
                break
            delimiter = text[index + 2 : delimiter_end]
            terminator = ")" + delimiter + '"'
            end = text.find(terminator, delimiter_end + 1)
            end = length if end < 0 else end + len(terminator)
            for position in range(index, end):
                if output[position] != "\n":
                    output[position] = " "
            index = end
        elif character in {'"', "'"}:
            if (
                character == "'"
                and index > 0
                and text[index - 1].isalnum()
                and index + 1 < length
                and text[index + 1].isalnum()
            ):
                index += 1
                continue
            quote = character
            cursor = index + 1
            while cursor < length:
                if text[cursor] == "\\":
                    cursor += 2
                    continue
                if text[cursor] == quote:
                    cursor += 1
                    break
                cursor += 1
            for position in range(index, cursor):
                if output[position] != "\n":
                    output[position] = " "
            index = cursor
        else:
            index += 1
    return "".join(output)


def line_comment_positions(text: str) -> list[tuple[int, int]]:
    """Return (line, column) for real // comments, not string contents."""
    positions: list[tuple[int, int]] = []
    index = 0
    line = 0
    length = len(text)
    while index < length:
        character = text[index]
        next_character = text[index + 1] if index + 1 < length else ""
        if character == "\n":
            line += 1
            index += 1
        elif character == "/" and next_character == "/":
            positions.append((line, index - text.rfind("\n", 0, index) - 1))
            end = text.find("\n", index)
            index = length if end < 0 else end
        elif character == "/" and next_character == "*":
            end = text.find("*/", index + 2)
            if end < 0:
                break
            line += text.count("\n", index, end + 2)
            index = end + 2
        elif character == "R" and next_character == '"':
            delimiter_end = text.find("(", index + 2)
            if delimiter_end < 0:
                break
            delimiter = text[index + 2 : delimiter_end]
            terminator = ")" + delimiter + '"'
            end = text.find(terminator, delimiter_end + 1)
            if end < 0:
                break
            line += text.count("\n", index, end + len(terminator))
            index = end + len(terminator)
        elif character in {'"', "'"}:
            if (
                character == "'"
                and index > 0
                and text[index - 1].isalnum()
                and index + 1 < length
                and text[index + 1].isalnum()
            ):
                index += 1
                continue
            quote = character
            cursor = index + 1
            while cursor < length:
                if text[cursor] == "\\":
                    cursor += 2
                    continue
                if text[cursor] == quote:
                    cursor += 1
                    break
                if text[cursor] == "\n":
                    break
                cursor += 1
            index = cursor
        else:
            index += 1
    return positions


def comment_spans(text: str) -> list[tuple[int, str]]:
    """Return the starting line and text of every real C++ comment."""
    spans: list[tuple[int, str]] = []
    index = 0
    line = 1
    length = len(text)
    while index < length:
        character = text[index]
        next_character = text[index + 1] if index + 1 < length else ""
        if character == "\n":
            line += 1
            index += 1
        elif character == "/" and next_character == "/":
            end = text.find("\n", index)
            end = length if end < 0 else end
            spans.append((line, text[index:end]))
            index = end
        elif character == "/" and next_character == "*":
            end = text.find("*/", index + 2)
            end = length if end < 0 else end + 2
            spans.append((line, text[index:end]))
            line += text.count("\n", index, end)
            index = end
        elif character == "R" and next_character == '"':
            delimiter_end = text.find("(", index + 2)
            if delimiter_end < 0:
                break
            delimiter = text[index + 2 : delimiter_end]
            terminator = ")" + delimiter + '"'
            end = text.find(terminator, delimiter_end + 1)
            end = length if end < 0 else end + len(terminator)
            line += text.count("\n", index, end)
            index = end
        elif character in {'"', "'"}:
            if (
                character == "'"
                and index > 0
                and text[index - 1].isalnum()
                and index + 1 < length
                and text[index + 1].isalnum()
            ):
                index += 1
                continue
            quote = character
            cursor = index + 1
            while cursor < length:
                if text[cursor] == "\\":
                    cursor += 2
                    continue
                if text[cursor] == quote:
                    cursor += 1
                    break
                cursor += 1
            line += text.count("\n", index, cursor)
            index = cursor
        else:
            index += 1
    return spans


def type_declaration_failures(text: str) -> list[tuple[int, str]]:
    """Return non-PascalCase type, alias and namespace declarations."""
    failures: list[tuple[int, str]] = []
    for number, line in enumerate(mask_non_code(text).splitlines(), 1):
        match = TYPE_DECLARATION.match(line)
        if match is not None:
            remainder = match.group(2).strip()
            if not remainder or not (remainder[0].isalnum() or remainder[0] == "_"):
                if not NAME.match(match.group(1)):
                    failures.append((number, f"type name is not PascalCase: {match.group(1)}"))
        if line == line.lstrip():
            alias = ALIAS_DECLARATION.match(line)
            if alias is not None and not NAME.match(alias.group(1)):
                failures.append((number, f"type alias is not PascalCase: {alias.group(1)}"))
            namespace = NAMESPACE_DECLARATION.match(line)
            if namespace is not None:
                declaration = line[len("namespace ") :].split("{", 1)[0]
                if "=" in declaration:
                    continue
                for part in declaration.strip().split("::"):
                    clean = part.strip()
                    if clean and not NAME.match(clean):
                        failures.append((number, f"namespace is not PascalCase: {clean}"))
    return failures


def reflected_field_failures(text: str) -> list[tuple[int, str]]:
    """Return non-PascalCase fields registered through FBY_FIELD."""
    failures: list[tuple[int, str]] = []
    for number, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("#define"):
            continue
        for match in REFLECTED_FIELD.finditer(line):
            if not NAME.match(match.group(1)):
                failures.append((number, f"reflected field is not PascalCase: {match.group(1)}"))
    return failures


def script_comment(line: str) -> str:
    """Extract a hash or batch comment while ignoring quoted text."""
    stripped = line.lstrip()
    lowered = stripped.lower()
    if lowered.startswith("rem ") or lowered.startswith("::"):
        return stripped
    quote = ""
    escaped = False
    for index, character in enumerate(line):
        if escaped:
            escaped = False
            continue
        if character == "\\" and quote:
            escaped = True
            continue
        if quote:
            if character == quote:
                quote = ""
            continue
        if character in {'"', "'"}:
            quote = character
            continue
        if character == "#":
            return line[index:]
    return ""


def control_body_failures(lines: list[str]) -> list[int]:
    """Flag control statements whose next non-empty line is not an opening brace."""
    failures: list[int] = []
    for number, line in enumerate(lines):
        stripped = line.strip()
        if not re.match(r"^(?:else\s+)?(?:if|for|while)\b", stripped):
            continue
        depth = 0
        close = -1
        for position, character in enumerate(line):
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    close = position
                    break
        if close < 0:
            continue
        tail = line[close + 1 :].strip()
        if tail:
            if not tail.startswith("{") and not tail.startswith("[[") and tail != "\\":
                failures.append(number + 1)
            continue
        cursor = number + 1
        while cursor < len(lines) and not lines[cursor].strip():
            cursor += 1
        if cursor < len(lines) and not lines[cursor].lstrip().startswith("{"):
            failures.append(number + 1)
    return failures


def main() -> int:
    failures: list[str] = []
    for directory in REQUIRED_DIRECTORIES:
        if not (ROOT / directory).is_dir():
            failures.append(f"missing required directory: {directory}")

    # The hierarchy contract: every engine/editor translation unit belongs to a
    # module directory, never directly to Source/Engine or Source/Editor.
    engine_root = ROOT / "Source" / "Engine"
    editor_root = ROOT / "Source" / "Editor"
    for stray in sorted(engine_root.glob("*.cpp")) + sorted(engine_root.glob("*.hpp")):
        failures.append(f"{stray.relative_to(ROOT).as_posix()}: file directly under Source/Engine")
    for stray in sorted(editor_root.glob("*.cpp")) + sorted(editor_root.glob("*.hpp")):
        failures.append(f"{stray.relative_to(ROOT).as_posix()}: file directly under Source/Editor")
    allowed_engine = {
        item.name
        for item in engine_root.iterdir()
        if item.is_dir()
    } - OPTIONAL_ENGINE_MODULES
    allowed_engine -= {directory.rsplit("/", 1)[-1] for directory in REQUIRED_DIRECTORIES}
    if allowed_engine:
        failures.append("unexpected Source/Engine module(s): " + ", ".join(sorted(allowed_engine)))
    for item in sorted(editor_root.iterdir()):
        if item.is_dir() and item.name not in EDITOR_MODULES:
            failures.append(f"unexpected Source/Editor module: {item.name}")

    # Repository hygiene: build output, temp objects, screenshots and editor
    # state must never be committed next to the sources.
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file():
            continue
        relative_parts = path.relative_to(ROOT).parts
        if any(skip_directory(part) for part in relative_parts):
            continue
        junk_directory = next((part for part in relative_parts if part in JUNK_DIRECTORIES), "")
        if junk_directory:
            failures.append(f"{path.relative_to(ROOT).as_posix()}: junk directory ({junk_directory})")
            continue
        if any(path.match(pattern) for pattern in JUNK_PATTERNS):
            failures.append(f"{path.relative_to(ROOT).as_posix()}: junk file ({path.suffix})")

    definitions = ROOT / "Source/Engine/Core/Definitions.hpp"
    if not definitions.is_file():
        failures.append("missing Source/Engine/Core/Definitions.hpp")
    else:
        definitions_text = definitions.read_text(encoding="utf-8")
        for token in ("FORCEINLINE", "FemCast", "Cast(", "StableTypeId", "TypeObject"):
            if token not in definitions_text:
                failures.append(f"Definitions.hpp is missing {token}")

    for path in sorted(SOURCE.rglob("*")):
        if not path.is_file() or path.suffix not in SUFFIXES:
            continue
        relative = path.relative_to(ROOT).as_posix()
        if not NAME.match(path.stem):
            failures.append(f"{relative}: file stem is not PascalCase")

        text = path.read_text(encoding="utf-8")
        if CYRILLIC.search(text):
            failures.append(f"{relative}: contains Cyrillic text")

        for number, message in type_declaration_failures(text):
            failures.append(f"{relative}:{number}: {message}")
        for number, message in reflected_field_failures(text):
            failures.append(f"{relative}:{number}: {message}")
        for number, comment in comment_spans(text):
            if NON_ASCII.search(comment):
                failures.append(f"{relative}:{number}: non-ASCII comment")
            if ASCII_ART.search(comment):
                failures.append(f"{relative}:{number}: ASCII-art comment")
        for number, line in enumerate(text.splitlines(), 1):
            if FOOTER_COMMENT.match(line):
                failures.append(f"{relative}:{number}: footer comment is not allowed")

        masked = mask_non_code(text)
        for token in ("Vec2", "Vec3", "Vec4"):
            if re.search(rf"\b{token}\b", masked):
                failures.append(f"{relative}: {token} identifier remains")
        if re.search(r"\bdynamic_cast\b|\btypeid\b", masked):
            failures.append(f"{relative}: RTTI construct remains")
        if re.search(r"\bInOut[A-Za-z0-9]*\b", masked):
            failures.append(f"{relative}: InOut parameter name remains")
        if re.search(r"\bg_[A-Za-z0-9_]+\b", masked):
            failures.append(f"{relative}: g_ global name remains")

        for number, line in enumerate(masked.splitlines()):
            if re.search(r"\)\s*\{\s*$", line) or re.search(r"\b(else|do)\s*\{\s*$", line):
                if "^" not in line:
                    failures.append(f"{relative}:{number + 1}: non-Allman brace")
        for number in control_body_failures(masked.splitlines()):
            failures.append(f"{relative}:{number}: unbraced control body")

        comment_lines = line_comment_positions(text)
        for number, column in comment_lines:
            line = text.splitlines()[number]
            code = line[:column].strip()
            comment = line[column:]
            if code and "NOLINT" not in comment:
                failures.append(f"{relative}:{number + 1}: trailing comment is not allowed")

    # Scripts and build files follow the same comment language and art rules.
    script_paths: set[pathlib.Path] = set()
    for directory_name in SCRIPT_ROOTS:
        directory = ROOT / directory_name
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix in SCRIPT_SUFFIXES or path.name == "CMakeLists.txt":
                script_paths.add(path)
    root_cmake = ROOT / "CMakeLists.txt"
    if root_cmake.is_file():
        script_paths.add(root_cmake)
    for path in sorted(script_paths):
        text = path.read_text(encoding="utf-8-sig")
        relative = path.relative_to(ROOT).as_posix()
        for number, line in enumerate(text.splitlines(), 1):
            comment = script_comment(line)
            if comment == "":
                continue
            if NON_ASCII.search(comment):
                failures.append(f"{relative}:{number}: non-ASCII script comment")
            if ASCII_ART.search(comment):
                failures.append(f"{relative}:{number}: ASCII-art script comment")

    if failures:
        print(f"style gate: {len(failures)} failure(s)")
        for failure in failures[:200]:
            print("  " + failure)
        return 1
    print("style gate: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
