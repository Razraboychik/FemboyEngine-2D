#!/usr/bin/env bash
# FemboyEngine 2.0 - C++ toolchain and source dependencies for Linux.
set -euo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/Setup.sh" --component cpp "$@"
