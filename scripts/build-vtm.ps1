# Builds the VTM (VVC Test Model) reference decoder for VidE.
#
# VidE reads block/syntax data directly from VTM's decoder structures and uses
# VTM's own KEEP_PRED_AND_RESI_SIGNALS option (via -DVIDE_DEFS) to keep
# picture-sized prediction/residual buffers, so vide_vvc_helper can export
# reco/pred/resi images. See external/patches/README.md.
#
# All build output goes under build/deps/vtm (unified build layout); the produced
# static libraries are copied to build/deps/vtm/lib where the main build looks.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts/build-vtm.ps1

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vtmSource = Join-Path $repoRoot "external\VTM"
$vtmBuild = Join-Path $repoRoot "build\deps\vtm"
$vtmLibOut = Join-Path $vtmBuild "lib"
$mingwBin = "C:\msys64\mingw64\bin"

if (-not (Test-Path (Join-Path $vtmSource "CMakeLists.txt"))) {
    throw "VTM source not found at $vtmSource"
}
if (-not (Test-Path (Join-Path $mingwBin "cmake.exe"))) {
    throw "MSYS2 MinGW toolchain not found at $mingwBin"
}

$env:PATH = "$mingwBin;$env:PATH"

Write-Host "Configuring VTM in $vtmBuild ..."
& (Join-Path $mingwBin "cmake.exe") -S $vtmSource -B $vtmBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DENABLE_TRACING=OFF `
    -DENABLE_HIGH_BITDEPTH=ON `
    -DVIDE_DEFS=KEEP_PRED_AND_RESI_SIGNALS=1
if ($LASTEXITCODE -ne 0) { throw "VTM configure failed" }

Write-Host "Building VTM static libraries ..."
& (Join-Path $mingwBin "cmake.exe") --build $vtmBuild --target DecoderLib -j 8
if ($LASTEXITCODE -ne 0) { throw "VTM build failed" }

# VTM's BBuildEnv writes archives into <source>/lib/<toolchain>/...; copy them
# flat into the unified build directory so the main build finds a stable path.
$srcLib = Join-Path $vtmSource "lib"
if (-not (Test-Path $srcLib)) { throw "VTM libraries were not produced ($srcLib)" }
if (Test-Path $vtmLibOut) { Remove-Item -Recurse -Force $vtmLibOut }
New-Item -ItemType Directory -Path $vtmLibOut -Force | Out-Null
Get-ChildItem -Path $srcLib -Recurse -Filter "*.a" | Copy-Item -Destination $vtmLibOut -Force

$libs = Get-ChildItem -Path $vtmLibOut -Filter "lib*.a"
if ($null -eq $libs) { throw "VTM static libraries were not found under $vtmLibOut" }

Write-Host "VTM libraries ready in ${vtmLibOut}:"
$libs | ForEach-Object { Write-Host "  $($_.FullName)" }
