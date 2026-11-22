# Handoff: Phase 7, the installers

2026-11-22. The second half of Phase 7. [`2026-11-22-phase7-optimisation.md`](2026-11-22-phase7-optimisation.md)
still holds for the optimisation and what bites there; this note covers the installers only.

## Where it stands

- **Built:** the three installers and the workflow that makes them.
  - `installer/likyu.iss`: Windows, Inno Setup 6, unsigned. VST3 to `Common Files\VST3`, both
    LV2s to `Common Files\LV2`, the documents to `Program Files\Pix et Bel\Likyu`. It refuses a
    processor that Windows reports as having no AVX2, and lets the user override that in the
    interactive installer, since a Windows older than the feature bit may report none on a
    processor that has it. A silent install takes No.
  - `installer/build-pkg.sh`: macOS, one pkg with VST3, AU and LV2 as choices, plus the documents
    in `/Library/Application Support/Pix et Bel/Likyu`. With the identities and notary credentials
    set, it signs, notarises the plugins and staples the VST3 and AU (for offline machines), then
    signs, notarises and staples the pkg. Without them it signs ad hoc, which is enough to install
    locally.
  - `installer/build-tarball-linux.sh`: the tarball with `install.sh --user|--system` (refuses
    without AVX2 and FMA unless `--force`) and `uninstall.sh`.
  - `scripts/build-installer.ps1` and `scripts/build-installer.sh`: configure the `release-*`
    preset, build, run the Release suite, then package into `installer/Output/`.
  - `.github/workflows/release.yml`: run by hand for the installers as artefacts, or on a tag
    `v<version>` for a **draft** release. The version is `project(likyu VERSION)`, read by every
    script, and the tag must match it. Each job installs what it built on its fresh runner,
    validates the *installed* plugins (pluginval at 10, auval, lilv) and, on Windows and Linux,
    uninstalls and checks nothing is left.
- **The universal build:** `CMAKE_OSX_ARCHITECTURES=arm64;x86_64` in one tree. With
  `LIKYU_SIMD=auto`, the new `LIKYU_SIMD_MACOS_X86_64=scalar` (the default) gives the x86_64 slice
  `-Xarch_x86_64 -DLIKYU_SIMD_SCALAR=1` and no AVX2. `avx2` brings back what 7f measured under
  Rosetta. With the flag pair kept together, JUCE's LV2 helper and bundle steps run once,
  unchanged.
- **Checked on 2026-11-22:**
  - Windows, this machine: the installer compiles; the Release suite and the Debug suites pass.
  - M4 Pro, unsigned: the suite natively, and under Rosetta, where it reports "scalar kernels,
    not fused" and passes all 180 cases; all four binaries carry both slices and have no ymm
    instruction in x86_64; the four components are non-relocatable.
  - NUC: the tarball installs into a throwaway `HOME`, lilv lists both URIs, and uninstall
    leaves nothing.
- **Not yet checked:** the signed, notarised run, and the clean-machine checks themselves, which
  only `release.yml` runs. Nothing was installed system-wide on the author's machines.

## Things that will bite

- **pkgbuild makes bundles relocatable by default.** The installer then "upgrades" a copy found
  anywhere on the disk (a build tree) instead of writing to `/Library`. `build-pkg.sh` writes
  `BundleIsRelocatable false` into each component plist, and adds the key itself because
  `--analyze` often leaves it out.
- **An `.lv2` folder is not a bundle.** Its binary is signed on its own and cannot be stapled.
- **`notarytool submit --wait` can exit 0 on Invalid.** The script reads the JSON status and
  prints the log.
- **To measure the AVX2 x86_64 slice on the Mac again**, configure with
  `-DLIKYU_SIMD_MACOS_X86_64=avx2`; `-DCMAKE_OSX_ARCHITECTURES=x86_64` alone now gives the
  scalar build.
- **The Inno uninstaller hands over to a copy of itself** and returns at once, so the workflow
  waits for the files to go, not for the process.

## Next

1. Dispatch `release.yml` on `main` and read the signed pkg's checks. A version tag, and
   publishing the draft, are the author's.
2. The listening passes on a build from `main`, with the plugin installed again in the hosts.
   The Windows installer from `scripts\build-installer.ps1` does that here.
