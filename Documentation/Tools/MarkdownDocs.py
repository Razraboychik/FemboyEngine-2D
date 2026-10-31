#!/usr/bin/env python3
"""Renders the Markdown documentation into DOCX and PDF.

The converter supports the subset the project's guides use: level one to three
headings, paragraphs, bullet and numbered lists, pipe tables, fenced code
blocks and inline bold or code spans. The DOCX is written with python-docx and
the PDF is produced from the DOCX through the LibreOffice Kit CLI, so the two
formats share one layout.

Usage:
  python Documentation/Tools/MarkdownDocs.py --input <file.md> --docx <file.docx> --pdf <file.pdf>
  python Documentation/Tools/MarkdownDocs.py --all
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOC_ROOT = ROOT / "Documentation"

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import DocumentConvert as docconvert  # noqa: E402
# PDF export needs Node and the LibreOffice Kit CLI. The environment variables
# win so a machine without the agent runtime can point at its own install.
NODE = pathlib.Path(os.environ.get("FBY_NODE", "node"))
LIBREOFFICE_CLI = pathlib.Path(os.environ.get("FBY_LO_CLI", ""))
# Documents that are rendered by --all, per language directory.
GUIDES = {
    "en": ("FemboyEngineGettingStarted_EN.md", "FemboyEngineGettingStarted_EN"),
    "ru": ("FemboyEngineGettingStarted_RU.md", "FemboyEngineGettingStarted_RU"),
}


def split_table_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def inline_runs(paragraph, text: str, code_font: str) -> None:
    """Adds the text to a paragraph, honouring **bold** and `code` spans."""
    pattern = re.compile(r"(\*\*.+?\*\*|`.+?`)")
    for piece in pattern.split(text):
        if not piece:
            continue
        if piece.startswith("**") and piece.endswith("**"):
            run = paragraph.add_run(piece[2:-2])
            run.bold = True
        elif piece.startswith("`") and piece.endswith("`"):
            run = paragraph.add_run(piece[1:-1])
            run.font.name = code_font
        else:
            paragraph.add_run(piece)


def render_markdown(markdown: str, document, body_font: str, code_font: str) -> None:
    from docx.shared import Pt, RGBColor

    lines = markdown.splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if not stripped:
            index += 1
            continue
        if stripped.startswith("```"):
            index += 1
            block: list[str] = []
            while index < len(lines) and not lines[index].strip().startswith("```"):
                block.append(lines[index])
                index += 1
            index += 1
            for code_line in block:
                paragraph = document.add_paragraph()
                paragraph.paragraph_format.space_after = Pt(0)
                run = paragraph.add_run(code_line if code_line else " ")
                run.font.name = code_font
                run.font.size = Pt(9)
            continue
        if stripped.startswith("|"):
            rows: list[list[str]] = []
            while index < len(lines) and lines[index].strip().startswith("|"):
                row = split_table_row(lines[index])
                if not all(re.fullmatch(r":?-{2,}:?", cell) for cell in row):
                    rows.append(row)
                index += 1
            if rows:
                columns = max(len(row) for row in rows)
                table = document.add_table(rows=0, cols=columns)
                table.style = "Table Grid"
                for row_index, row in enumerate(rows):
                    cells = table.add_row().cells
                    for column in range(columns):
                        value = row[column] if column < len(row) else ""
                        paragraph = cells[column].paragraphs[0]
                        inline_runs(paragraph, value, code_font)
                        for run in paragraph.runs:
                            run.font.size = Pt(9)
                            if row_index == 0:
                                run.bold = True
                document.add_paragraph()
            continue
        heading = re.match(r"^(#{1,3})\s+(.*)$", stripped)
        if heading:
            level = len(heading.group(1))
            if level == 1:
                title = document.add_paragraph()
                run = title.add_run(heading.group(2))
                run.bold = True
                run.font.size = Pt(20)
                run.font.name = body_font
            else:
                document.add_heading(heading.group(2), level=level - 1)
            index += 1
            continue
        bullet = re.match(r"^[-*]\s+(.*)$", stripped)
        if bullet:
            paragraph = document.add_paragraph(style="List Bullet")
            inline_runs(paragraph, bullet.group(1), code_font)
            index += 1
            continue
        numbered = re.match(r"^\d+\.\s+(.*)$", stripped)
        if numbered:
            paragraph = document.add_paragraph(style="List Number")
            inline_runs(paragraph, numbered.group(1), code_font)
            index += 1
            continue
        paragraph = document.add_paragraph()
        inline_runs(paragraph, stripped, code_font)
        for run in paragraph.runs:
            if run.font.name != code_font:
                run.font.name = body_font
        index += 1


def build_docx(markdown_path: pathlib.Path, docx_path: pathlib.Path) -> None:
    from docx import Document
    from docx.shared import Pt

    document = Document()
    style = document.styles["Normal"]
    style.font.name = "Arial"
    style.font.size = Pt(10.5)
    render_markdown(markdown_path.read_text(encoding="utf-8"), document, "Arial", "Consolas")
    docx_path.parent.mkdir(parents=True, exist_ok=True)
    document.save(docx_path)


def build_pdf(docx_path: pathlib.Path, pdf_path: pathlib.Path) -> int:
    pdf_path.parent.mkdir(parents=True, exist_ok=True)
    if pdf_path.is_file():
        pdf_path.unlink()
    if not NODE.is_file() or not LIBREOFFICE_CLI.is_file():
        print("pdf export skipped: LibreOffice Kit is not installed")
        return 1
    command = [str(NODE), str(LIBREOFFICE_CLI), "convert", "--input", str(docx_path), "--output", str(pdf_path)]
    result = subprocess.run(command, check=False)
    if result.returncode != 0 or not pdf_path.is_file() or pdf_path.stat().st_size == 0:
        print(f"pdf export failed: {docx_path}")
        return 1
    with pdf_path.open("rb") as handle:
        if handle.read(5) != b"%PDF-":
            print(f"pdf signature missing: {pdf_path}")
            return 1
    return 0


def render_one(markdown_path: pathlib.Path, docx_path: pathlib.Path, pdf_path: pathlib.Path,
               djvu_path: pathlib.Path) -> int:
    print(f"docx {docx_path.relative_to(ROOT)}")
    build_docx(markdown_path, docx_path)
    if build_pdf(docx_path, pdf_path) != 0:
        return 1
    print(f"pdf  {pdf_path.relative_to(ROOT)}")
    docconvert.BuildDjvu(pdf_path, djvu_path)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Render the Markdown guides to DOCX and PDF")
    parser.add_argument("--input", help="Markdown source")
    parser.add_argument("--docx", help="DOCX destination")
    parser.add_argument("--pdf", help="PDF destination")
    parser.add_argument("--all", action="store_true", help="render every guide in Documentation/md")
    arguments = parser.parse_args()
    if arguments.all:
        failures = 0
        for language, (source, stem) in GUIDES.items():
            failures += render_one(
                DOC_ROOT / "md" / language / source,
                DOC_ROOT / "docx" / language / f"{stem}.docx",
                DOC_ROOT / "pdf" / language / f"{stem}.pdf",
                DOC_ROOT / "djvu" / language / f"{stem}.djvu",
            )
        return 1 if failures else 0
    if not arguments.input or not arguments.docx:
        parser.error("--input and --docx are required unless --all is used")
    pdf_path = pathlib.Path(arguments.pdf) if arguments.pdf else None
    if pdf_path is None:
        build_docx(pathlib.Path(arguments.input), pathlib.Path(arguments.docx))
        return 0
    djvu_path = pdf_path.with_suffix(".djvu")
    return render_one(pathlib.Path(arguments.input), pathlib.Path(arguments.docx), pdf_path, djvu_path)


if __name__ == "__main__":
    raise SystemExit(main())
