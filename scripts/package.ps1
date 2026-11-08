# Packages a ready-to-run VidE distribution into build\dist.
#
# Copies vide.exe, the helper executables and every non-system DLL they depend
# on (transitively) so the app can be double-clicked without MSYS2 on PATH.
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts\package.ps1
#
# By default the FFmpeg runtime DLLs are NOT bundled: the system FFmpeg on the
# build machine is usually a GPL build, and distributing it alongside this MIT
# project is not license-safe. Pass -IncludeFfmpeg only when the DLLs next to
# the executable are an LGPL build (see THIRD_PARTY_NOTICES.md).

param(
    [switch]$IncludeFfmpeg   # bundle FFmpeg DLLs (must be an LGPL build!)
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $repoRoot "build\bin"
$distDir = Join-Path $repoRoot "build\dist"
$mingwBin = "C:\msys64\mingw64\bin"

if (-not (Test-Path (Join-Path $binDir "vide.exe"))) {
    throw "vide.exe not found in $binDir. Build first."
}

$objdump = Join-Path $mingwBin "objdump.exe"
if (-not (Test-Path $objdump)) {
    throw "objdump.exe not found at $objdump (needed to list DLL dependencies)."
}

if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

# System DLLs that ship with Windows and must NOT be copied.
$systemDlls = @(
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll",
    "ole32.dll", "oleaut32.dll", "opengl32.dll", "msvcrt.dll", "ws2_32.dll",
    "comdlg32.dll", "winmm.dll", "version.dll", "imm32.dll", "dwmapi.dll",
    "bcrypt.dll", "crypt32.dll", "setupapi.dll", "cfgmgr32.dll", "powrprof.dll",
    "shlwapi.dll", "ntdll.dll", "netapi32.dll", "userenv.dll", "d3d11.dll",
    "dxgi.dll", "opengl32.dll", "mpr.dll", "psapi.dll", "dbghelp.dll"
)

# Search paths for resolving DLLs by name. The vendored FFmpeg build comes first
# so its (LGPL) av*.dll are chosen over any same-versioned MSYS2 ones.
$ffmpegBin = Join-Path $repoRoot "build\deps\ffmpeg\bin"
$searchDirs = @($ffmpegBin, $binDir, $mingwBin, (Join-Path $mingwBin "..\lib"))
$env:PATH = "$mingwBin;$env:PATH"

function Get-ImportedDlls([string]$exe) {
    $out = & $objdump -p $exe 2>$null
    $dlls = @()
    foreach ($line in $out) {
        if ($line -match "DLL Name:\s*(.+)$") { $dlls += $Matches[1].Trim() }
    }
    return $dlls
}

# BFS over the dependency graph, collecting resolvable DLL paths.
$resolved = @{}
$queue = New-Object System.Collections.Queue
foreach ($exe in @("vide.exe", "vide_hevc_helper.exe", "vide_vvc_helper.exe",
                   "vide_avc_helper.exe", "vide_vp9_helper.exe", "vide_av1_helper.exe")) {
    $p = Join-Path $binDir $exe
    if (Test-Path $p) { $queue.Enqueue($p) }
}

while ($queue.Count -gt 0) {
    $current = $queue.Dequeue()
    foreach ($dll in (Get-ImportedDlls $current)) {
        $lower = $dll.ToLowerInvariant()
        if ($systemDlls -contains $lower) { continue }
        if (-not $IncludeFfmpeg -and
            $lower -match '^(av(codec|format|util|filter|device)|sw(scale|resample)|postproc)(-\d+)?\.dll$') {
            continue
        }
        if ($resolved.ContainsKey($lower)) { continue }

        $found = $null
        foreach ($dir in $searchDirs) {
            $candidate = Join-Path $dir $dll
            if (Test-Path $candidate) { $found = $candidate; break }
        }
        if ($found) {
            $resolved[$lower] = $found
            $queue.Enqueue($found)
        } else {
            Write-Warning "Could not resolve dependency: $dll (needed by $(Split-Path $current -Leaf))"
        }
    }
}

# Copy executables + resolved DLLs.
foreach ($exe in @("vide.exe", "vide_hevc_helper.exe", "vide_vvc_helper.exe",
                   "vide_avc_helper.exe", "vide_vp9_helper.exe", "vide_av1_helper.exe",
                   "vide_tests.exe")) {
    $p = Join-Path $binDir $exe
    if (Test-Path $p) { Copy-Item $p $distDir -Force }
}
foreach ($dll in $resolved.Values) {
    Copy-Item $dll $distDir -Force
}

# When bundling FFmpeg, ship the LGPL text (required by the license).
if ($IncludeFfmpeg) {
    $lgpl = Get-ChildItem -Path (Join-Path $repoRoot "build\deps\ffmpeg-src") -Recurse `
                          -Filter "COPYING.LGPLv2.1" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($lgpl) {
        Copy-Item $lgpl.FullName (Join-Path $distDir "FFMPEG-COPYING.LGPLv2.1.txt") -Force
    } else {
        Write-Warning "FFmpeg LGPL text not found under build\deps\ffmpeg-src"
    }
}

$count = (Get-ChildItem $distDir -Filter *.dll).Count
Write-Host "Packaged to $distDir"
Write-Host "  executables + $count DLLs"
if (-not $IncludeFfmpeg) {
    Write-Host "  FFmpeg runtime NOT bundled (license-safe default)."
    Write-Host "  Users need FFmpeg (LGPL recommended) on PATH or next to vide.exe;"
    Write-Host "  pass -IncludeFfmpeg only with an LGPL build of the av*.dll files."
}
Get-ChildItem $distDir | Select-Object Name | Format-Table -AutoSize
