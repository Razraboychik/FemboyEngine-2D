# FemboyEngine 2.0 - optional vendor SDK setup for DLSS, FidelityFX and XeSS.
#
# The SDKs are vendor-licensed and are not part of the engine source tree.
# Defaults are third_party\dlss, third_party\fidelityfx\Kits\FidelityFX and
# third_party\xess; FBY_DLSS_ROOT, FBY_FIDELITYFX_ROOT and FBY_XESS_ROOT override.
param(
    [switch]$Download,
    [switch]$AcceptLicense,
    [switch]$Require,
    [ValidateSet("", "dlss", "fidelityfx", "xess")]
    [string]$Only = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Third = Join-Path $Root "third_party"
$DlssRoot = if ($env:FBY_DLSS_ROOT) { $env:FBY_DLSS_ROOT } else { Join-Path $Third "dlss" }
$FidelityFxRepo = if ($env:FBY_FIDELITYFX_REPO) { $env:FBY_FIDELITYFX_REPO } else { Join-Path $Third "fidelityfx" }
$FidelityFxRoot = if ($env:FBY_FIDELITYFX_ROOT) { $env:FBY_FIDELITYFX_ROOT } else { Join-Path $FidelityFxRepo "Kits\FidelityFX" }
$XessRoot = if ($env:FBY_XESS_ROOT) { $env:FBY_XESS_ROOT } else { Join-Path $Third "xess" }

function Write-Step([string]$Text) {
    Write-Host ""
    Write-Host "==> $Text" -ForegroundColor Magenta
}

function Test-Dlss { Test-Path (Join-Path $DlssRoot "include\nvsdk_ngx.h") }
function Test-FidelityFx { Test-Path (Join-Path $FidelityFxRoot "api\include\ffx_api.h") }
function Test-Xess { Test-Path (Join-Path $XessRoot "inc\xess\xess.h") }

if ($Download -and -not $AcceptLicense) {
    Write-Error "The SDK licenses must be accepted explicitly. Review the vendor terms, then pass -AcceptLicense."
    exit 2
}
if ($Download -and -not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "git is required to download the SDK repositories."
    exit 2
}
if ($Download) {
    Write-Step "Vendor SDK license notice"
    Write-Host "NVIDIA DLSS SDK: NVIDIA RTX SDKs License."
    Write-Host "AMD FidelityFX SDK: AMD FidelityFX license."
    Write-Host "Intel XeSS SDK: Intel Simplified Software License."
    Write-Host "These SDKs are not part of FemboyEngine2D and must not be committed."
}

function Clone-Sdk([string]$Url, [string]$Destination) {
    if (Test-Path (Join-Path $Destination ".git")) {
        Write-Host "  present $Destination"
        return
    }
    Write-Host "  cloning $Url"
    git clone --depth 1 $Url $Destination
    if ($LASTEXITCODE -ne 0) { throw "git clone failed for $Url" }
}

if ($Download) {
    Write-Step "Downloading vendor SDKs"
    if ([string]::IsNullOrEmpty($Only) -or $Only -eq "dlss") {
        Clone-Sdk "https://github.com/NVIDIA/DLSS.git" $DlssRoot
    }
    if ([string]::IsNullOrEmpty($Only) -or $Only -eq "fidelityfx") {
        Clone-Sdk "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git" $FidelityFxRepo
    }
    if ([string]::IsNullOrEmpty($Only) -or $Only -eq "xess") {
        Clone-Sdk "https://github.com/intel/xess.git" $XessRoot
    }
}

Write-Step "SDK layout"
$Missing = 0
if (Test-Dlss) { Write-Host "  DLSS       : $DlssRoot" }
else { Write-Warning "DLSS not found at $DlssRoot (expected include\nvsdk_ngx.h)"; $Missing++ }
if (Test-FidelityFx) { Write-Host "  FidelityFX : $FidelityFxRoot" }
else { Write-Warning "FidelityFX not found at $FidelityFxRoot (expected api\include\ffx_api.h)"; $Missing++ }
if (Test-Xess) { Write-Host "  XeSS       : $XessRoot (SDK retained; the engine does not compile XeSS)" }
else { Write-Warning "XeSS not found at $XessRoot (expected inc\xess\xess.h)" }

Write-Host ""
Write-Host "CMake flags for a local SDK:"
Write-Host "  -DFBY_WITH_DLSS=ON -DFBY_DLSS_ROOT=`"$DlssRoot`""
Write-Host "  -DFBY_WITH_FIDELITYFX=ON -DFBY_FIDELITYFX_ROOT=`"$FidelityFxRoot`""
Write-Host ""
Write-Host "Without an SDK, leave the flags OFF. FSR1, the FSR2-compatible core path and"
Write-Host "custom frame generation build without a vendor SDK."

if ($Require -and $Missing -ne 0) { exit 1 }
