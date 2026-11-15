"""Single source for the public documentation version.

The engine version itself lives in Version.txt at the repository root; this
module reads it so the documentation generators never carry a second copy.
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[2]
VERSION = (ROOT / "Version.txt").read_text(encoding="utf-8").strip()

# The release status follows the SemVer pre-release suffix: a bare version is a
# release, and an -alpha.N, -beta.N or -rc.N suffix names the matching phase.
STATUS = "Release"
Match = re.search(r"-(alpha|beta|rc)\.\d+$", VERSION)
if Match:
    STATUS = {"alpha": "Alpha", "beta": "Beta", "rc": "Release Candidate"}[Match.group(1)]
