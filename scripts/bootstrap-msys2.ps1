# Installs the MSYS2 MinGW-w64 packages VidE needs to build from source.
#
# Windows builds require MSYS2 because FFmpeg, libvpx and the build-*.sh scripts
# use a POSIX shell + autotools. Run this once after installing MSYS2:
#   https://www.msys2.org/
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts/bootstrap-msys2.ps1
#         powershell ... -NoUpdate    # skip `pacman -Syu` (package DB refresh)

param(
    [switch]$NoUpdate
)

$ErrorActionPreference = "Stop"

$msysRoot = "C:\msys64"
$pacman = Join-Path $msysRoot "usr\bin\pacman.exe"
if (-not (Test-Path $pacman)) {
    throw "MSYS2 not found at $msysRoot. Install it from https://www.msys2.org/ first."
}

# MinGW-w64 build/runtime packages. Note: we do NOT install mingw ffmpeg (it is
# a GPL build); VidE builds its own pinned LGPL FFmpeg via build-ffmpeg.ps1.
$packages = @(
    "mingw-w64-x86_64-gcc",
    "mingw-w64-x86_64-cmake",
    "mingw-w64-x86_64-ninja",
    "mingw-w64-x86_64-glfw",
    "mingw-w64-x86_64-pkgconf",
    "mingw-w64-x86_64-aom",
    "mingw-w64-x86_64-libvpx",
    "mingw-w64-x86_64-yasm",
    "mingw-w64-x86_64-nasm",
    "mingw-w64-x86_64-ca-certificates",
    "make",
    "perl",
    "tar",
    "xz",
    "curl",
    "ca-certificates",
    "git"
)

$bash = Join-Path $msysRoot "usr\bin\bash.exe"

function Initialize-Keyring {
    Write-Host "Initializing MSYS2 pacman keyring ..." -ForegroundColor Yellow
    & $bash -lc "pacman-key --init; pacman-key --populate msys2"
}

if (-not $NoUpdate) {
    Write-Host "Refreshing MSYS2 package databases ..."
    & $pacman -Sy --noconfirm
    if ($LASTEXITCODE -ne 0) {
        Initialize-Keyring
        & $pacman -Sy --noconfirm
        if ($LASTEXITCODE -ne 0) { throw "pacman -Sy failed (exit $LASTEXITCODE)" }
    }
}

Write-Host "Installing build dependencies ..."
& $pacman -S --needed --noconfirm @packages
if ($LASTEXITCODE -ne 0) {
    Write-Host "pacman -S failed; refreshing keys and retrying ..." -ForegroundColor Yellow
    Initialize-Keyring
    & $pacman -S --needed --noconfirm @packages
    if ($LASTEXITCODE -ne 0) {
        throw "pacman failed (exit $LASTEXITCODE). Try: C:\msys64\usr\bin\pacman.exe -Syu"
    }
}

Write-Host "MSYS2 dependencies installed. Next:"
Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\build-all.ps1 -RunTests"
