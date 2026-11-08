# Builds libaom (AV1) for VidE's AV1 helper. libaom is built out-of-tree into
# build/deps/aom (unified build layout); the resulting libaom.a is used directly
# from there by the main build.
#
# CONFIG_AV1_ENCODER=1 also builds aomenc/aomdec so compatible test streams can
# be produced (this libaom is pre-OBU; see external/patches/README.md).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts/build-aom.ps1

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$aomSource = Join-Path $repoRoot "external\libaom"
$aomBuild = Join-Path $repoRoot "build\deps\aom"
$mingwBin = "C:\msys64\mingw64\bin"

if (-not (Test-Path (Join-Path $aomSource "CMakeLists.txt"))) {
    throw "libaom source not found at $aomSource"
}
if (-not (Test-Path (Join-Path $mingwBin "cmake.exe"))) {
    throw "MSYS2 MinGW toolchain not found at $mingwBin"
}

$env:PATH = "$mingwBin;$env:PATH"

Write-Host "Configuring libaom in $aomBuild ..."
& (Join-Path $mingwBin "cmake.exe") -S $aomSource -B $aomBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DAOM_TARGET_CPU=x86_64 -DCONFIG_AV1_ENCODER=1 `
    -DCONFIG_UNIT_TESTS=0 -DENABLE_TESTS=0 -DENABLE_EXAMPLES=0 -DENABLE_TOOLS=0 -DENABLE_DOCS=0
if ($LASTEXITCODE -ne 0) { throw "libaom configure failed" }

Write-Host "Building libaom ..."
& (Join-Path $mingwBin "cmake.exe") --build $aomBuild --target aom aomenc aomdec -j 8
if ($LASTEXITCODE -ne 0) { throw "libaom build failed" }

if (-not (Test-Path (Join-Path $aomBuild "libaom.a"))) {
    throw "libaom.a was not produced in $aomBuild"
}
Write-Host "libaom ready: $(Join-Path $aomBuild 'libaom.a')"
