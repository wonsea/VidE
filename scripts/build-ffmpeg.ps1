# Builds the pinned LGPL FFmpeg (shared) into build/deps/ffmpeg.
#
# FFmpeg's build system (configure + make) only runs under a POSIX shell, so on
# Windows this REQUIRES MSYS2 (same as the other third-party builds). The script
# checks for MSYS2 and the required tools first, then delegates to
# scripts/build-ffmpeg.sh.
#
# The tarball is downloaded with PowerShell (Windows TLS/cert store) rather than
# MSYS2 curl, which may lack a CA bundle; the SHA-256 is verified here and again
# in the shell script.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts/build-ffmpeg.ps1
# Offline: pass -Tarball C:\path\to\ffmpeg-8.1.3.tar.xz (or set VIDE_FFMPEG_TARBALL).

param(
    [string]$Tarball = ""
)

$ErrorActionPreference = "Stop"

$ffVersion = "8.1.3"
$ffTar     = "ffmpeg-$ffVersion.tar.xz"
$ffUrl     = "https://ffmpeg.org/releases/$ffTar"
$ffSha256  = "7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3"

$repoRoot = Split-Path -Parent $PSScriptRoot
$msysRoot = "C:\msys64"
$bashExe  = Join-Path $msysRoot "usr\bin\bash.exe"
$mingwBin = Join-Path $msysRoot "mingw64\bin"
$usrBin   = Join-Path $msysRoot "usr\bin"

if (-not (Test-Path $bashExe)) {
    throw "MSYS2 not found at $msysRoot.`n" +
          "Install it from https://www.msys2.org/ or run scripts\build-all.ps1 (auto-bootstraps).`n" +
          "Then run: powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1"
}

# Tools needed to extract/compile FFmpeg.
$required = @(
    (Join-Path $mingwBin "gcc.exe"),
    (Join-Path $mingwBin "nasm.exe"),
    (Join-Path $usrBin   "make.exe"),
    (Join-Path $usrBin   "tar.exe"),
    (Join-Path $usrBin   "xz.exe"),
    (Join-Path $usrBin   "sha256sum.exe")
)
$missing = @($required | Where-Object { -not (Test-Path $_) })
if ($missing.Count -gt 0) {
    throw "Missing MSYS2 tools:`n  " + ($missing -join "`n  ") + "`n" +
          "Run: powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1"
}

# ---- acquire + verify the source tarball -----------------------------------
$srcRoot = Join-Path $repoRoot "build\deps\ffmpeg-src"
New-Item -ItemType Directory -Force -Path $srcRoot | Out-Null

$candidates = @()
if ($Tarball -ne "") { $candidates += $Tarball }
$candidates += (Join-Path $srcRoot $ffTar)
$candidates += (Join-Path $repoRoot $ffTar)
$candidates += (Join-Path $repoRoot "external\$ffTar")
$tarballPath = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1

if (-not $tarballPath) {
    $tarballPath = Join-Path $srcRoot $ffTar
    Write-Host "Downloading $ffUrl ..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $ffUrl -OutFile $tarballPath -UseBasicParsing
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tarballPath).Hash.ToLowerInvariant()
if ($hash -ne $ffSha256) {
    throw "FFmpeg tarball checksum mismatch!`n  expected: $ffSha256`n  actual:   $hash`n  file:     $tarballPath"
}

# Repo path in MSYS form (D:\work\source\VidE -> /d/work/source/VidE).
$drive = $repoRoot.Substring(0, 1).ToLower()
$posixRepo = "/$drive" + ($repoRoot.Substring(2) -replace '\\', '/')
$env:VIDE_FFMPEG_TARBALL = $tarballPath -replace '\\', '/'

Write-Host "Building FFmpeg $ffVersion (pinned LGPL shared) into build\deps\ffmpeg ..."
& $bashExe -lc "cd '$posixRepo' && bash scripts/build-ffmpeg.sh"
if ($LASTEXITCODE -ne 0) { throw "FFmpeg build failed (exit $LASTEXITCODE)" }

if (-not (Test-Path (Join-Path $repoRoot "build\deps\ffmpeg\include\libavformat\avformat.h"))) {
    throw "FFmpeg headers were not installed under build\deps\ffmpeg"
}
Write-Host "FFmpeg ready: $(Join-Path $repoRoot 'build\deps\ffmpeg')"
