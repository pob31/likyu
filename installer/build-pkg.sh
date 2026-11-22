#!/usr/bin/env bash
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

# The macOS installer (devplan, Phase 7): one .pkg from the release-macos preset's
# universal build, its arm64 slice NEON and its x86_64 slice the scalar build (decided
# 2026-11-22). Run by scripts/build-installer.sh after the build; on its own to repackage.
#
#   Installs   Likyu.vst3                  to  /Library/Audio/Plug-Ins/VST3/
#              Likyu.component             to  /Library/Audio/Plug-Ins/Components/
#              Likyu.lv2, Likyu-mono.lv2   to  /Library/Audio/Plug-Ins/LV2/
#              LICENSE, README, notices    to  /Library/Application Support/Pix et Bel/Likyu/
#   Output     installer/Output/Likyu-<version>-macOS.pkg and its .sha256
#
# What it checks before packaging: every binary carries both slices, and the x86_64 slice
# has no AVX instruction (a ymm register anywhere in it would be one).
#
# Signing and notarisation, each only when its variables are set, so that an unsigned
# local run still gives a pkg that installs (ad hoc signatures, which the AU registrar
# needs):
#   CODESIGN_IDENTITY_APP      "Developer ID Application: …", the plugins
#   CODESIGN_IDENTITY_INSTALL  "Developer ID Installer: …", the pkg
#   notary credentials, the first complete set of:
#     NOTARY_API_KEY (path to the .p8) + NOTARY_API_KEY_ID + NOTARY_API_ISSUER   (CI)
#     NOTARY_APPLE_ID + NOTARY_PASSWORD + NOTARY_TEAM_ID
#     NOTARIZE_KEYCHAIN_PROFILE
# With notary credentials the plugins are notarised first and the VST3 and AU stapled, so
# that a bundle copied to an offline machine (a show computer) still carries its ticket;
# then the pkg is notarised and stapled. An .lv2 folder is not a bundle and cannot be
# stapled: Gatekeeper looks its binaries' tickets up online.
#
#   LIKYU_BUILD_DIR   the tree's src/shims/juce (default: build/release-macos/src/shims/juce)
#
# Origin: WFS-DIY Plugin/Installer/build-pkg.sh, with the LV2 bundles, the slice checks,
# the plugins notarised and stapled before the pkg, and non-relocatable components.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${LIKYU_BUILD_DIR:-$REPO/build/release-macos/src/shims/juce}"
STEREO="$BUILD_DIR/likyu_plugin_artefacts/Release"
MONO="$BUILD_DIR/likyu_plugin_lv2_mono_artefacts/Release"
OUT="$REPO/installer/Output"
WORK="$OUT/macos-work"
ID=io.github.pob31.likyu.pkg

VERSION="$(sed -nE 's/^project\(likyu VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$REPO/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "error: no project(likyu VERSION …) in CMakeLists.txt" >&2; exit 1; }
PKG="$OUT/Likyu-$VERSION-macOS.pkg"
echo "==> Likyu $VERSION, macOS pkg"

for b in "$STEREO/VST3/Likyu.vst3" "$STEREO/AU/Likyu.component" "$STEREO/LV2/Likyu.lv2" "$MONO/LV2/Likyu-mono.lv2"; do
    [ -d "$b" ] || { echo "error: not built: $b" >&2; exit 1; }
done

# ── Stage, one root per component ─────────────────────────────────────────
rm -rf "$WORK" "$PKG" "$PKG.sha256"
mkdir -p "$WORK"/root/{vst3,au,lv2,docs} "$WORK/pkgs" "$WORK/resources"
cp -R "$STEREO/VST3/Likyu.vst3" "$WORK/root/vst3/"
cp -R "$STEREO/AU/Likyu.component" "$WORK/root/au/"
cp -R "$STEREO/LV2/Likyu.lv2" "$MONO/LV2/Likyu-mono.lv2" "$WORK/root/lv2/"
cp "$REPO/LICENSE" "$WORK/root/docs/LICENSE.txt"
cp "$REPO/README.md" "$WORK/root/docs/README.md"
cp "$REPO/THIRD_PARTY_NOTICES.md" "$WORK/root/docs/THIRD_PARTY_NOTICES.md"
cp "$REPO/LICENSE" "$WORK/resources/LICENSE.txt"
# codesign refuses a bundle with extended attributes ("detritus not allowed").
xattr -cr "$WORK/root"

# ── The binaries: both slices, and no AVX in the x86_64 one ──────────────
binaries=()
while IFS= read -r -d '' f; do
    if file -b "$f" | grep -q 'Mach-O'; then binaries+=("$f"); fi
done < <(find "$WORK/root" -type f -print0)
[ "${#binaries[@]}" -eq 4 ] || { echo "error: expected 4 binaries (VST3, AU, two LV2), found ${#binaries[@]}" >&2; printf '  %s\n' "${binaries[@]}" >&2; exit 1; }

for f in "${binaries[@]}"; do
    rel="${f#"$WORK/root/"}"
    lipo "$f" -verify_arch arm64 x86_64 || { echo "error: $rel is not arm64 + x86_64: $(lipo -archs "$f")" >&2; exit 1; }
    ymm="$(otool -arch x86_64 -tV "$f" | grep -c '%ymm' || true)"
    [ "$ymm" -eq 0 ] || { echo "error: $rel: $ymm instructions on ymm registers in the x86_64 slice, which must be the scalar build" >&2; exit 1; }
    echo "    $rel: $(lipo -archs "$f"), x86_64 slice without AVX"
done

# ── Sign the plugins ──────────────────────────────────────────────────────
sign() {
    if [ -n "${CODESIGN_IDENTITY_APP:-}" ]; then
        codesign --force --options runtime --timestamp --sign "$CODESIGN_IDENTITY_APP" "$1"
    else
        codesign --force --sign - "$1"
    fi
}
[ -n "${CODESIGN_IDENTITY_APP:-}" ] && echo "==> Signing as $CODESIGN_IDENTITY_APP" || echo "==> Signing ad hoc (no CODESIGN_IDENTITY_APP)"
# The LV2s' binaries one by one, the VST3 and the AU as bundles.
for f in "${binaries[@]}"; do
    case "$f" in */lv2/*) sign "$f" ;; esac
done
sign "$WORK/root/vst3/Likyu.vst3"
sign "$WORK/root/au/Likyu.component"
for f in "$WORK/root/vst3/Likyu.vst3" "$WORK/root/au/Likyu.component" "${binaries[@]}"; do
    codesign --verify --strict --verbose=1 "$f"
done

# ── Notary credentials ────────────────────────────────────────────────────
NOTARY_AUTH=()
if [ -n "${NOTARY_API_KEY:-}" ] && [ -n "${NOTARY_API_KEY_ID:-}" ] && [ -n "${NOTARY_API_ISSUER:-}" ]; then
    NOTARY_AUTH=(--key "$NOTARY_API_KEY" --key-id "$NOTARY_API_KEY_ID" --issuer "$NOTARY_API_ISSUER")
elif [ -n "${NOTARY_APPLE_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ] && [ -n "${NOTARY_TEAM_ID:-}" ]; then
    NOTARY_AUTH=(--apple-id "$NOTARY_APPLE_ID" --password "$NOTARY_PASSWORD" --team-id "$NOTARY_TEAM_ID")
elif [ -n "${NOTARIZE_KEYCHAIN_PROFILE:-}" ]; then
    NOTARY_AUTH=(--keychain-profile "$NOTARIZE_KEYCHAIN_PROFILE")
fi

if [ "${#NOTARY_AUTH[@]}" -gt 0 ]; then
    # Apple refuses ad hoc signatures and unsigned packages outright.
    [ -n "${CODESIGN_IDENTITY_APP:-}" ] && [ -n "${CODESIGN_IDENTITY_INSTALL:-}" ] || {
        echo "error: notarisation needs CODESIGN_IDENTITY_APP and CODESIGN_IDENTITY_INSTALL" >&2; exit 1; }
fi

# `notarytool submit --wait` can end with status Invalid and exit 0: read the status.
notarise() {
    local file="$1" result="$WORK/notary-$(basename "$1").json" status id
    echo "==> Notarising $(basename "$file")"
    xcrun notarytool submit "$file" "${NOTARY_AUTH[@]}" --wait --timeout 1h --output-format json > "$result"
    status="$(plutil -extract status raw -o - "$result")"
    id="$(plutil -extract id raw -o - "$result")"
    echo "    $id: $status"
    if [ "$status" != "Accepted" ]; then
        xcrun notarytool log "$id" "${NOTARY_AUTH[@]}" || true
        echo "error: notarisation of $(basename "$file") ended $status" >&2
        exit 1
    fi
}

if [ "${#NOTARY_AUTH[@]}" -gt 0 ]; then
    ditto -c -k "$WORK/root" "$WORK/plugins.zip"
    notarise "$WORK/plugins.zip"
    rm "$WORK/plugins.zip"
    xcrun stapler staple "$WORK/root/vst3/Likyu.vst3"
    xcrun stapler staple "$WORK/root/au/Likyu.component"
fi

# ── Component packages ────────────────────────────────────────────────────
# pkgbuild makes a bundle relocatable by default: the installer then "updates" any copy
# of it it finds elsewhere on the disk (a build tree) and leaves /Library without it.
component() {
    local name="$1" location="$2" plist="$WORK/$1.plist" i=0
    pkgbuild --analyze --root "$WORK/root/$name" "$plist" > /dev/null
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" > /dev/null 2>&1; do
        # --analyze leaves the key out as often as not: write it, whatever was there.
        /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$plist" > /dev/null 2>&1 || true
        /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
        i=$((i + 1))
    done
    pkgbuild --root "$WORK/root/$name" --component-plist "$plist" \
             --identifier "$ID.$name" --version "$VERSION" \
             --install-location "$location" "$WORK/pkgs/likyu-$name.pkg"
}
component vst3 "/Library/Audio/Plug-Ins/VST3"
component au   "/Library/Audio/Plug-Ins/Components"
component lv2  "/Library/Audio/Plug-Ins/LV2"
component docs "/Library/Application Support/Pix et Bel/Likyu"

# ── The product ───────────────────────────────────────────────────────────
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Likyu $VERSION</title>
    <license file="LICENSE.txt"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true" enable_anywhere="false" enable_currentUserHome="false"/>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="lv2"/>
        <line choice="docs"/>
    </choices-outline>
    <choice id="vst3" title="VST3" description="Likyu.vst3, in /Library/Audio/Plug-Ins/VST3."><pkg-ref id="$ID.vst3"/></choice>
    <choice id="au" title="Audio Unit" description="Likyu.component, in /Library/Audio/Plug-Ins/Components."><pkg-ref id="$ID.au"/></choice>
    <choice id="lv2" title="LV2" description="Likyu (stereo) and Likyu mono, in /Library/Audio/Plug-Ins/LV2."><pkg-ref id="$ID.lv2"/></choice>
    <choice id="docs" title="Licence and notices" visible="false" enabled="false" selected="true"><pkg-ref id="$ID.docs"/></choice>
    <pkg-ref id="$ID.vst3" version="$VERSION" onConclusion="none">likyu-vst3.pkg</pkg-ref>
    <pkg-ref id="$ID.au" version="$VERSION" onConclusion="none">likyu-au.pkg</pkg-ref>
    <pkg-ref id="$ID.lv2" version="$VERSION" onConclusion="none">likyu-lv2.pkg</pkg-ref>
    <pkg-ref id="$ID.docs" version="$VERSION" onConclusion="none">likyu-docs.pkg</pkg-ref>
</installer-gui-script>
XML

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
             --resources "$WORK/resources" "$WORK/unsigned.pkg"

if [ -n "${CODESIGN_IDENTITY_INSTALL:-}" ]; then
    echo "==> Signing the pkg as $CODESIGN_IDENTITY_INSTALL"
    productsign --timestamp --sign "$CODESIGN_IDENTITY_INSTALL" "$WORK/unsigned.pkg" "$PKG"
    pkgutil --check-signature "$PKG"
else
    mv "$WORK/unsigned.pkg" "$PKG"
fi

if [ "${#NOTARY_AUTH[@]}" -gt 0 ]; then
    notarise "$PKG"
    xcrun stapler staple "$PKG"
    xcrun stapler validate "$PKG"
fi

(cd "$OUT" && shasum -a 256 "$(basename "$PKG")" > "$(basename "$PKG").sha256")
rm -rf "$WORK"
echo "==> $PKG"
cat "$PKG.sha256"
