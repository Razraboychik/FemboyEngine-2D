#!/usr/bin/env python3
"""Turns build output into GitHub annotations.

The CI build steps tee their output to ``build.log``; when a build fails this
script lifts the first errors into ``::error::`` workflow commands, which GitHub
stores as check-run annotations. Annotations are readable anonymously, unlike
the job logs, so a failure can be diagnosed without repository admin rights.

The script never fails the step: a missing log is reported as an annotation and
the step exits successfully so the original failure stays the job's conclusion.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ERROR_MARKERS = (
    "error:",
    "error C",
    "error LNK",
    "fatal error:",
    "fatal error C",
    "fatal error LNK",
    "FAILED:",
    "CMake Error",
    "undefined reference",
    "ERROR: AddressSanitizer",
    "ERROR: LeakSanitizer",
    "SUMMARY:",
    "***Failed",
    "The following tests FAILED",
    "FAIL ",
    "Assertion",
    "Exception",
    "Access violation",
    "Timeout",
    "timeout",
    "Segmentation",
)
MAX_ANNOTATIONS = 10


def escape(text: str) -> str:
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", default="build.log")
    parser.add_argument("--limit", type=int, default=MAX_ANNOTATIONS)
    args = parser.parse_args()

    root = pathlib.Path(args.log)
    if not root.is_file():
        print(f"::error::build log {args.log} was not produced")
        return 0

    text = root.read_text(encoding="utf-8", errors="replace")
    candidates = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or not any(marker in stripped for marker in ERROR_MARKERS):
            continue
        candidates.append(stripped)
    candidates = list(dict.fromkeys(candidates))

    # Compiler diagnostics and assertion lines explain the failure; ctest's
    # "***Failed" summary only names the test. Annotate the explanations first.
    primary = [
        line
        for line in candidates
        if "error:" in line or "FAIL " in line or "Assertion" in line or "fatal error" in line
    ]
    secondary = [line for line in candidates if line not in primary]
    emitted = 0
    for line in (primary + secondary)[: args.limit]:
        print(f"::error::{escape(line[:900])}")
        emitted += 1

    if emitted == 0:
        print("::error::build failed without a recognizable compiler error; see the job log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
