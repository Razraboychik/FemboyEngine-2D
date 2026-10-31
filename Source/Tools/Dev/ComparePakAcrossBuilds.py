#!/usr/bin/env python3
"""Compares the asset cooker output of two independent builds.

Two builds of the same revision must cook the same bytes. This script runs each
build's `fby_fempak` on an identical tree (under the workspace, because the tool
cannot write to the OS temp directory) and compares the SHA-256 of the archives.

    python Source/Tools/Dev/ComparePakAcrossBuilds.py \
        --first build/bin/tools/fby_fempak --second build-repro/bin/tools/fby_fempak
"""

from __future__ import annotations

import argparse
import hashlib
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


def cook(tool: pathlib.Path, root: pathlib.Path, output: pathlib.Path) -> str:
    result = subprocess.run(
        [str(tool), "pack", "--root", str(root), "--out", str(output)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"{tool} failed:\n{result.stdout}{result.stderr}")
    return digest(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--first", required=True, help="first build's fby_fempak")
    parser.add_argument("--second", required=True, help="second build's fby_fempak")
    parser.add_argument("--workdir", default="build", help="parent directory for the scratch tree")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    first = pathlib.Path(args.first)
    second = pathlib.Path(args.second)
    missing = [str(tool) for tool in (first, second) if not tool.exists()]
    if missing:
        print(f"pak across builds: tool not found: {', '.join(missing)}", file=sys.stderr)
        return 1

    parent = pathlib.Path(args.workdir)
    parent.mkdir(parents=True, exist_ok=True)
    workdir = pathlib.Path(tempfile.mkdtemp(prefix="pak_compare_", dir=parent))
    try:
        root = workdir / "assets"
        root.mkdir()
        (root / "a.txt").write_text("cross-build pak payload\n", encoding="utf-8")
        (root / "nested").mkdir()
        (root / "nested" / "b.txt").write_text("second entry\n", encoding="utf-8")

        first_hash = cook(first, root, workdir / "first.fempak")
        second_hash = cook(second, root, workdir / "second.fempak")
        if first_hash != second_hash:
            print(
                f"pak across builds: mismatch ({first_hash[:16]} != {second_hash[:16]})",
                file=sys.stderr,
            )
            return 1
        print(f"pak across builds: {first_hash[:16]} from both toolchains")
        return 0
    except RuntimeError as error:
        print(f"pak across builds: {error}", file=sys.stderr)
        return 1
    finally:
        if args.keep:
            print(f"pak across builds: kept {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
