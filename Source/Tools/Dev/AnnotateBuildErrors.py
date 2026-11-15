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

ERROR_MARKERS = ("error:", "FAILED:", "CMake Error", "fatal error:", "undefined reference")
MAX_ANNOTATIONS = 30


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
    emitted = 0
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or not any(marker in stripped for marker in ERROR_MARKERS):
            continue
        # A bare "error:" inside a command echo is noise; keep real compiler
        # diagnostics and linker failures.
        match = re.search(r"error:|fatal error:|FAILED:|CMake Error|undefined reference", stripped)
        if match is None:
            continue
        print(f"::error::{escape(stripped[:900])}")
        emitted += 1
        if emitted >= args.limit:
            break

    if emitted == 0:
        print("::error::build failed without a recognizable compiler error; see the job log")
    return 0


if __name__ == "__main__":
    sys.exit(main())
