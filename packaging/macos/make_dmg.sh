#!/bin/sh
# Builds an (unsigned) disk image from a staged install (docs/build/macos.md):
#   cmake --install build/release --component app --prefix stage   # runs macdeployqt
#   packaging/macos/make_dmg.sh stage/NurbforgeCAD.app NurbforgeCAD-<version>-macos.dmg
# Signing and notarisation follow in milestone M8.
set -eu
app="$1"
dmg="$2"
tmp=$(mktemp -d)
cp -R "$app" "$tmp/"
ln -s /Applications "$tmp/Applications"
hdiutil create -volname "Nurbforge CAD" -srcfolder "$tmp" -ov -format UDZO "$dmg"
rm -rf "$tmp"
