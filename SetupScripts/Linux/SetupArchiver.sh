#!/usr/bin/env bash
# FemboyEngine 2.0 - 7-Zip archiver setup for Linux.
set -euo pipefail
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/Setup.sh" --component archiver "$@"
