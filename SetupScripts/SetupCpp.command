#!/bin/bash
# FemboyEngine 2.0 - C++ toolchain and source dependencies for macOS.
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/Setup.sh" --component cpp "$@"
