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

# Builds the host probe and the product in Release and copies the bundles where a host
# finds them (devplan, Phase 1). Windows; install.sh is the macOS and Linux one.
#
#   spikes\hostprobe\install.ps1                 build, then copy to the per-user folders
#   spikes\hostprobe\install.ps1 -SkipBuild      copy what is already built
#   spikes\hostprobe\install.ps1 -Uninstall      remove what this script copied
#   spikes\hostprobe\install.ps1 -Vst3Dir "C:\Program Files\Common Files\VST3"
#                                                the system folder, from an elevated shell
#
# The defaults need no elevation: %LOCALAPPDATA%\Programs\Common\VST3 and %APPDATA%\LV2.
# A host that does not scan the per-user VST3 folder is given it in its preferences, or
# the system folder is used instead.

param(
    [switch] $SkipBuild,
    [switch] $Uninstall,
    [string] $Vst3Dir = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'),
    [string] $Lv2Dir = (Join-Path $env:APPDATA 'LV2')
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root 'build\hostprobe'

$bundles = @(
    @{ Source = 'likyu_hostprobe_artefacts\Release\VST3\LikyuProbe.vst3';                          Destination = $Vst3Dir },
    @{ Source = 'likyu_hostprobe_fixed_artefacts\Release\VST3\LikyuProbe-fixed.vst3';              Destination = $Vst3Dir },
    @{ Source = 'likyu\src\shims\juce\likyu_plugin_artefacts\Release\VST3\Likyu.vst3';             Destination = $Vst3Dir },
    @{ Source = 'likyu_hostprobe_artefacts\Release\LV2\LikyuProbe.lv2';                            Destination = $Lv2Dir },
    @{ Source = 'likyu_hostprobe_lv2_mono_artefacts\Release\LV2\LikyuProbe-mono.lv2';              Destination = $Lv2Dir },
    @{ Source = 'likyu\src\shims\juce\likyu_plugin_artefacts\Release\LV2\Likyu.lv2';               Destination = $Lv2Dir },
    @{ Source = 'likyu\src\shims\juce\likyu_plugin_lv2_mono_artefacts\Release\LV2\Likyu-mono.lv2'; Destination = $Lv2Dir }
)

if ($Uninstall) {
    foreach ($bundle in $bundles) {
        $target = Join-Path $bundle.Destination (Split-Path $bundle.Source -Leaf)
        if (Test-Path $target) {
            Remove-Item -Recurse -Force -Confirm:$false $target
            Write-Host "removed $target"
        }
    }
    return
}

if (-not $SkipBuild) {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        # Visual Studio's own CMake (CLAUDE.md, The bench).
        $vs = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
        if (-not (Test-Path (Join-Path $vs 'cmake.exe'))) { throw 'cmake is not on PATH and Visual Studio''s was not found' }
        $env:PATH = "$vs;$env:PATH"
    }

    cmake -S (Join-Path $root 'spikes\hostprobe') -B $build
    if ($LASTEXITCODE -ne 0) { throw 'configure failed' }

    cmake --build $build --config Release --parallel --target likyu_hostprobe_All likyu_hostprobe_fixed_All likyu_hostprobe_lv2_mono_All likyu_plugin_All likyu_plugin_lv2_mono_All
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

foreach ($bundle in $bundles) {
    $source = Join-Path $build $bundle.Source
    if (-not (Test-Path $source)) { throw "not built: $source" }

    New-Item -ItemType Directory -Force $bundle.Destination | Out-Null
    $target = Join-Path $bundle.Destination (Split-Path $source -Leaf)
    if (Test-Path $target) { Remove-Item -Recurse -Force -Confirm:$false $target }
    Copy-Item -Recurse $source $target
    Write-Host "installed $target"
}
