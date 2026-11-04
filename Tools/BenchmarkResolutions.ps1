# FemboyEngine2D - run the GPU optimization A/B at the target resolutions.
param(
    [int]$Frames = 30,
    [string]$Only = "",
    [string]$Executable = "build-audit/bin/tests/fby_optimization_ab.exe",
    [string]$OutputDirectory = "build-audit/benchmarks"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Executable)) {
    throw "benchmark executable not found: $Executable"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$Resolutions = @(
    @{ Name = "2560x1600"; Width = 2560; Height = 1600; Frames = $Frames },
    @{ Name = "3840x2160"; Width = 3840; Height = 2160; Frames = $Frames },
    @{ Name = "7680x4320"; Width = 7680; Height = 4320; Frames = $Frames },
    # 16K allocates >1 GB for color+depth; keep the smoke pass short.
    @{ Name = "15360x8640"; Width = 15360; Height = 8640; Frames = [Math]::Min($Frames, 3) }
)

foreach ($Resolution in $Resolutions) {
    $Log = Join-Path $OutputDirectory ("optimization_ab_" + $Resolution.Name + ".csv")
    Write-Host ("running " + $Resolution.Name + " -> " + $Log)
    $Arguments = @("--width", $Resolution.Width, "--height", $Resolution.Height, "--frames", $Resolution.Frames)
    if ($Only -ne "") {
        $Arguments += @("--only", $Only)
    }
    & $Executable @Arguments | Tee-Object -FilePath $Log
}
