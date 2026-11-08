# Builds the HM (HEVC Test Model) static libraries that VidE links in-process to
# read block-level data straight from the decoder's structures.
#
# Only one upstream change is applied: the CMake `warnings-as-errors` flag is
# dropped for GCC 16 compatibility (see external/patches/README.md).
#
# All build output goes under build/deps/hm (unified build layout); the produced
# static libraries are copied to build/deps/hm/lib where the main build looks.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts/build-hm.ps1

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$hmSource = Join-Path $repoRoot "external\HM"
$hmBuild = Join-Path $repoRoot "build\deps\hm"
$hmLibOut = Join-Path $hmBuild "lib"
$mingwBin = "C:\msys64\mingw64\bin"

if (-not (Test-Path (Join-Path $hmSource "CMakeLists.txt"))) {
    throw "HM source not found at $hmSource"
}
if (-not (Test-Path (Join-Path $mingwBin "cmake.exe"))) {
    throw "MSYS2 MinGW toolchain not found at $mingwBin"
}

$env:PATH = "$mingwBin;$env:PATH"

Write-Host "Configuring HM in $hmBuild ..."
& (Join-Path $mingwBin "cmake.exe") -S $hmSource -B $hmBuild -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "HM configure failed" }

Write-Host "Building HM static libraries ..."
& (Join-Path $mingwBin "cmake.exe") --build $hmBuild -j 8
if ($LASTEXITCODE -ne 0) { throw "HM build failed" }

# HM's BBuildEnv writes archives into <source>/lib/<toolchain>/...; copy them
# flat into the unified build directory so the main build finds a stable path.
$srcLib = Join-Path $hmSource "lib"
if (-not (Test-Path $srcLib)) { throw "HM libraries were not produced ($srcLib)" }
if (Test-Path $hmLibOut) { Remove-Item -Recurse -Force $hmLibOut }
New-Item -ItemType Directory -Path $hmLibOut -Force | Out-Null
Get-ChildItem -Path $srcLib -Recurse -Filter "*.a" | Copy-Item -Destination $hmLibOut -Force

$libs = Get-ChildItem -Path $hmLibOut -Filter "libTLib*.a"
if ($null -eq $libs) { throw "HM static libraries were not found under $hmLibOut" }

Write-Host "HM libraries ready in ${hmLibOut}:"
$libs | ForEach-Object { Write-Host "  $($_.FullName)" }
