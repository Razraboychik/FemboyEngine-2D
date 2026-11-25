# FemboyEngine2D developer entry point. Build trees live under build/<config>.
param(
    [ValidateSet('configure', 'build', 'test', 'run', 'clean')]
    [string]$Action = 'build',
    [ValidateSet('dev', 'release', 'clangcl', 'ci', 'windows', 'linux', 'macos')]
    [string]$Preset = 'dev'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$PresetMap = @{
    dev = 'dev-clang'
    release = 'release-clang'
    clangcl = 'dev-clangcl'
    ci = 'ci-clangcl'
    windows = 'windows-release'
    linux = 'linux-dev'
    macos = 'macos-dev'
}
$Config = $PresetMap[$Preset]
$BuildMap = @{
    'dev-clang' = 'build/dev'
    'release-clang' = 'build/release'
    'dev-clangcl' = 'build/clangcl'
    'ci-clangcl' = 'build/ci'
    'windows-release' = 'build/windows-release'
    'linux-dev' = 'build/linux-dev'
    'macos-dev' = 'build/macos-dev'
}
$BuildDir = Join-Path $Root $BuildMap[$Config]

switch ($Action) {
    'configure' { cmake --preset $Config }
    'build' {
        if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) { cmake --preset $Config }
        cmake --build --preset $Config
    }
    'test' {
        if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) { cmake --preset $Config }
        cmake --build --preset $Config
        ctest --preset $Config
    }
    'run' {
        $Editor = Join-Path $BuildDir 'bin/fby_editor.exe'
        if (-not (Test-Path $Editor)) { throw "editor is not built: $Editor" }
        & $Editor
    }
    'clean' {
        if (Test-Path (Join-Path $Root 'build')) { Remove-Item (Join-Path $Root 'build') -Recurse -Force }
    }
}
