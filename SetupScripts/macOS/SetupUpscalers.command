#!/bin/bash
# FemboyEngine 2.0 - optional DLSS/FidelityFX/XeSS SDK setup for macOS.
set -euo pipefail
exec "$(cd "$(dirname "$0")/../Linux" && pwd)/SetupUpscalers.sh" "$@"
