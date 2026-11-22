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

# The Linux tarball (devplan, Phase 7), from the release-linux preset's build. Run by
# scripts/build-installer.sh after the build; on its own to repackage.
#
#   Output   installer/Output/Likyu-<version>-Linux-x86_64.tar.gz and its .sha256
#
#   Likyu-<version>-Linux-x86_64/
#     VST3/Likyu.vst3/
#     LV2/Likyu.lv2/  LV2/Likyu-mono.lv2/
#     install.sh      --user (~/.vst3, ~/.lv2) or --system (/usr/local/lib/vst3, …/lv2)
#     uninstall.sh    the same two, removing Likyu's three bundles and nothing else
#     README.txt  README.md  LICENSE  THIRD_PARTY_NOTICES.md
#
#   LIKYU_BUILD_DIR   the tree's src/shims/juce (default: build/release-linux/src/shims/juce)
#
# Origin: WFS-DIY Plugin/Installer/build-tarball-linux.sh, without the Standalone and the
# bridge library, with the processor check.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${LIKYU_BUILD_DIR:-$REPO/build/release-linux/src/shims/juce}"
STEREO="$BUILD_DIR/likyu_plugin_artefacts/Release"
MONO="$BUILD_DIR/likyu_plugin_lv2_mono_artefacts/Release"
OUT="$REPO/installer/Output"

VERSION="$(sed -nE 's/^project\(likyu VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$REPO/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "error: no project(likyu VERSION …) in CMakeLists.txt" >&2; exit 1; }

ARCH="$(uname -m)"
[ "$ARCH" = "x86_64" ] || { echo "error: the tarball is built on x86_64, this is $ARCH" >&2; exit 1; }
STEM="Likyu-$VERSION-Linux-$ARCH"
STAGE="$OUT/$STEM"
TARBALL="$OUT/$STEM.tar.gz"
echo "==> $STEM"

for b in "$STEREO/VST3/Likyu.vst3" "$STEREO/LV2/Likyu.lv2" "$MONO/LV2/Likyu-mono.lv2"; do
    [ -d "$b" ] || { echo "error: not built: $b" >&2; exit 1; }
done

rm -rf "$STAGE" "$TARBALL" "$TARBALL.sha256"
mkdir -p "$STAGE/VST3" "$STAGE/LV2"
cp -R "$STEREO/VST3/Likyu.vst3" "$STAGE/VST3/"
cp -R "$STEREO/LV2/Likyu.lv2" "$MONO/LV2/Likyu-mono.lv2" "$STAGE/LV2/"
cp "$REPO/LICENSE" "$REPO/README.md" "$REPO/THIRD_PARTY_NOTICES.md" "$STAGE/"

cat > "$STAGE/install.sh" <<'EOF'
#!/usr/bin/env bash
# Likyu's installer for Linux. GPL-3.0-or-later; see LICENSE beside this file.
#
#   ./install.sh --user      ~/.vst3 and ~/.lv2, no sudo
#   ./install.sh --system    /usr/local/lib/vst3 and /usr/local/lib/lv2, with sudo
#   add --force to install on a processor that reports no AVX2 or FMA
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

MODE="" FORCE=0
for arg in "$@"; do
    case "$arg" in
        --user|--system) MODE="$arg" ;;
        --force) FORCE=1 ;;
        *) echo "Usage: $0 --user | --system [--force]" >&2; exit 1 ;;
    esac
done
if [[ -z "$MODE" && -t 0 ]]; then
    read -r -p "Install for this user only (~/.vst3, ~/.lv2)? [Y/n] " ans
    case "${ans,,}" in n|no) MODE="--system" ;; *) MODE="--user" ;; esac
fi
case "$MODE" in
    --user)   VST3_DIR="$HOME/.vst3";          LV2_DIR="$HOME/.lv2";          SUDO="" ;;
    --system) VST3_DIR="/usr/local/lib/vst3";  LV2_DIR="/usr/local/lib/lv2";  SUDO="sudo" ;;
    *) echo "Usage: $0 --user | --system [--force]" >&2; exit 1 ;;
esac

# Built for AVX2 with FMA, with no dispatch: on a processor without them the host that
# loads the plugin stops at its first instruction.
flags="$(grep -m1 '^flags' /proc/cpuinfo 2>/dev/null || true)"
if ! grep -qw avx2 <<<"$flags" || ! grep -qw fma <<<"$flags"; then
    echo "This processor reports no AVX2 or no FMA, which Likyu is built for (Intel from 2013, AMD from 2015)." >&2
    if [[ "$FORCE" != 1 ]]; then
        echo "Not installed. Add --force if you know better." >&2
        exit 1
    fi
fi

echo "==> Installing Likyu"
echo "    VST3: $VST3_DIR/Likyu.vst3"
echo "    LV2:  $LV2_DIR/Likyu.lv2, $LV2_DIR/Likyu-mono.lv2"
$SUDO mkdir -p "$VST3_DIR" "$LV2_DIR"
$SUDO rm -rf "$VST3_DIR/Likyu.vst3" "$LV2_DIR/Likyu.lv2" "$LV2_DIR/Likyu-mono.lv2"
$SUDO cp -R "$HERE/VST3/Likyu.vst3" "$VST3_DIR/"
$SUDO cp -R "$HERE/LV2/Likyu.lv2" "$HERE/LV2/Likyu-mono.lv2" "$LV2_DIR/"
echo "Done. Rescan plugins in your host; Likyu is under Pix et Bel."
echo "To uninstall: $HERE/uninstall.sh $MODE"
EOF

cat > "$STAGE/uninstall.sh" <<'EOF'
#!/usr/bin/env bash
# Likyu's uninstaller for Linux: removes Likyu's three bundles, nothing else.
# GPL-3.0-or-later; see LICENSE beside this file.
set -euo pipefail
case "${1:-}" in
    --user)   VST3_DIR="$HOME/.vst3";          LV2_DIR="$HOME/.lv2";          SUDO="" ;;
    --system) VST3_DIR="/usr/local/lib/vst3";  LV2_DIR="/usr/local/lib/lv2";  SUDO="sudo" ;;
    *) echo "Usage: $0 --user | --system" >&2; exit 1 ;;
esac
$SUDO rm -rf "$VST3_DIR/Likyu.vst3" "$LV2_DIR/Likyu.lv2" "$LV2_DIR/Likyu-mono.lv2"
echo "Likyu removed from $VST3_DIR and $LV2_DIR."
EOF
chmod 0755 "$STAGE/install.sh" "$STAGE/uninstall.sh"

cat > "$STAGE/README.txt" <<EOF
Likyu $VERSION, Linux $ARCH
Dynamic EQ and transient designer. Pix et Bel. GPLv3: see LICENSE.

Contents
  VST3/Likyu.vst3        any channel layout up to 64, with a sidechain input
  LV2/Likyu.lv2          stereo  (https://pixetbel.org/lv2/likyu/stereo)
  LV2/Likyu-mono.lv2     mono    (https://pixetbel.org/lv2/likyu/mono)

Install
  ./install.sh --user      ~/.vst3 and ~/.lv2 (no sudo)
  ./install.sh --system    /usr/local/lib/vst3 and /usr/local/lib/lv2 (sudo)
  ./uninstall.sh --user | --system

Needs an x86-64 processor with AVX2 and FMA (Intel from Haswell, 2013; AMD from
Excavator and Zen). install.sh checks /proc/cpuinfo.

Source: https://github.com/pob31/likyu
EOF

(cd "$OUT" && tar --owner=0 --group=0 -czf "$TARBALL" "$STEM" && sha256sum "$(basename "$TARBALL")" > "$(basename "$TARBALL").sha256")
rm -rf "$STAGE"
echo "==> $TARBALL"
cat "$TARBALL.sha256"
