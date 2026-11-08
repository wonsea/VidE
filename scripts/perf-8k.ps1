# 8K / large-stream performance measurement for VidE.
#
# Generates an 8K (7680x4320) intra HEVC clip from a synthetic raw source via
# HM's TAppEncoder, then measures VidE cold open + helper decode and warm cached
# random access with `vide --bench`. Requires HM to be built (see build-hm.ps1).
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\perf-8k.ps1 [-Frames 4] [-Reads 100]
param(
    [int]$Frames = 4,
    [int]$Reads = 100
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $repoRoot "build\bin"
$mingwBin = "C:\msys64\mingw64\bin"
$env:PATH = "$mingwBin;$env:PATH"

$encoder = Join-Path $repoRoot "external\HM\bin\ninja\gcc-mingw-16.2\x86_64\release\TAppEncoder.exe"
$cfg = Join-Path $repoRoot "external\HM\cfg\encoder_intra_main.cfg"
$vide = Join-Path $binDir "vide.exe"

if (-not (Test-Path $encoder)) { throw "HM encoder not found: $encoder (run scripts\build-hm.ps1)" }
if (-not (Test-Path $vide)) { throw "VidE not found: $vide (run cmake --build build)" }

$work = Join-Path $repoRoot "tmp\perf8k"
New-Item -ItemType Directory -Force -Path $work | Out-Null

# --- Synthetic 8K I420 source ------------------------------------------------
$w = 7680
$h = 4320
$frameBytes = [int]($w * $h * 3 / 2)
$raw = Join-Path $work "raw_8k.yuv"
$block = New-Object byte[] 1048576
for ($i = 0; $i -lt $block.Length; $i++) { $block[$i] = [byte](($i * 13 + 7) -band 0xFF) }
$fs = [System.IO.File]::Create($raw)
$remaining = [int64]$frameBytes * $Frames
while ($remaining -gt 0) {
    $n = [int][Math]::Min([int64]$block.Length, $remaining)
    $fs.Write($block, 0, $n)
    $remaining -= $n
}
$fs.Close()
Write-Host ("source   : {0} ({1:N1} MB)" -f $raw, ((Get-Item $raw).Length / 1MB))

# --- Encode one 8K intra frame, then repeat it to build a multi-frame stream ---
# HM writes the elementary stream to "str.bin" (cwd) and the reconstruction to -o.
$strBin = Join-Path $work "str.bin"
if (Test-Path $strBin) { Remove-Item $strBin }
Write-Host "encoding : 8K intra frame (this takes ~2 min)..."
Push-Location $work
& $encoder -c $cfg -i $raw -wdt $w -hgt $h -f 1 -fr 30 -o (Join-Path $work "recon_8k.yuv") | Out-Null
Pop-Location
if (-not (Test-Path $strBin)) { throw "encoder produced no bitstream" }

$one = [System.IO.File]::ReadAllBytes($strBin)
$stream = Join-Path $work "perf_8k.265"
$ms = [System.IO.MemoryStream]::new()
for ($i = 0; $i -lt $Frames; $i++) { $ms.Write($one, 0, $one.Length) }
[System.IO.File]::WriteAllBytes($stream, $ms.ToArray())
$ms.Close()
Write-Host ("stream   : {0} ({1} frames, {2} bytes)" -f $stream, $Frames, (Get-Item $stream).Length)

# --- Measure VidE: cold (fresh cache) then warm ------------------------------
$env:VIDE_TMP_DIR = Join-Path $work "tmp"
$cacheDir = Join-Path $env:VIDE_TMP_DIR "vide_cache"
Remove-Item -Recurse -Force $cacheDir -ErrorAction SilentlyContinue

Write-Host "`n-- cold --"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
& $vide --bench $Reads $stream | Select-String "bench"
$sw.Stop()
Write-Host ("wall     : {0:N2} s" -f $sw.Elapsed.TotalSeconds)

Write-Host "`n-- warm --"
$sw.Restart()
& $vide --bench $Reads $stream | Select-String "bench"
$sw.Stop()
Write-Host ("wall     : {0:N2} s" -f $sw.Elapsed.TotalSeconds)

if (Test-Path $cacheDir) {
    $pack = Get-ChildItem $cacheDir -Filter *.vpk | Select-Object -First 1
    if ($pack) { Write-Host ("cache    : {0} ({1:N1} MB)" -f $pack.Name, ($pack.Length / 1MB)) }
}
