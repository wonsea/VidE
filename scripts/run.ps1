# Runs a VidE binary from build\bin with the MSYS2/MinGW runtime on PATH, so the
# required DLLs (FFmpeg, GLFW, libstdc++, ...) resolve without a manual setup.
#
# Usage (arguments are forwarded verbatim to the executable):
#   powershell -ExecutionPolicy Bypass -File scripts\run.ps1                  # GUI
#   powershell -ExecutionPolicy Bypass -File scripts\run.ps1 --info clip.h265
#   powershell -ExecutionPolicy Bypass -File scripts\run.ps1 --dump-blocks 0 blocks.csv clip.h265
#   powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -tool vide_tests
#
# Note: PowerShell may try to bind leading '-x' tokens to its own parameters.
# The script takes no named parameters, so everything lands in $args intact.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $repoRoot "build\bin"
$mingwBin = "C:\msys64\mingw64\bin"

$tool = "vide"
$rest = @()
if ($args.Count -gt 0 -and $args[0] -match '^-(tool|Tool)$') {
    if ($args.Count -gt 1) { $tool = $args[1] }
    if ($args.Count -gt 2) { $rest = $args[2..($args.Count - 1)] }
} else {
    $rest = $args
}

$exe = Join-Path $binDir "$tool.exe"
if (-not (Test-Path $exe)) {
    throw "Binary not found: $exe. Build first (see docs/03_build_guide.md)."
}
if (Test-Path $mingwBin) {
    $env:PATH = "$mingwBin;$binDir;$env:PATH"
} else {
    $env:PATH = "$binDir;$env:PATH"
}

# Helpers are looked up by name; point VidE at the ones beside the app.
$env:VIDE_HEVC_HELPER_PATH = Join-Path $binDir "vide_hevc_helper.exe"
$env:VIDE_VVC_HELPER_PATH = Join-Path $binDir "vide_vvc_helper.exe"
$env:VIDE_AVC_HELPER_PATH = Join-Path $binDir "vide_avc_helper.exe"
$env:VIDE_VP9_HELPER_PATH = Join-Path $binDir "vide_vp9_helper.exe"
$env:VIDE_AV1_HELPER_PATH = Join-Path $binDir "vide_av1_helper.exe"

Write-Host "Running $exe $($rest -join ' ')"
& $exe @rest
exit $LASTEXITCODE
