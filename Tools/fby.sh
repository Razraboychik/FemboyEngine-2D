#!/usr/bin/env bash
# FemboyEngine2D developer entry point. Build trees live under build/<config>.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
action="${1:-build}"
preset="${2:-dev}"
case "$preset" in
    dev) config="dev-clang"; dir="build/dev" ;;
    release) config="release-clang"; dir="build/release" ;;
    clangcl) config="dev-clangcl"; dir="build/clangcl" ;;
    ci) config="ci-clangcl"; dir="build/ci" ;;
    windows) config="windows-release"; dir="build/windows-release" ;;
    linux) config="linux-dev"; dir="build/linux-dev" ;;
    macos) config="macos-dev"; dir="build/macos-dev" ;;
    *) echo "unknown preset: $preset" >&2; exit 2 ;;
esac
cd "$root"
case "$action" in
    configure) cmake --preset "$config" ;;
    build) cmake --preset "$config"; cmake --build --preset "$config" ;;
    test) cmake --preset "$config"; cmake --build --preset "$config"; ctest --preset "$config" ;;
    run) "$dir/bin/fby_editor" ;;
    clean) rm -rf build ;;
    *) echo "unknown action: $action" >&2; exit 2 ;;
esac
