#!/usr/bin/env bash
# FemboyEngine2D 0.1.0-alpha.1 - fast syntax gate for one translation unit.
#
# The engine's real build is a single Ninja graph in build/. Running a full
# `ninja` from several agents at once would contend for the same object files,
# so each module is checked with `-fsyntax-only` using exactly the flags CMake
# passes to the engine library. The Lead owns the full link/test build.
#
# Usage:
#   Source/Tools/Dev/CheckTranslationUnit.sh Source/Engine/Script/Vm.cpp
#   Source/Tools/Dev/CheckTranslationUnit.sh Source/Tests/ScriptVmTest.cpp --with-tests -- -Iw/e/x/tra
#
# Extra include directories can be passed after `--`, or provided in
# FBY_EXTRA_INCLUDES (space separated, Windows-style paths).
set -uo pipefail

# Git-bash rewrites anything that looks like a POSIX path into C:/..., which
# would turn /W4 and /std:c++latest into linker inputs. clang-cl flags must be
# passed through verbatim.
export MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLANG_CL="${FBY_CLANG_CL:-}"
if [[ -z "$CLANG_CL" ]]; then
    CLANG_CL="$(command -v clang-cl || true)"
fi
if [[ -z "$CLANG_CL" || ! -x "$CLANG_CL" ]]; then
    echo "clang-cl not found; set FBY_CLANG_CL" >&2
    exit 2
fi

SOURCE="${1:?usage: check_tu.sh <source.cpp> [--with-tests] [-- extra includes]}"
shift || true
EXTRA=()
WITH_TESTS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --with-tests) WITH_TESTS=1 ;;
        --) shift; EXTRA+=("$@"); break ;;
        *) EXTRA+=("$1") ;;
    esac
    shift
done

W() { cygpath -w "$1" 2>/dev/null || echo "$1"; }
ROOTW="$(W "$ROOT")"
BUILDW="$(W "$ROOT/build")"

INCLUDES=(
    "-I${ROOTW}/engine/include"
    "-I${BUILDW}/generated"
    "-I${BUILDW}/_deps/sdl3-src/include"
    "-I${BUILDW}/_deps/sdl3-build/include-revision"
    "-I${ROOTW}/third_party/box2d-main/box2d-main/include"
    "-I${BUILDW}/third_party/box2d/src"
    "-I${ROOTW}/third_party/flatbuffers-master/flatbuffers-master/include"
    "-I${BUILDW}/_deps/vulkanheaders-src/include"
    "-I${BUILDW}/_deps/glslang-src"
    "-I${BUILDW}/_deps/glslang-build"
    "-I${ROOTW}/third_party/opus-master/opus-main/include"
    "-I${ROOTW}/third_party/stb"
    "-I${ROOTW}/engine/tools"
    "-imsvc${ROOTW}/third_party/entt-main/entt-main/src"
    "-imsvc${ROOTW}/third_party/tomlplusplus-master/tomlplusplus-master"
    "-imsvc${ROOTW}/third_party/miniaudio-master/miniaudio-master"
)

if [[ $WITH_TESTS -eq 1 ]]; then
    INCLUDES+=(
        "-I${ROOTW}/third_party/Catch2-master/Catch2-master/src"
    )
fi

DEFINES=(
    -DB2_ENABLE_ASSERT
    -DFBY_ENGINE_VERSION=\"0.1.0-alpha.1\"
    -DFBY_PLATFORM_NAME=\"Windows\"
    -DFBY_HAS_VULKAN_HEADERS=1
    -DNOMINMAX -DTOML_ENABLE_FORMATTERS=0
    -DUNICODE -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -D_UNICODE
)
if [[ -n "${FBY_HAS_VULKAN_HEADERS:-}" ]]; then
    DEFINES+=(-DFBY_HAS_VULKAN_HEADERS=1)
fi

FLAGS=(
    /EHsc /std:c++latest /W4
    -Wno-unused-parameter -Wshadow -Wno-microsoft-enum-value
    -Wno-c++98-compat -Wno-c++98-compat-pedantic
    -Wno-unsafe-buffer-usage -Wno-unsafe-buffer-usage-in-libc-call
    -Wno-padded -Wno-ctad-maybe-unsupported -Wno-nrvo
    -fsyntax-only
)

if [[ -n "${FBY_EXTRA_INCLUDES:-}" ]]; then
    for inc in $FBY_EXTRA_INCLUDES; do
        EXTRA+=("-I$(W "$inc")")
    done
fi

"$CLANG_CL" "${DEFINES[@]}" "${FLAGS[@]}" "${INCLUDES[@]}" "${EXTRA[@]}" "$(W "$SOURCE")"
STATUS=$?
if [[ $STATUS -eq 0 ]]; then
    echo "OK  $SOURCE"
else
    echo "FAIL($STATUS)  $SOURCE"
fi
exit $STATUS
