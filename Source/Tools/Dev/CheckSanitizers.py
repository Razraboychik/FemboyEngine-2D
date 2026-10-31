#!/usr/bin/env python3
"""FemboyEngine2D sanitizer gate.

Configures a separate build tree with AddressSanitizer, builds it and runs the
text stack plus the engine unit tests under the sanitizer runtime:

  * Windows: the dynamic ASan runtime from the clang distribution. Leak
    detection is not available there, so ASAN_OPTIONS carries detect_leaks=0.
  * Linux and macOS: the same AddressSanitizer plus LeakSanitizer, which reports
    leaks at process exit. LSAN_OPTIONS keeps the suppression file optional.

The script is dependency free on purpose: it runs on a bare build agent.

  python Source/Tools/Dev/CheckSanitizers.py --filter "text|font"
  python Source/Tools/Dev/CheckSanitizers.py --full
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
DEFAULT_BUILD = ROOT / "build-asan"
# The text stack and the unit tests; the GUI-backed suites need a desktop
# session and are covered by the interactive gate.
DEFAULT_FILTER = (
    "^(bigint|decimal|script_bigint|text|text_render|text_format|text_markup|text_asset_format|text_asset_file|"
    "font_builder|text_shaper|text_shape_cache|text_shape_system|font_paging|text_coverage|text_hot_path|"
    "text_golden|notices|definitions|logboy)$"
)
# Tests that the sanitizer gate builds and runs by default. Building only these
# targets keeps the gate off the editor and the pipeline tools, which mix plain
# static libraries with sanitized objects; a full-build run covers them on Linux.
TEXT_TARGETS = [
    "fby_text_test",
    "fby_text_render_test",
    "fby_text_format_test",
    "fby_text_markup_test",
    "fby_text_asset_format_test",
    "fby_text_asset_file_test",
    "fby_font_builder_test",
    "fby_text_shaper_test",
    "fby_text_shape_cache_test",
    "fby_text_shape_system_test",
    "fby_font_paging_test",
    "fby_text_coverage_test",
    "fby_text_hot_path_test",
    "fby_text_golden_test",
    "fby_bigint_test",
    "fby_decimal_test",
    "fby_script_bigint_test",
    "fby_notices_test",
    "fby_definitions_test",
    "fby_logboy_test",
]


def run(command: list[str], env: dict[str, str]) -> int:
    print("+ " + " ".join(command), flush=True)
    return subprocess.call(command, cwd=str(ROOT), env=env)


def main() -> int:
    parser = argparse.ArgumentParser(description="AddressSanitizer and LeakSanitizer gate")
    parser.add_argument("--build-dir", default=str(DEFAULT_BUILD))
    parser.add_argument("--config", default="RelWithDebInfo")
    parser.add_argument("--filter", default=DEFAULT_FILTER)
    parser.add_argument("--full", action="store_true", help="run the whole suite instead of the filter")
    parser.add_argument("--no-editor", action="store_true", help="skip the editor targets")
    parser.add_argument("--clean", action="store_true", help="remove the sanitizer build tree first")
    parser.add_argument("--full-build", action="store_true",
                        help="build every target instead of only the text and unit test targets")
    arguments = parser.parse_args()

    build_dir = pathlib.Path(arguments.build_dir)
    if arguments.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    windows = sys.platform.startswith("win")
    environment = dict(os.environ)
    if windows:
        environment["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1:allocator_may_return_null=1"
    else:
        environment["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=1:allocator_may_return_null=1"
        environment["LSAN_OPTIONS"] = "detect_leaks=1:report_objects=1"

    configure = [
        "cmake",
        "-S", str(ROOT),
        "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={arguments.config}",
        "-DFBY_BUILD_TESTS=ON",
        "-DFBY_WITH_ASAN=ON",
    ]
    if windows:
        configure.append("-DCMAKE_C_COMPILER=clang-cl")
        configure.append("-DCMAKE_CXX_COMPILER=clang-cl")
    if arguments.no_editor:
        configure.append("-DFBY_BUILD_EDITOR=OFF")
    if run(configure, environment) != 0:
        print("sanitizer gate: configure failed")
        return 1
    build = ["cmake", "--build", str(build_dir), "--parallel", "8"]
    if not arguments.full_build:
        build += ["--target", *TEXT_TARGETS]
    if run(build, environment) != 0:
        print("sanitizer gate: build failed")
        return 1

    test = ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "--timeout", "300"]
    if not arguments.full:
        test += ["-R", arguments.filter]
    if run(test, environment) != 0:
        print("sanitizer gate: tests failed")
        return 1
    print("sanitizer gate: all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
