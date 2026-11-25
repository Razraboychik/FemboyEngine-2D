#!/bin/bash
# FemboyEngine 2.0 - macOS dependency setup (all components).
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/Setup.command" "$@"
