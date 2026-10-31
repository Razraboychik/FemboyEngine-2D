#!/usr/bin/env python3
"""Release checklist for a cooked FemboyEngine build.

Collects the required artifacts, hashes them with SHA-256 and writes a
manifest. A missing artifact (or a forbidden file, such as the retired feature
ledger) fails the run, so a release candidate cannot be assembled from a stale
or partial build.

    python Source/Tools/Dev/ReleaseChecklist.py \
        --artifacts build/bin --require fby_editor.exe --require tools/fby_fempak.exe \
        --forbid Documentation/FEATURE_NOTES.md --version 0.1.0-alpha.1 \
        --out build/release-manifest.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[3]


def digest(path: pathlib.Path) -> str:
    hash_object = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            hash_object.update(chunk)
    return hash_object.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifacts", action="append", default=[], help="directory containing build outputs")
    parser.add_argument("--require", action="append", default=[], help="artifact path relative to a root")
    parser.add_argument("--forbid", action="append", default=[], help="path that must not exist")
    parser.add_argument("--version", default="0.1.0-alpha.1")
    parser.add_argument("--out", default="release-manifest.json")
    args = parser.parse_args()

    roots = [pathlib.Path(item) for item in args.artifacts] or [pathlib.Path("build/bin")]
    failures: list[str] = []
    entries: list[dict[str, object]] = []

    for required in args.require:
        found = None
        for root in roots:
            candidate = root / required
            if candidate.exists():
                found = candidate
                break
        if found is None:
            failures.append(f"missing artifact: {required}")
            continue
        entries.append({"path": str(found), "bytes": found.stat().st_size, "sha256": digest(found)})

    for forbidden in args.forbid:
        candidate = ROOT / forbidden
        if candidate.exists():
            failures.append(f"forbidden file still present: {forbidden}")

    if failures:
        for message in failures:
            print(f"release checklist: {message}", file=sys.stderr)
        return 1

    manifest = {
        "version": args.version,
        "artifacts": entries,
        "forbidden_checked": args.forbid,
    }
    output = pathlib.Path(args.out)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"release checklist: {len(entries)} artifact(s) hashed -> {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
