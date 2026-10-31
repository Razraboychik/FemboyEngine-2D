#!/usr/bin/env python3
"""Compare a live noise-c XX run with the vendored reference vector.

The noise-differential CI job builds the pinned noise-c checkout, runs
NoiseCVectorDriver.c through that library and pipes its output here. Every
handshake message, payload, the handshake hash and the three transport
ciphertexts must match the constants in
Source/Tests/Vectors/NoiseXxChaChaPolySha256Vector.hpp byte for byte.

Usage:
  CompareNoiseCVector.py --write-input FILE
  CompareNoiseCVector.py --compare FILE
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
HEADER = ROOT / "Source" / "Tests" / "Vectors" / "NoiseXxChaChaPolySha256Vector.hpp"
PROTOCOL_NAME = "Noise_XX_25519_ChaChaPoly_SHA256"

STRING_CONSTANT = re.compile(r'inline constexpr char (\w+)\[\] = "([^"]*)";')

INPUT_FIELDS = [
    ("init_static", "kInitiatorStatic"),
    ("init_ephemeral", "kInitiatorEphemeral"),
    ("resp_static", "kResponderStatic"),
    ("resp_ephemeral", "kResponderEphemeral"),
    ("prologue", "kInitiatorPrologue"),
    ("payload1", "kHandshake1Payload"),
    ("payload2", "kHandshake2Payload"),
    ("payload3", "kHandshake3Payload"),
    ("payload4", "kTransport1Payload"),
    ("payload5", "kTransport2Payload"),
    ("payload6", "kTransport3Payload"),
]

EXPECTED_FIELDS = [
    ("msg1", "kHandshake1"),
    ("payload1", "kHandshake1Payload"),
    ("msg2", "kHandshake2"),
    ("payload2", "kHandshake2Payload"),
    ("msg3", "kHandshake3"),
    ("payload3", "kHandshake3Payload"),
    ("hash", "kHandshakeHash"),
    ("t1", "kTransport1"),
    ("t2", "kTransport2"),
    ("t3", "kTransport3"),
]


def load_constants() -> dict[str, str]:
    """Reads the vendored header and returns its string constants."""
    text = HEADER.read_text(encoding="utf-8")
    raw = {name: value for name, value in STRING_CONSTANT.findall(text)}
    if raw.get("kVectorName") != PROTOCOL_NAME:
        raise SystemExit(f"vector header names {raw.get('kVectorName')!r}, expected {PROTOCOL_NAME!r}")
    return {name: value.lower() for name, value in raw.items()}


def write_input(path: pathlib.Path) -> None:
    values = load_constants()
    lines = []
    for field, constant in INPUT_FIELDS:
        if constant not in values:
            raise SystemExit(f"vector header is missing {constant}")
        lines.append(f"{field}={values[constant]}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {path} ({len(lines)} fields)")


def read_output(path: pathlib.Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or "=" not in line:
            continue
        name, value = line.split("=", 1)
        entries[name.strip()] = value.strip().lower()
    return entries


def compare(path: pathlib.Path) -> int:
    values = load_constants()
    observed = read_output(path)
    failures = 0
    for field, constant in EXPECTED_FIELDS:
        expected = values.get(constant)
        if expected is None:
            print(f"FAIL vector header is missing {constant}")
            failures += 1
            continue
        actual = observed.get(field)
        if actual is None:
            print(f"FAIL noise-c output has no {field}")
            failures += 1
            continue
        if actual != expected:
            print(f"FAIL {field}: noise-c={actual}")
            print(f"     vector={expected}")
            failures += 1
    if failures:
        print(f"noise differential: {failures} mismatch(es)")
        return 1
    print("noise differential: noise-c matches the vendored vector")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-input", type=pathlib.Path, metavar="FILE")
    parser.add_argument("--compare", type=pathlib.Path, metavar="FILE")
    arguments = parser.parse_args()
    if arguments.write_input is not None:
        write_input(arguments.write_input)
        return 0
    if arguments.compare is not None:
        return compare(arguments.compare)
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
