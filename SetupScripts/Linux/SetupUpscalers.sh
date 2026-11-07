#!/usr/bin/env bash
# FemboyEngine 2.0 - optional vendor SDK setup for upscaling and frame generation.
#
# Use this script only when a DLSS or FidelityFX backend is required. The SDKs
# are vendor-licensed and are not part of the engine source tree.
#
#   ./SetupUpscalers.sh                       # verify default local paths
#   ./SetupUpscalers.sh --download --accept-license
#   ./SetupUpscalers.sh --only fidelityfx --download --accept-license
#
# The default targets are third_party/dlss, third_party/fidelityfx/Kits/FidelityFX
# and third_party/xess. FBY_DLSS_ROOT, FBY_FIDELITYFX_ROOT and FBY_XESS_ROOT
# override them. No SDK is copied into the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
THIRD="${ROOT}/third_party"
DLSS_ROOT="${FBY_DLSS_ROOT:-${THIRD}/dlss}"
FIDELITYFX_REPO="${FBY_FIDELITYFX_REPO:-${THIRD}/fidelityfx}"
FIDELITYFX_ROOT="${FBY_FIDELITYFX_ROOT:-${FIDELITYFX_REPO}/Kits/FidelityFX}"
XESS_ROOT="${FBY_XESS_ROOT:-${THIRD}/xess}"
DOWNLOAD=0
ACCEPT=0
REQUIRE=0
ONLY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --download) DOWNLOAD=1 ;;
        --accept-license) ACCEPT=1 ;;
        --require) REQUIRE=1 ;;
        --only) ONLY="${2:-}"; shift ;;
        --only=*) ONLY="${1#*=}" ;;
        --help|-h)
            sed -n '2,17p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
    shift
done

log() { printf '\n==> %s\n' "$*"; }
warn() { printf 'WARN: %s\n' "$*" >&2; }
display_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$1"
    else
        printf '%s\n' "$1"
    fi
}

validate_dlss() {
    [[ -f "${DLSS_ROOT}/include/nvsdk_ngx.h" ]]
}
validate_fidelityfx() {
    [[ -f "${FIDELITYFX_ROOT}/api/include/ffx_api.h" ]]
}
validate_xess() {
    [[ -f "${XESS_ROOT}/inc/xess/xess.h" ]]
}

if [[ "${DOWNLOAD}" -eq 1 && "${ACCEPT}" -ne 1 ]]; then
    echo "The SDK licenses must be accepted explicitly." >&2
    echo "Read the vendor terms, then re-run with --accept-license." >&2
    exit 2
fi

if [[ "${DOWNLOAD}" -eq 1 ]]; then
    if ! command -v git >/dev/null 2>&1; then
        echo "git is required to download the SDK repositories." >&2
        exit 2
    fi
    log "Vendor SDK license notice"
    cat <<'NOTICE'
NVIDIA DLSS SDK: NVIDIA RTX SDKs License.
AMD FidelityFX SDK: AMD FidelityFX license.
Intel XeSS SDK: Intel Simplified Software License.

These SDKs are binary/source distributions governed by their own terms. They
are not part of FemboyEngine2D and must not be committed to the engine
repository. Review the license files before enabling a backend.
NOTICE
fi

clone_sdk() {
    local url="$1" destination="$2"
    if [[ -d "${destination}/.git" || -f "${destination}/README.md" || -f "${destination}/readme.md" ]]; then
        echo "  present ${destination}"
        return
    fi
    echo "  cloning ${url}"
    git clone --depth 1 "${url}" "${destination}"
}

if [[ "${DOWNLOAD}" -eq 1 ]]; then
    log "Downloading vendor SDKs"
    if [[ -z "${ONLY}" || "${ONLY}" == "dlss" ]]; then
        clone_sdk "https://github.com/NVIDIA/DLSS.git" "${DLSS_ROOT}"
    fi
    if [[ -z "${ONLY}" || "${ONLY}" == "fidelityfx" ]]; then
        clone_sdk "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git" "${FIDELITYFX_REPO}"
    fi
    if [[ -z "${ONLY}" || "${ONLY}" == "xess" ]]; then
        clone_sdk "https://github.com/intel/xess.git" "${XESS_ROOT}"
    fi
fi

log "SDK layout"
MISSING=0
if validate_dlss; then
    echo "  DLSS       : ${DLSS_ROOT}"
else
    warn "DLSS not found at ${DLSS_ROOT} (expected include/nvsdk_ngx.h)"
    MISSING=$((MISSING + 1))
fi
if validate_fidelityfx; then
    echo "  FidelityFX : ${FIDELITYFX_ROOT}"
else
    warn "FidelityFX not found at ${FIDELITYFX_ROOT} (expected api/include/ffx_api.h)"
    MISSING=$((MISSING + 1))
fi
if validate_xess; then
    echo "  XeSS       : ${XESS_ROOT} (SDK retained; the engine does not compile XeSS)"
else
    warn "XeSS not found at ${XESS_ROOT} (expected inc/xess/xess.h)"
fi

cat <<CONFIGURE

CMake flags for a local SDK:
  -DFBY_WITH_DLSS=ON -DFBY_DLSS_ROOT="$(display_path "${DLSS_ROOT}")"
  -DFBY_WITH_FIDELITYFX=ON -DFBY_FIDELITYFX_ROOT="$(display_path "${FIDELITYFX_ROOT}")"

Without an SDK, leave the flags OFF. FSR1, the FSR2-compatible core path and
custom frame generation build without any vendor SDK; runtime capabilities
fall back when a backend is unavailable.
CONFIGURE

if [[ "${REQUIRE}" -eq 1 && "${MISSING}" -ne 0 ]]; then
    exit 1
fi
