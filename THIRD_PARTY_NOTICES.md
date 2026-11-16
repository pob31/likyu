# Third-Party Notices

Likyū incorporates the third-party code below. Its licences and copyright notices are recorded
here, once, per dependency.

Two submodules under `ThirdParty/` supply everything. Nothing is vendored by this repository
itself, and nothing is fetched at configure time. Each section names its submodule path and the
commit it is pinned at; `scripts/check-pins.py` fails CI when a gitlink and this file disagree, so
the versions written here are checked, not remembered.

---

## JUCE Framework

- **Website**: https://juce.com
- **Submodule**: `ThirdParty/JUCE`
- **Version**: 9.0.5, commit `d60b7a78030661574a615b586d9325ba7a7fa7dd` (the tag itself; the same
  commit WFS-DIY and the OSCQuery Automation Plugin pin)
- **Licence**: AGPLv3 or commercial JUCE Licence
- **Copyright**: Raw Material Software Limited

The JUCE modules are dual-licensed under the [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html)
and the commercial [JUCE End User Licence Agreement](https://juce.com/legal/juce-8-licence/). This
project uses JUCE under the AGPLv3, which is compatible with its own GPLv3. JUCE bundles further
dependencies with their own licences; see `ThirdParty/JUCE/LICENSE.md` for the list. Those that
reach a binary this project builds are named in the three sections that follow.

JUCE is in the plugin shell only. The DSP library, `lib/dsp`, includes no JUCE header.

---

## VST3 SDK (inside JUCE)

- **Website**: https://github.com/steinbergmedia/vst3sdk
- **Version**: as bundled by JUCE 9.0.5, under
  `ThirdParty/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/`
- **Licence**: MIT
- **Copyright**: Steinberg Media Technologies GmbH

It reaches every VST3 binary this project builds. The licence is as the OSCQuery Automation Plugin
read it on 2026-11-04 from the bundled `LICENSE.txt`, at the same JUCE commit. VST is a trademark
of Steinberg Media Technologies GmbH.

---

## LV2 and its libraries (inside JUCE)

- **Website**: https://lv2plug.in
- **Version**: as bundled by JUCE 9.0.5, under
  `ThirdParty/JUCE/modules/juce_audio_processors_headless/format_types/LV2_SDK/`
- **Licence and copyright**: as stated in each bundled library's own files there

They reach the two LV2 bundles this project builds.

---

## AudioUnitSDK (inside JUCE)

- **Website**: https://github.com/apple/AudioUnitSDK
- **Version**: as bundled by JUCE 9.0.5, under
  `ThirdParty/JUCE/modules/juce_audio_plugin_client/AU/AudioUnitSDK/`
- **Licence and copyright**: as stated in the bundled files there

It reaches the AU this project builds on macOS.

---

## doctest

- **Website**: https://github.com/doctest/doctest
- **Submodule**: `ThirdParty/doctest`
- **Version**: 2.5.3, commit `2d0a9359a60c51affe2a9bebb1be1dca47868151`
- **Licence**: MIT
- **Copyright**: Viktor Kirilov

The unit-test framework. It is compiled into the test executables only and never reaches a plugin
binary. Full text: `ThirdParty/doctest/LICENSE.txt`.

---

## Tools that are run and not incorporated

- **pluginval** 1.0.4 (https://github.com/Tracktion/pluginval, GPLv3, Tracktion Corporation):
  downloaded from its release page by `.github/workflows/ci.yml` and `scripts/farm-check.sh` to
  validate the built VST3. It never reaches the repository or a binary.
- **lilv's utilities, lv2lint** (from the Linux runner's packages): run by CI on the built LV2
  bundles. They never reach the repository or a binary.

## Code from sibling repositories

Files copied from the OSCQuery Automation Plugin (https://github.com/pob31/OSCQuery-Automation-plugin,
GPLv3, Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle) and, through it, from Go.dot
(https://github.com/pob31/go.dot, GPLv3, Copyright (C) 2026 Pierre-Olivier Boulant) keep their
original header and carry an `Origin:` line naming the file and the commit.
