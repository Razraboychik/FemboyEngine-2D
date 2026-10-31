#!/usr/bin/env python3
"""Checks that the asset cooker produces byte-identical archives.

The reproducible-build goal needs two builds to agree, but the cheaper
invariant is that one build converges: the same inputs, with different file
modification times and in a different directory, must produce the same
`.fempak` bytes. This script runs `fby_fempak pack` twice and compares SHA-256.

    python Source/Tools/Dev/CheckPakReproducible.py --tool build/bin/tools/fby_fempak
"""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def digest(path: pathlib.Path) -> str:
    hash_object = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            hash_object.update(chunk)
    return hash_object.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", default="build/bin/tools/fby_fempak", help="path to fby_fempak")
    parser.add_argument("--workdir", default="build", help="parent directory for the scratch tree")
    parser.add_argument("--keep", action="store_true", help="keep the temporary directory")
    args = parser.parse_args()

    tool = pathlib.Path(args.tool)
    if not tool.exists():
        print(f"pak reproducibility: tool not found: {tool}", file=sys.stderr)
        return 1

    parent = pathlib.Path(args.workdir)
    parent.mkdir(parents=True, exist_ok=True)
    workdir = pathlib.Path(tempfile.mkdtemp(prefix="pak_repro_", dir=parent))
    try:
        root = workdir / "assets"
        root.mkdir()
        (root / "a.txt").write_text("deterministic pak payload\n", encoding="utf-8")
        (root / "nested").mkdir()
        (root / "nested" / "b.txt").write_text("second entry\n", encoding="utf-8")

        outputs = []
        for run in range(2):
            # Different mtimes must not leak into the archive.
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    os.utime(path, (1_600_000_000 + run, 1_600_000_000 + run))
            archive = workdir / f"run{run}.fempak"
            result = subprocess.run(
                [str(tool), "pack", "--root", str(root), "--out", str(archive)],
                capture_output=True,
                text=True,
                check=False,
            )
            if result.returncode != 0:
                print(f"pak reproducibility: pack run {run} failed:\n{result.stdout}{result.stderr}", file=sys.stderr)
                return 1
            outputs.append(archive)

        first = digest(outputs[0])
        second = digest(outputs[1])
        if first != second:
            print(f"pak reproducibility: hashes differ ({first[:16]} != {second[:16]})", file=sys.stderr)
            return 1
        print(f"pak reproducibility: {first[:16]} for both runs")
        return 0
    finally:
        if args.keep:
            print(f"pak reproducibility: kept {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
