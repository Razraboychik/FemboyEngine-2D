param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Passthrough
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Resolve-Path (Join-Path $ScriptDirectory "../..")
$Arguments = @("--platform", "windows") + $Passthrough
& python (Join-Path $Root "Source/Tools/Dev/PackageDesktop.py") @Arguments
exit $LASTEXITCODE
