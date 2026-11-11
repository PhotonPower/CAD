# Building on macOS

Primary development platform. These steps mirror the `macos` job in `.github/workflows/ci.yml`.

## Requirements

- macOS 14 (Sonoma) or newer, Apple Silicon or Intel
- Xcode Command Line Tools (Apple Clang, C++20): `xcode-select --install`
- [Homebrew](https://brew.sh): `brew install qt cmake ninja`
  (alternatively the Qt online installer; then pass `-DCMAKE_PREFIX_PATH=~/Qt/6.x/macos`)

## Nurbforge

CMake looks for Nurbforge in this order (see [ADR 0003](../adr/0003-nurbforge-integration.md)):

1. an installed package (`find_package(Nurbforge)`, e.g. `cmake --install` of Nurbforge into a prefix
   listed in `CMAKE_PREFIX_PATH`)
2. a local checkout next to this repository (`../Nurbforge`) or at `-DNFCAD_NURBFORGE_SOURCE_DIR=...`
3. a download of the tag `NFCAD_NURBFORGE_TAG` from GitHub

For kernel and GUI development at the same time, option 2 is the most convenient.

## Build and run

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
open build/debug/NurbforgeCAD.app
```

Presets: `debug`, `release`, `ci` (Release with warnings as errors).

## Deployment target and bundle

```bash
export MACOSX_DEPLOYMENT_TARGET=14.0
cmake --preset release && cmake --build --preset release
cmake --install build/release --component app --prefix stage     # runs macdeployqt
packaging/macos/make_dmg.sh stage/NurbforgeCAD.app NurbforgeCAD-macos.dmg
```

With Homebrew's Qt, macdeployqt reports unresolved `QtVirtualKeyboard` rpaths for an optional
input-method plugin; the bundle works regardless. The DMG is unsigned (signing and notarisation:
M8). CI: workflow `Packages`, job `macos`.
