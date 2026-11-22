; This file is part of Likyu — https://github.com/pob31/likyu
;
; Copyright (C) 2026 Pierre-Olivier Boulant
;
; Likyu is free software: you can redistribute it and/or modify it under the
; terms of the GNU General Public License as published by the Free Software
; Foundation, either version 3 of the License, or (at your option) any later
; version. Likyu is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
; or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
; (LICENSE, at the repository root) for more details.
;
; SPDX-License-Identifier: GPL-3.0-or-later
;
; The Windows installer (devplan, Phase 7): Inno Setup 6, unsigned, as WFS-DIY's.
;
;   Installs   Likyu.vst3                     to  C:\Program Files\Common Files\VST3\
;              Likyu.lv2, Likyu-mono.lv2      to  C:\Program Files\Common Files\LV2\
;              LICENSE, README, notices       to  C:\Program Files\Pix et Bel\Likyu\
;
; Compiled by scripts/build-installer.ps1, which passes what this file must not hold a
; second copy of:
;
;   ISCC /DLikyuVersion=<project VERSION> [/DBuildDir=<…\src\shims\juce>] installer\likyu.iss
;
; BuildDir defaults to the release-windows preset's tree. Output: installer\Output\.
;
; Origin: WFS-DIY Plugin/Installer/WFS-DIY-Plugins.iss, with the LV2 bundles, components,
; the AVX2 check and the version from the caller.

#ifndef LikyuVersion
  #error Pass /DLikyuVersion=<x.y.z>: the version is CMakeLists.txt's project(VERSION), never written here.
#endif
#ifndef BuildDir
  #define BuildDir "..\build\release-windows\src\shims\juce"
#endif

#define Stereo BuildDir + "\likyu_plugin_artefacts\Release"
#define Mono   BuildDir + "\likyu_plugin_lv2_mono_artefacts\Release"

[Setup]
; Never changes: Windows knows an installed Likyu by it, and an upgrade replaces the
; installation that carries it.
AppId={{6B376A09-78D6-4F33-AF52-D0EEA141791A}
AppName=Likyu
AppVersion={#LikyuVersion}
AppVerName=Likyu {#LikyuVersion}
AppPublisher=Pix et Bel
AppPublisherURL=https://github.com/pob31/likyu
AppSupportURL=https://github.com/pob31/likyu/issues
AppCopyright=Copyright (C) 2026 Pierre-Olivier Boulant
VersionInfoVersion={#LikyuVersion}
VersionInfoDescription=Likyu installer: dynamic EQ and transient designer (VST3, LV2)
DefaultDirName={commonpf64}\Pix et Bel\Likyu
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=Likyu-{#LikyuVersion}-Windows-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
UninstallDisplayName=Likyu {#LikyuVersion}
; GPLv3: the licence goes with the binaries, on its page and beside them.
LicenseFile=..\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french";  MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
english.CompVst3=VST3 plugin (Likyu)
french.CompVst3=Plugin VST3 (Likyu)
english.CompLv2=LV2 plugins (Likyu, stereo, and Likyu mono)
french.CompLv2=Plugins LV2 (Likyu, stéréo, et Likyu mono)
english.NoAvx2=Windows reports that this processor has no AVX2.%n%nLikyu is built for an x86-64 processor with AVX2 and FMA (Intel from 2013, AMD from 2015). On a processor without them it would stop the host that loads it.%n%nInstall anyway? Choose Yes only if you know this processor has AVX2.
french.NoAvx2=Windows indique que ce processeur n'a pas l'AVX2.%n%nLikyu est compilé pour un processeur x86-64 avec AVX2 et FMA (Intel depuis 2013, AMD depuis 2015). Sur un processeur qui ne les a pas, il arrêterait l'hôte qui le charge.%n%nInstaller quand même ? Ne choisissez Oui que si vous savez que ce processeur a l'AVX2.

[Types]
Name: "full";   Description: "{code:FullTypeName}"
Name: "custom"; Description: "{code:CustomTypeName}"; Flags: iscustom

[Components]
Name: "vst3"; Description: "{cm:CompVst3}"; Types: full custom
Name: "lv2";  Description: "{cm:CompLv2}";  Types: full custom

[InstallDelete]
; A bundle is replaced whole, so that no file of an older version stays inside it.
Type: filesandordirs; Name: "{commoncf64}\VST3\Likyu.vst3";    Components: vst3
Type: filesandordirs; Name: "{commoncf64}\LV2\Likyu.lv2";      Components: lv2
Type: filesandordirs; Name: "{commoncf64}\LV2\Likyu-mono.lv2"; Components: lv2

[Files]
Source: "{#Stereo}\VST3\Likyu.vst3\*";  DestDir: "{commoncf64}\VST3\Likyu.vst3";   Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stereo}\LV2\Likyu.lv2\*";    DestDir: "{commoncf64}\LV2\Likyu.lv2";     Components: lv2;  Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Mono}\LV2\Likyu-mono.lv2\*"; DestDir: "{commoncf64}\LV2\Likyu-mono.lv2"; Components: lv2;  Flags: ignoreversion recursesubdirs createallsubdirs

; As .txt, so that a double click opens them.
Source: "..\LICENSE";                DestDir: "{app}"; DestName: "LICENSE.txt";             Flags: ignoreversion
Source: "..\README.md";              DestDir: "{app}"; DestName: "README.txt";              Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; DestName: "THIRD_PARTY_NOTICES.txt"; Flags: ignoreversion

[Code]
const
  PF_AVX2_INSTRUCTIONS_AVAILABLE = 40;

function IsProcessorFeaturePresent(Feature: DWORD): BOOL;
  external 'IsProcessorFeaturePresent@kernel32.dll stdcall';

function FullTypeName(Param: String): String;
begin
  if ActiveLanguage = 'french' then Result := 'Installation complète' else Result := 'Full installation';
end;

function CustomTypeName(Param: String): String;
begin
  if ActiveLanguage = 'french' then Result := 'Installation personnalisée' else Result := 'Custom installation';
end;

{ The plugin is an AVX2 binary with no dispatch (decided 2026-11-21): on a processor
  without AVX2 the host dies at the first instruction. A Windows older than the feature
  bit may report none on a processor that has it, so the user may go on; a silent install
  takes the default, No. }
function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) then
    Result := SuppressibleMsgBox(CustomMessage('NoAvx2'), mbError, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
end;
