#!/usr/bin/env python3
"""Generates the shipped third-party notices from `third_party/notices.toml`.

Outputs:
  * `THIRD_PARTY_NOTICES.md` - human-readable attribution with full license texts;
  * `Source/Engine/Core/ThirdPartyNotices.cpp` - the same data embedded into the
    engine, exposed through `Fby::Notices` for an in-game licenses screen.

Both outputs are committed so a build never requires Python. `--check` verifies
that the committed files match the manifest and fails otherwise; run it before a
push and from CI.

Usage:
  python Documentation/Tools/GenerateThirdPartyNotices.py
  python Documentation/Tools/GenerateThirdPartyNotices.py --check
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import tomllib

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "third_party" / "notices.toml"
MARKDOWN_OUT = ROOT / "THIRD_PARTY_NOTICES.md"
SOURCE_OUT = ROOT / "Source" / "Engine" / "Core" / "ThirdPartyNotices.cpp"
RAW_DELIMITER = "FBYNOTICE"

REQUIRED_KEYS = ("name", "version", "spdx", "purpose", "copyright", "url")


def fail(message: str) -> None:
    print(f"third-party notices: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_components() -> list[dict[str, str]]:
    if not MANIFEST.is_file():
        fail(f"manifest not found: {MANIFEST}")
    with MANIFEST.open("rb") as handle:
        data = tomllib.load(handle)
    components = data.get("component")
    if not isinstance(components, list) or not components:
        fail("manifest has no [[component]] entries")

    seen: set[str] = set()
    for index, component in enumerate(components):
        for key in REQUIRED_KEYS:
            value = component.get(key)
            if not isinstance(value, str) or not value.strip():
                fail(f"component #{index + 1} is missing a value for '{key}'")
        name = component["name"]
        if name in seen:
            fail(f"duplicate component: {name}")
        seen.add(name)

        license_file = component.get("license_file")
        if license_file is not None:
            path = ROOT / license_file
            if not path.is_file():
                fail(f"{name}: license_file does not exist: {license_file}")
            component["license_text"] = path.read_text(encoding="utf-8").strip()
        else:
            component["license_text"] = (
                f"The upstream license text is not vendored in this checkout. "
                f"SPDX: {component['spdx']}. See {component['url']}."
            )
    return components


def render_markdown(components: list[dict[str, str]]) -> str:
    lines = [
        "# Third-party notices",
        "",
        "FemboyEngine2D is licensed under the Mozilla Public License 2.0 (see",
        "[LICENSE](LICENSE)). The components below are distributed with it under",
        "their own licenses; those licenses apply to the listed component and not",
        "to the engine source.",
        "",
        "This file is generated from [`third_party/notices.toml`](third_party/notices.toml)",
        "by [`Documentation/Tools/GenerateThirdPartyNotices.py`](Documentation/Tools/GenerateThirdPartyNotices.py).",
        "Do not edit it by hand; change the manifest and regenerate.",
        "",
        "| Component | Version | License (SPDX) | Purpose |",
        "|---|---|---|---|",
    ]
    for component in components:
        lines.append(
            f"| {component['name']} | {component['version']} | {component['spdx']} | {component['purpose']} |"
        )

    lines.extend(["", "## License texts", ""])
    for component in components:
        lines.append(f"### {component['name']} {component['version']}")
        lines.append("")
        lines.append(f"- SPDX: `{component['spdx']}`")
        lines.append(f"- Copyright: {component['copyright']}")
        lines.append(f"- Upstream: {component['url']}")
        lines.append("")
        lines.append("```text")
        lines.append(component["license_text"])
        lines.append("```")
        lines.append("")

    lines.append("## Engine license")
    lines.append("")
    lines.append(
        "The engine itself is distributed under the Mozilla Public License 2.0."
    )
    lines.append(
        "The complete source of every MPL-covered file is available from the project"
        " repository published with each release."
    )
    lines.append("")
    return "\n".join(lines)


def cpp_string(text: str) -> str:
    if f'){RAW_DELIMITER}"' in text:
        fail(f"text collides with the raw string delimiter {RAW_DELIMITER}")
    return f'R"{RAW_DELIMITER}({text}){RAW_DELIMITER}"'


def render_source(components: list[dict[str, str]], markdown: str) -> str:
    entries = []
    for component in components:
        entries.append(
            "    {"
            + ", ".join(
                cpp_string(component[key])
                for key in ("name", "version", "spdx", "purpose", "copyright", "license_text")
            )
            + "},"
        )
    return (
        "// FemboyEngine2D - generated third-party notices; source: third_party/notices.toml.\n"
        "// Regenerate with Documentation/Tools/GenerateThirdPartyNotices.py.\n"
        '#include "Engine/Core/ThirdPartyNotices.hpp"\n'
        "\n"
        "namespace Fby::Notices\n"
        "{\n"
        "namespace\n"
        "{\n"
        "constexpr NoticeEntry kEntries[] = {\n"
        + "\n".join(entries)
        + "\n};\n"
        "\n"
        "constexpr char kAllText[] = " + cpp_string(markdown) + ";\n"
        "}\n"
        "\n"
        "std::span<const NoticeEntry> Entries() noexcept\n"
        "{\n"
        "    return kEntries;\n"
        "}\n"
        "\n"
        "std::string_view AllText() noexcept\n"
        "{\n"
        "    return kAllText;\n"
        "}\n"
        "}\n"
    )


def write_or_check(path: pathlib.Path, content: str, check: bool) -> bool:
    if check:
        if not path.is_file():
            print(f"third-party notices: missing generated file {path}", file=sys.stderr)
            return False
        if path.read_text(encoding="utf-8") != content:
            print(f"third-party notices: stale generated file {path}", file=sys.stderr)
            return False
        return True
    path.write_text(content, encoding="utf-8")
    print(f"third-party notices: wrote {path.relative_to(ROOT).as_posix()}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate third-party notices.")
    parser.add_argument("--check", action="store_true", help="verify committed files are up to date")
    arguments = parser.parse_args()

    components = load_components()
    markdown = render_markdown(components)
    source = render_source(components, markdown)

    ok = write_or_check(MARKDOWN_OUT, markdown, arguments.check)
    ok = write_or_check(SOURCE_OUT, source, arguments.check) and ok

    if arguments.check and not ok:
        print("third-party notices: FAILED", file=sys.stderr)
        return 1
    if arguments.check:
        print(f"third-party notices: up to date ({len(components)} components)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
