# One-step build for VidE (Windows / MSYS2 MinGW).
#
# Builds the third-party libraries (pinned LGPL FFmpeg, HM / VTM / libaom /
# libvpx) into build/deps/, configures and builds the main project into build/,
# and packages a ready-to-run distribution into build/dist/ (exe + helper +
# DLLs). FFmpeg source is downloaded + SHA-256 verified at build time; the other
# third-party sources are vendored under external/.
#
# Windows builds REQUIRE MSYS2. By default this script is fully self-bootstrapping:
#   * MSYS2 missing        -> installs it via winget (MSYS2.MSYS2)
#   * MSYS2 packages missing -> runs scripts\bootstrap-msys2.ps1
# Pass -NoBootstrap to disable (then it only checks and reports what is missing).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1
#   powershell ... -RunTests | -SkipDeps | -NoPackage | -NoBootstrap

param(
    [switch]$SkipDeps,     # reuse existing build/deps/* and only build the app
    [switch]$RunTests,
    [switch]$NoPackage,    # skip producing the runnable build/dist package
    [switch]$NoBootstrap   # do not auto-install MSYS2 / packages
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$msysRoot = "C:\msys64"
$mingwBin = Join-Path $msysRoot "mingw64\bin"
$usrBin   = Join-Path $msysRoot "usr\bin"
$bashExe  = Join-Path $usrBin "bash.exe"

function Test-Msys2 { Test-Path $bashExe }

function Get-MissingTools {
    $required = @(
        (Join-Path $mingwBin "gcc.exe"),
        (Join-Path $mingwBin "cmake.exe"),
        (Join-Path $mingwBin "ninja.exe"),
        (Join-Path $mingwBin "nasm.exe"),
        (Join-Path $usrBin   "make.exe"),
        (Join-Path $usrBin   "tar.exe"),
        (Join-Path $usrBin   "xz.exe"),
        (Join-Path $usrBin   "curl.exe"),
        (Join-Path $usrBin   "sha256sum.exe")
    )
    return @($required | Where-Object { -not (Test-Path $_) })
}

# ---- MSYS2 itself -----------------------------------------------------------
if (-not (Test-Msys2)) {
    if ($NoBootstrap) {
        throw "MSYS2 not found at $msysRoot (-NoBootstrap given).`n" +
              "Install it from https://www.msys2.org/ then run scripts\bootstrap-msys2.ps1."
    }
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if (-not $winget) {
        throw "MSYS2 not found at $msysRoot and winget is unavailable.`n" +
              "Install MSYS2 from https://www.msys2.org/ then run scripts\bootstrap-msys2.ps1."
    }
    Write-Host ""
    Write-Host "==== MSYS2 not found; installing via winget ====" -ForegroundColor Cyan
    winget install --id MSYS2.MSYS2 -e --silent --accept-package-agreements --accept-source-agreements
    if (-not (Test-Msys2)) {
        throw "MSYS2 install via winget did not complete. Install it manually from https://www.msys2.org/."
    }
}

# ---- MSYS2 packages/tools ---------------------------------------------------
$missing = Get-MissingTools
if ($missing.Count -gt 0) {
    if ($NoBootstrap) {
        throw "Missing MSYS2 build tools:`n  " + ($missing -join "`n  ") + "`n" +
              "Run: powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1"
    }
    Write-Host ""
    Write-Host "==== Installing missing MSYS2 packages ====" -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot "bootstrap-msys2.ps1")
    $missing = Get-MissingTools
    if ($missing.Count -gt 0) {
        throw "Still missing after bootstrap:`n  " + ($missing -join "`n  ")
    }
}

$env:PATH = "$mingwBin;$usrBin;$env:PATH"

# Repo path in MSYS form (D:\work\source\VidE -> /d/work/source/VidE).
$drive = $repoRoot.Substring(0, 1).ToLower()
$posixRepo = "/$drive" + ($repoRoot.Substring(2) -replace '\\', '/')

function Invoke-Step([string]$name, [scriptblock]$action) {
    Write-Host ""
    Write-Host "==== $name ====" -ForegroundColor Cyan
    & $action
    if ($LASTEXITCODE -ne 0) { throw "$name failed (exit $LASTEXITCODE)" }
}

if (-not $SkipDeps) {
    Invoke-Step "[1/6] FFmpeg (LGPL)"  { & (Join-Path $PSScriptRoot "build-ffmpeg.ps1") }
    Invoke-Step "[2/6] HM (HEVC)"      { & (Join-Path $PSScriptRoot "build-hm.ps1") }
    Invoke-Step "[3/6] VTM (VVC)"      { & (Join-Path $PSScriptRoot "build-vtm.ps1") }
    Invoke-Step "[4/6] libaom (AV1)"   { & (Join-Path $PSScriptRoot "build-aom.ps1") }
    Invoke-Step "[5/6] libvpx (VP9)"   { & $bashExe -lc "cd '$posixRepo' && bash scripts/build-vpx.sh" }
} else {
    Write-Host "Skipping third-party deps (-SkipDeps)." -ForegroundColor Yellow
}

Invoke-Step "[6/6] VidE (main)" {
    cmake -S $repoRoot -B (Join-Path $repoRoot "build") -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 `
        -DVIDE_WITH_FFMPEG=ON -DVIDE_WITH_HEVC_HM=ON -DVIDE_WITH_VVC_VTM=ON `
        -DVIDE_WITH_AVC_JM=ON -DVIDE_WITH_VP9=ON -DVIDE_WITH_AV1=ON
    if ($LASTEXITCODE -ne 0) { return }
    cmake --build (Join-Path $repoRoot "build")
}

if ($RunTests) {
    Invoke-Step "[test] ctest" { ctest --test-dir (Join-Path $repoRoot "build") --output-on-failure }
}

if (-not $NoPackage) {
    Invoke-Step "[7/7] package (build\dist)" { & (Join-Path $PSScriptRoot "package.ps1") -IncludeFfmpeg }
}

Write-Host ""
Write-Host "Build complete." -ForegroundColor Green
Write-Host "  run (GUI):  powershell -ExecutionPolicy Bypass -File scripts\run.ps1"
if ($NoPackage) {
    Write-Host "  package:    powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -IncludeFfmpeg  -> build/dist/"
} else {
    Write-Host "  runnable:   build\dist\vide.exe  (double-click; LGPL FFmpeg DLLs bundled)"
}
