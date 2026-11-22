# This file is part of Likyu — https://github.com/pob31/likyu
#
# Copyright (C) 2026 Pierre-Olivier Boulant
#
# Likyu is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. Likyu is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later

<#
    The Windows installer, from a clean checkout to installer\Output\ (devplan, Phase 7):
    configure the release-windows preset, build the plugins and lib/dsp's suite in
    Release, run the suite, compile installer\likyu.iss with Inno Setup 6, and write the
    installer's SHA-256 beside it. The same script runs in .github/workflows/release.yml.

      scripts\build-installer.ps1              everything
      scripts\build-installer.ps1 -SkipBuild   repackage the tree already built
      scripts\build-installer.ps1 -SkipTests   no suite (not for a release)

    Needs CMake (on PATH, or Visual Studio's own, found as CLAUDE.md describes) and Inno
    Setup 6 (`choco install innosetup`, or jrsoftware.org).

    Origin: WFS-DIY Plugin/Scripts/build-all.ps1 and the Windows job of
    .github/workflows/plugins-release.yml, on Likyu's presets.
#>

[CmdletBinding()]
param(
    [switch] $SkipBuild,
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

function Invoke-Checked {
    param([string] $Exe, [string[]] $Arguments)
    Write-Host "==> $Exe $($Arguments -join ' ')"
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

# The version: project(likyu VERSION x.y.z) in CMakeLists.txt, and nowhere else.
$match = Select-String -Path (Join-Path $repo 'CMakeLists.txt') -Pattern 'project\(likyu VERSION ([0-9]+\.[0-9]+\.[0-9]+)'
if (-not $match) { throw 'could not read project(likyu VERSION ...) from CMakeLists.txt' }
$version = $match.Matches[0].Groups[1].Value
Write-Host "==> Likyu $version"

# CMake: PATH first (CI), then the one Visual Studio installs (this machine, CLAUDE.md).
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
$ctest = (Get-Command ctest -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = if (Test-Path $vswhere) { & $vswhere -latest -property installationPath } else { $null }
    $bin = if ($vs) { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin' } else { $null }
    if (-not $bin -or -not (Test-Path (Join-Path $bin 'cmake.exe'))) { throw 'cmake is neither on PATH nor in Visual Studio' }
    $cmake = Join-Path $bin 'cmake.exe'
    $ctest = Join-Path $bin 'ctest.exe'
}

if (-not $SkipBuild) {
    Invoke-Checked $cmake @('--preset', 'release-windows')
    Invoke-Checked $cmake @('--build', '--preset', 'release-windows-release', '--parallel',
        '--target', 'likyu_plugin_All', 'likyu_plugin_lv2_mono_All', 'likyu_dsp_tests', 'likyu_bench')
}

if (-not $SkipTests) {
    Invoke-Checked $ctest @('--preset', 'release-windows-release')
}

$iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source
if (-not $iscc) { $iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
if (-not (Test-Path $iscc)) { throw 'ISCC.exe not found: install Inno Setup 6 (choco install innosetup)' }

$buildDir = Join-Path $repo 'build\release-windows\src\shims\juce'
foreach ($bundle in @('likyu_plugin_artefacts\Release\VST3\Likyu.vst3',
                      'likyu_plugin_artefacts\Release\LV2\Likyu.lv2',
                      'likyu_plugin_lv2_mono_artefacts\Release\LV2\Likyu-mono.lv2')) {
    if (-not (Test-Path (Join-Path $buildDir $bundle))) { throw "not built: $bundle" }
}

Invoke-Checked $iscc @('/Q', "/DLikyuVersion=$version", "/DBuildDir=$buildDir", (Join-Path $repo 'installer\likyu.iss'))

$exe = Join-Path $repo "installer\Output\Likyu-$version-Windows-x64-Setup.exe"
if (-not (Test-Path $exe)) { throw "ISCC produced no $exe" }
$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path -Leaf $exe)" | Out-File -FilePath "$exe.sha256" -Encoding ascii -NoNewline
Write-Host "==> $exe"
Write-Host "    sha256 $hash"
