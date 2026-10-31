#!/usr/bin/env python3
"""Renders the authored reference manuals and checks them against the sources.

The two manuals are authored Markdown, not content-model data:

    Documentation/md/en/ViewportModes_EN.md
    Documentation/md/ru/ViewportModes_RU.md
    Documentation/md/en/EditorSettingsReference_EN.md
    Documentation/md/ru/EditorSettingsReference_RU.md

This script renders each one into DOCX, PDF and DjVu. The DOCX is produced by
MarkdownDocs.build_docx, the PDF by MarkdownDocs.build_pdf through the LibreOffice
Kit CLI, and the DjVu by DocumentConvert.BuildDjvu, so the conversion lives in one
place and this file only owns the document list and the drift check.

The `--check` mode is the documentation gate for editor settings. It parses the
`[editor]` table of ProjectConfig.cpp and the members of EditorSettings, then
fails with an exact missing key or field when either language of the settings
reference falls behind the sources:

    python Documentation/Tools/GenerateReferenceDocs.py --check

The check also verifies that both languages of the viewport manual mention the two
viewport settings, so the manual cannot silently drop the keys it documents.

Usage:
  python Documentation/Tools/GenerateReferenceDocs.py --skip-pdf
  python Documentation/Tools/GenerateReferenceDocs.py --check
  python Documentation/Tools/GenerateReferenceDocs.py --node <node.exe> --lo-cli <cli.js>
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import DocPaths
import DocumentConvert as DocConvert
import MarkdownDocs

RootPath = pathlib.Path(__file__).resolve().parents[2]
DocumentationPath = RootPath / "Documentation"

# Node and the LibreOffice Kit CLI follow the FBY_NODE and FBY_LO_CLI convention.
# An empty default keeps the resolution inside MarkdownDocs, which already falls
# back to the bundled runtime paths.
NodeDefault = os.environ.get("FBY_NODE", "")
LoCliDefault = os.environ.get("FBY_LO_CLI", "")

# One entry per authored manual: the file stem and the languages that must exist.
Documents = (
    {"Stem": "ViewportModes"},
    {"Stem": "EditorSettingsReference"},
)
Languages = ("en", "ru")

# Source files the drift check reads. Both are the single source of their list.
ProjectConfigSource = DocumentationPath.parent / "Source/Editor/Infrastructure/ProjectConfig.cpp"
EditorSettingsSource = DocumentationPath.parent / "Source/Editor/Infrastructure/EditorSettings.hpp"

# The encoder block of the [editor] table. The markers are structural, so a
# renamed local variable is the only way they can drift.
EditorTableStart = 'toml::table ScopedEditorTable;'
EditorTableEnd = 'ScopedRoot.insert("editor"'
EditorInsertPattern = re.compile(r'insert\("([A-Za-z0-9_]+)"')
EditorStructStart = "struct EditorSettings"
EditorStructEnd = "\n};"
EditorMemberPattern = re.compile(
    r"^\s+(?:float|bool|char|int|double|std::uint8_t|std::uint16_t|std::uint32_t|std::uint64_t)"
    r"\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*(?:=|;)",
    re.MULTILINE,
)

# A parse that finds fewer entries than this means the markers changed, not that
# the settings shrank. Failing loudly is the point of the gate.
MinimumEditorKeys = 20
MinimumEditorFields = 15

# The viewport keys and fields that the viewport manuals must mention.
ViewportTokens = (
    "viewport_view_mode",
    "viewport_complexity_budget",
    "viewportViewMode",
    "viewportComplexityBudget",
)


def ReadText(InPath: pathlib.Path) -> str:
    """Reads a UTF-8 file and returns its text."""
    return InPath.read_text(encoding="utf-8")


def DisplayPath(InPath: pathlib.Path) -> str:
    """Returns a repository-relative POSIX path, or the path itself when outside."""
    try:
        return InPath.resolve().relative_to(RootPath).as_posix()
    except ValueError:
        return InPath.as_posix()


def ParseEditorKeys(InText: str) -> list[str]:
    """Returns the keys inserted into the [editor] table, in encoder order."""
    ScopedStart = InText.find(EditorTableStart)
    if ScopedStart < 0:
        raise ValueError(f"cannot find the encoder marker: {EditorTableStart}")
    ScopedEnd = InText.find(EditorTableEnd, ScopedStart)
    if ScopedEnd < 0:
        raise ValueError(f"cannot find the encoder end marker: {EditorTableEnd}")
    ScopedKeys: list[str] = []
    for ScopedMatch in EditorInsertPattern.finditer(InText[ScopedStart:ScopedEnd]):
        ScopedKey = ScopedMatch.group(1)
        if ScopedKey not in ScopedKeys:
            ScopedKeys.append(ScopedKey)
    return ScopedKeys


def ParseEditorFields(InText: str) -> list[str]:
    """Returns the EditorSettings member names, in declaration order."""
    ScopedStart = InText.find(EditorStructStart)
    if ScopedStart < 0:
        raise ValueError(f"cannot find the struct marker: {EditorStructStart}")
    ScopedEnd = InText.find(EditorStructEnd, ScopedStart)
    if ScopedEnd < 0:
        raise ValueError("cannot find the end of the EditorSettings struct")
    ScopedFields: list[str] = []
    for ScopedMatch in EditorMemberPattern.finditer(InText[ScopedStart:ScopedEnd]):
        ScopedField = ScopedMatch.group(1)
        if ScopedField not in ScopedFields:
            ScopedFields.append(ScopedField)
    return ScopedFields


def MentionErrors(InPath: pathlib.Path, InTokens: list[str], InKind: str) -> list[str]:
    """Returns one error line per token that the Markdown file does not mention."""
    if not InPath.is_file():
        return [f"missing Markdown file: {DisplayPath(InPath)}"]
    ScopedText = ReadText(InPath)
    ScopedErrors: list[str] = []
    for ScopedToken in InTokens:
        if ScopedToken not in ScopedText:
            ScopedErrors.append(f"{DisplayPath(InPath)}: missing {InKind}: {ScopedToken}")
    return ScopedErrors


def CheckDocuments() -> int:
    """Verifies that the manuals mention every key and every editor field."""
    ScopedErrors: list[str] = []
    if not ProjectConfigSource.is_file():
        print(f"error: missing source file: {ProjectConfigSource.as_posix()}")
        return 1
    if not EditorSettingsSource.is_file():
        print(f"error: missing source file: {EditorSettingsSource.as_posix()}")
        return 1

    try:
        ScopedKeys = ParseEditorKeys(ReadText(ProjectConfigSource))
        ScopedFields = ParseEditorFields(ReadText(EditorSettingsSource))
    except ValueError as ScopedError:
        print(f"error: {ScopedError}")
        return 1

    # Report the parsed counts so a silently empty parse is visible in the log.
    print(f"check: [editor] keys: {len(ScopedKeys)}")
    print(f"check: EditorSettings fields: {len(ScopedFields)}")
    if len(ScopedKeys) < MinimumEditorKeys:
        print(
            f"error: parsed only {len(ScopedKeys)} [editor] key(s); "
            f"the encoder markers in {ProjectConfigSource.name} changed"
        )
        return 1
    if len(ScopedFields) < MinimumEditorFields:
        print(
            f"error: parsed only {len(ScopedFields)} EditorSettings field(s); "
            f"the struct markers in {EditorSettingsSource.name} changed"
        )
        return 1

    ScopedSettingsTokens = ScopedKeys + ScopedFields
    for ScopedLanguage in Languages:
        ScopedErrors.extend(
            MentionErrors(
                DocPaths.Md(ScopedLanguage, f"EditorSettingsReference_{ScopedLanguage.upper()}.md"),
                ScopedSettingsTokens,
                "key or field",
            )
        )
        ScopedErrors.extend(
            MentionErrors(
                DocPaths.Md(ScopedLanguage, f"ViewportModes_{ScopedLanguage.upper()}.md"),
                list(ViewportTokens),
                "viewport key or field",
            )
        )

    for ScopedError in ScopedErrors:
        print(f"error: {ScopedError}")
    if ScopedErrors:
        print(f"docs drift: {len(ScopedErrors)} missing mention(s); update the Markdown")
        return 1
    print("docs drift: none, every [editor] key and every EditorSettings field is documented")
    return 0


def ConfigurePdf(InArguments) -> None:
    """Points MarkdownDocs at the requested Node and LibreOffice Kit CLI."""
    if InArguments.Node:
        MarkdownDocs.NODE = pathlib.Path(InArguments.Node)
    if InArguments.LoCli:
        MarkdownDocs.LIBREOFFICE_CLI = pathlib.Path(InArguments.LoCli)


def RenderDocument(InDocument: dict, InLanguage: str, InSkipPdf: bool) -> int:
    """Renders one Markdown edition to DOCX and, unless skipped, PDF and DjVu."""
    ScopedSuffix = InLanguage.upper()
    ScopedStem = f"{InDocument['Stem']}_{ScopedSuffix}"
    ScopedMarkdown = DocPaths.Md(InLanguage, f"{ScopedStem}.md")
    if not ScopedMarkdown.is_file():
        print(f"missing Markdown: {DisplayPath(ScopedMarkdown)}", file=sys.stderr)
        return 1

    ScopedDocx = DocPaths.Docx(InLanguage, f"{ScopedStem}.docx")
    print(f"docx {DisplayPath(ScopedDocx)}")
    MarkdownDocs.build_docx(ScopedMarkdown, ScopedDocx)
    if InSkipPdf:
        return 0

    ScopedPdf = DocPaths.Pdf(InLanguage, f"{ScopedStem}.pdf")
    if MarkdownDocs.build_pdf(ScopedDocx, ScopedPdf) != 0:
        return 1
    print(f"pdf  {DisplayPath(ScopedPdf)}")
    DocConvert.BuildDjvu(ScopedPdf, DocPaths.Djvu(InLanguage, f"{ScopedStem}.djvu"))
    return 0


def RenderAll(InArguments) -> int:
    """Renders both manuals in both languages."""
    DocPaths.EnsureLayout()
    ConfigurePdf(InArguments)
    ScopedFailures = 0
    for ScopedDocument in Documents:
        for ScopedLanguage in Languages:
            ScopedFailures += RenderDocument(ScopedDocument, ScopedLanguage, InArguments.SkipPdf)
    if ScopedFailures:
        print(f"reference docs: {ScopedFailures} failure(s)", file=sys.stderr)
        return 1
    print("reference docs: all editions rendered")
    return 0


def Main() -> int:
    """Entry point."""
    parser = argparse.ArgumentParser(description="Render and check the authored reference manuals")
    parser.add_argument(
        "--skip-pdf",
        dest="SkipPdf",
        action="store_true",
        help="write DOCX only; skips PDF and DjVu",
    )
    parser.add_argument(
        "--node",
        dest="Node",
        default=NodeDefault,
        help="node executable for LibreOffice Kit (default: FBY_NODE)",
    )
    parser.add_argument(
        "--lo-cli",
        dest="LoCli",
        default=LoCliDefault,
        help="LibreOffice Kit cli.js (default: FBY_LO_CLI)",
    )
    parser.add_argument(
        "--check",
        dest="Check",
        action="store_true",
        help="verify the Markdown against the [editor] keys and EditorSettings fields",
    )
    ScopedArguments = parser.parse_args()
    if ScopedArguments.Check:
        return CheckDocuments()
    return RenderAll(ScopedArguments)


if __name__ == "__main__":
    raise SystemExit(Main())
