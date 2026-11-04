#!/bin/bash
# FemboyEngine 2.0 - 7-Zip archiver setup for macOS.
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/Setup.sh" --component archiver "$@"
