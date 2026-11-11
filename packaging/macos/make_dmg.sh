#!/bin/sh
# Builds an (unsigned) disk image from a staged install (docs/build/macos.md):
#   cmake --install build/release --component app --prefix stage   # runs macdeployqt
#   packaging/macos/make_dmg.sh stage/RaytatouilleFusion.app RaytatouilleFusion-<version>-macos.dmg
# Unsigned by decision (2026-11-11); see docs/user-guide.md for opening it the first time.
set -eu
app="$1"
dmg="$2"
tmp=$(mktemp -d)
cp -R "$app" "$tmp/"
ln -s /Applications "$tmp/Applications"
hdiutil create -volname "Raytatouille Fusion" -srcfolder "$tmp" -ov -format UDZO "$dmg"
rm -rf "$tmp"
