# FemboyEngine2D - register the .femproj file type for the current user.
#
# After this script runs, double-clicking a project.femproj file in Explorer (or
# choosing "Open with FemboyEngine2D Editor") starts the built editor with the
# project path as its first argument. The editor resolves either a .femproj file
# or the project folder.
#
#   powershell -ExecutionPolicy Bypass -File SetupScripts/RegisterFemboyProjects.ps1
#
# Pass -Unregister to remove the association again, or -EditorPath to point at a
# different editor build.

param(
    [string]$EditorPath = "",
    [switch]$Unregister
)

$ErrorActionPreference = "Stop"
$ProgId = "FemboyEngine.Project"
$Extension = ".femproj"
$Classes = "HKCU:\Software\Classes"

if ($Unregister) {
    Remove-Item -Path (Join-Path $Classes $Extension) -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path (Join-Path $Classes $ProgId) -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Removed the $Extension association."
    exit 0
}

if ([string]::IsNullOrWhiteSpace($EditorPath)) {
    $EditorPath = Join-Path (Split-Path -Parent $PSScriptRoot) "build\bin\fby_editor.exe"
}
$EditorPath = [System.IO.Path]::GetFullPath($EditorPath)
if (-not (Test-Path -LiteralPath $EditorPath)) {
    Write-Warning "The editor was not found at '$EditorPath'. Register the association anyway and rebuild the editor there, or re-run with -EditorPath."
}

New-Item -Path (Join-Path $Classes $ProgId) -Force | Out-Null
Set-Item -Path (Join-Path $Classes $ProgId) -Value "FemboyEngine2D Project"
New-Item -Path (Join-Path $Classes "$ProgId\DefaultIcon") -Force | Out-Null
Set-Item -Path (Join-Path $Classes "$ProgId\DefaultIcon") -Value "`"$EditorPath`",0"
New-Item -Path (Join-Path $Classes "$ProgId\shell\open\command") -Force | Out-Null
Set-Item -Path (Join-Path $Classes "$ProgId\shell\open\command") -Value "`"$EditorPath`" `"%1`""

New-Item -Path (Join-Path $Classes $Extension) -Force | Out-Null
Set-Item -Path (Join-Path $Classes $Extension) -Value $ProgId
New-Item -Path (Join-Path $Classes "$Extension\OpenWithProgids") -Force | Out-Null
New-ItemProperty -Path (Join-Path $Classes "$Extension\OpenWithProgids") -Name $ProgId -Value "" -PropertyType String -Force | Out-Null

Write-Host "Registered $Extension -> $ProgId"
Write-Host "  editor: $EditorPath"
Write-Host "Windows may cache file associations; sign out or restart Explorer if the icon does not refresh."
