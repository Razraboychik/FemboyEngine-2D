#!/usr/bin/env python3
"""Checks that fetched dependencies match cmake/FbyDependencyLock.cmake.

FetchContent clones a tag or a branch; a moved tag would silently change the
toolchain between two builds. This script reads the expected commits from the
lock file and compares them with the actual checkout under `<build>/_deps`.

    python Source/Tools/Dev/CheckDependencyLock.py --build build [--require]

Without `--require` a missing dependency directory is reported and skipped, so
the script can run on a tree that has not been configured yet.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[3]
LOCK_FILE = ROOT / "cmake" / "FbyDependencyLock.cmake"

# Lock variable suffix -> FetchContent source directory.
SOURCES = {
    "SDL3": "sdl3-src",
    "VULKAN_HEADERS": "vulkanheaders-src",
    "GLSLANG": "glslang-src",
}
# Vendored trees are pinned by content digest instead of a git commit.
VENDORED = {
    "BOX2D_TREE": "third_party/box2d-main/box2d-main",
    "OPUS_TREE": "third_party/opus-master/opus-main",
}
LOCK_RE = re.compile(r'set\(FBY_LOCK_(\w+)_COMMIT\s+"([0-9a-fA-F]+)"\)')
TREE_RE = re.compile(r'set\(FBY_LOCK_(\w+)_TREE\s+"([0-9a-fA-F]+)"\)')


def read_lock() -> dict[str, str]:
    text = LOCK_FILE.read_text(encoding="utf-8")
    lock = {name: commit.lower() for name, commit in LOCK_RE.findall(text)}
    lock.update({f"{name}_TREE": digest for name, digest in TREE_RE.findall(text)})
    return lock


def tree_digest(root: pathlib.Path) -> str:
    hash_object = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        hash_object.update(path.relative_to(root).as_posix().encode("utf-8"))
        hash_object.update(b"\0")
        hash_object.update(path.read_bytes())
    return hash_object.hexdigest()


def checkout_commit(source: pathlib.Path) -> str | None:
    git_dir = source / ".git"
    if not git_dir.exists():
        return None
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip().lower()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build", help="build directory with _deps")
    parser.add_argument("--require", action="store_true", help="fail when a dependency is missing")
    args = parser.parse_args()

    lock = read_lock()
    if not lock:
        print(f"dependency lock: no entries in {LOCK_FILE}", file=sys.stderr)
        return 1

    failures = 0
    for name, directory in SOURCES.items():
        expected = lock.get(name)
        if expected is None:
            print(f"dependency lock: {name} has no expected commit", file=sys.stderr)
            failures += 1
            continue
        source = pathlib.Path(args.build) / "_deps" / directory
        actual = checkout_commit(source)
        if actual is None:
            message = f"dependency lock: {name} is not fetched under {source}"
            if args.require:
                print(message, file=sys.stderr)
                failures += 1
            else:
                print(f"{message} (skipped)")
            continue
        if actual != expected:
            print(
                f"dependency lock: {name} is {actual}, lock expects {expected}",
                file=sys.stderr,
            )
            failures += 1
        else:
            print(f"dependency lock: {name} {actual[:12]} ok")

    for name, directory in VENDORED.items():
        expected = lock.get(name)
        if expected is None:
            print(f"dependency lock: {name} has no expected digest", file=sys.stderr)
            failures += 1
            continue
        source = ROOT / directory
        if not source.is_dir():
            message = f"dependency lock: {name} tree missing at {source}"
            if args.require:
                print(message, file=sys.stderr)
                failures += 1
            else:
                print(f"{message} (skipped)")
            continue
        actual = tree_digest(source)
        if actual != expected:
            print(f"dependency lock: {name} tree is {actual[:12]}, lock expects {expected[:12]}", file=sys.stderr)
            failures += 1
        else:
            print(f"dependency lock: {name} {actual[:12]} ok")

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
