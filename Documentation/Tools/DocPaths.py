#!/usr/bin/env python3
"""Shared output layout for the generated documentation set.

Every generator writes below Documentation/ only, split by format and language:

    Documentation/md/{en,ru}
    Documentation/docx/{en,ru}
    Documentation/pdf/{en,ru}
    Documentation/djvu/{en,ru}

Each language is a complete, independent edition. There is no combined bilingual
output. Nothing is written to the Documentation root except README.md and the
source documents that live there. PDF transcripts go to the OS temp directory,
never into the tree.

The DjVu edition is converted from the PDF, so it exists only where the PDF step
ran and a converter is installed; see DocumentConvert.
"""

from __future__ import annotations

import pathlib
import sys

sys.dont_write_bytecode = True

DOCUMENTATION = pathlib.Path(__file__).resolve().parents[1]
ROOT = DOCUMENTATION.parent

MD_ROOT = DOCUMENTATION / "md"
DOCX_ROOT = DOCUMENTATION / "docx"
PDF_ROOT = DOCUMENTATION / "pdf"
DJVU_ROOT = DOCUMENTATION / "djvu"

LANGUAGES = ("en", "ru")


def EnsureLayout() -> None:
    """Creates the format/language tree. Safe to call from every generator."""
    for root in (MD_ROOT, DOCX_ROOT, PDF_ROOT, DJVU_ROOT):
        root.mkdir(parents=True, exist_ok=True)
        for language in LANGUAGES:
            (root / language).mkdir(parents=True, exist_ok=True)


def _Path(root: pathlib.Path, language: str, name: str) -> pathlib.Path:
    if language not in LANGUAGES:
        raise ValueError(f"unsupported documentation language: {language}")
    return root / language / name


def Md(language: str, name: str) -> pathlib.Path:
    return _Path(MD_ROOT, language, name)


def Docx(language: str, name: str) -> pathlib.Path:
    return _Path(DOCX_ROOT, language, name)


def Pdf(language: str, name: str) -> pathlib.Path:
    return _Path(PDF_ROOT, language, name)


def Djvu(language: str, name: str) -> pathlib.Path:
    return _Path(DJVU_ROOT, language, name)
