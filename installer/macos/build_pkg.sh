#!/usr/bin/env bash
# Vesper — macOS .pkg builder.
# Usage: ./build_pkg.sh [Release-build-dir] [signing-identity]
# Produces Vesper-<version>.pkg installing VST3 + AU + Standalone.
set -euo pipefail

VERSION="1.0.0"
BUILD="${1:-../../build/Vesper_artefacts/Release}"
IDENTITY="${2:-}"   # "Developer ID Installer: Your Name (TEAMID)"
STAGE="$(mktemp -d)"

mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/Components"
mkdir -p "$STAGE/Applications"

cp -R "$BUILD/VST3/Vesper.vst3"      "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$BUILD/AU/Vesper.component"   "$STAGE/Library/Audio/Plug-Ins/Components/"
cp -R "$BUILD/Standalone/Vesper.app" "$STAGE/Applications/"

PKG_ARGS=(
  --root "$STAGE"
  --identifier com.sableaudio.vesper
  --version "$VERSION"
  --install-location /
  "Vesper-$VERSION.pkg"
)
[[ -n "$IDENTITY" ]] && PKG_ARGS=(--sign "$IDENTITY" "${PKG_ARGS[@]}")

pkgbuild "${PKG_ARGS[@]}"
rm -rf "$STAGE"

echo "Built Vesper-$VERSION.pkg"
echo "For distribution: notarize with 'xcrun notarytool submit' then 'xcrun stapler staple'."
