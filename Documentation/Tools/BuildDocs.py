#!/usr/bin/env python3
"""Runs the whole documentation set in the order that README documents.

The set has one generator per product plus two verifiers. Running them by hand in
the right order is easy to get wrong, because the first generator owns the shared
format/language layout and the reference manuals must be rendered before the
check. This script is a thin orchestrator: it builds the command line for each
existing generator, forwards the common flags and runs CheckDocs at the end. It
contains no conversion logic of its own.

Order (matches Documentation/README.md):

    GenerateDocumentation.py            architecture, the base of the set
    GenerateApplicationGuide.py         task-oriented walkthrough
    GenerateAPIReference.py             reference subset
    GenerateCompleteReference.py        the normative book
    GenerateSupplementaryDocs.py        CMake and FemboyScript manuals
    GenerateProjectSettingsRecommendations.py  project settings advice
    MarkdownDocs.py --all               Getting Started
    GenerateReferenceDocs.py            viewport modes and editor settings
    CheckDocs.py                        links, language pairs, layout

Usage:
  python Documentation/Tools/BuildDocs.py
  python Documentation/Tools/BuildDocs.py --skip-pdf
  python Documentation/Tools/BuildDocs.py --node <node.exe> --lo-cli <cli.js>

`--skip-pdf` is forwarded to every generator that accepts it. `--node` and
`--lo-cli` are forwarded the same way and are also exported as FBY_NODE and
FBY_LO_CLI, because MarkdownDocs and GenerateProjectSettingsRecommendations read
the environment instead of taking the flags.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys

sys.dont_write_bytecode = True

RootPath = pathlib.Path(__file__).resolve().parents[2]
ToolsPath = pathlib.Path(__file__).resolve().parent

# Node and the LibreOffice Kit CLI follow the FBY_NODE and FBY_LO_CLI convention.
NodeDefault = os.environ.get("FBY_NODE", "")
LoCliDefault = os.environ.get("FBY_LO_CLI", "")

# One entry per step: the script, its fixed extra arguments and whether it accepts
# the shared PDF/node flags. GenerateProjectSettingsRecommendations has no command
# line at all, and MarkdownDocs only takes --all; both read the environment.
Steps = (
    {"Script": "GenerateDocumentation.py", "PdfFlags": True},
    {"Script": "GenerateApplicationGuide.py", "PdfFlags": True},
    {"Script": "GenerateAPIReference.py", "PdfFlags": True},
    {"Script": "GenerateCompleteReference.py", "PdfFlags": True},
    {"Script": "GenerateSupplementaryDocs.py", "PdfFlags": True},
    {"Script": "GenerateProjectSettingsRecommendations.py", "PdfFlags": False},
    {"Script": "MarkdownDocs.py", "PdfFlags": False, "Extra": ("--all",)},
    {"Script": "GenerateReferenceDocs.py", "PdfFlags": True},
    {"Script": "CheckDocs.py", "PdfFlags": False},
)


def BuildCommand(InStep: dict, InArguments) -> list[str]:
    """Returns the command line for one generator."""
    ScopedCommand = [sys.executable, str(ToolsPath / InStep["Script"])]
    ScopedCommand.extend(InStep.get("Extra", ()))
    if InStep["PdfFlags"]:
        if InArguments.SkipPdf:
            ScopedCommand.append("--skip-pdf")
        if InArguments.Node:
            ScopedCommand.extend(["--node", InArguments.Node])
        if InArguments.LoCli:
            ScopedCommand.extend(["--lo-cli", InArguments.LoCli])
    return ScopedCommand


def BuildEnvironment(InArguments) -> dict[str, str]:
    """Returns the process environment with the tool paths exported."""
    ScopedEnvironment = dict(os.environ)
    if InArguments.Node:
        ScopedEnvironment["FBY_NODE"] = InArguments.Node
    if InArguments.LoCli:
        ScopedEnvironment["FBY_LO_CLI"] = InArguments.LoCli
    return ScopedEnvironment


def RunStep(InStep: dict, InArguments) -> int:
    """Runs one generator and returns its exit code."""
    ScopedCommand = BuildCommand(InStep, InArguments)
    print(f"== {InStep['Script']}")
    print(f"   {' '.join(ScopedCommand)}")
    ScopedResult = subprocess.run(
        ScopedCommand,
        cwd=str(RootPath),
        env=BuildEnvironment(InArguments),
        check=False,
    )
    if ScopedResult.returncode != 0:
        print(f"   failed with exit code {ScopedResult.returncode}", file=sys.stderr)
    return ScopedResult.returncode


def Main() -> int:
    """Entry point: run every step in order and report the failures."""
    parser = argparse.ArgumentParser(description="Regenerate and verify the documentation set")
    parser.add_argument(
        "--skip-pdf",
        dest="SkipPdf",
        action="store_true",
        help="write Markdown and DOCX only; skips PDF and DjVu",
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
    ScopedArguments = parser.parse_args()

    ScopedFailed: list[str] = []
    for ScopedStep in Steps:
        if RunStep(ScopedStep, ScopedArguments) != 0:
            ScopedFailed.append(ScopedStep["Script"])

    if ScopedFailed:
        print(f"docs build: {len(ScopedFailed)} step(s) failed: {', '.join(ScopedFailed)}")
        return 1
    print("docs build: every step passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(Main())
