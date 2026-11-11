# Building on Windows 11

> Status: supported (M7). CI builds and runs all tests on `windows-latest` (MSVC, Qt 6.8);
> the Packages workflow builds the installer.

## Requirements

- Visual Studio 2022 or 2026 with "Desktop development with C++" (MSVC, CMake, Ninja included)
- Qt ≥ 6.8 for MSVC 2022 64-bit from the [Qt online installer](https://www.qt.io/download-qt-installer-oss)
  (component *MSVC 2022 64-bit*; *Qt Shader Tools* is required)
- Git

## Build

From a *Developer PowerShell for VS*:

```powershell
cmake --preset msvc -DCMAKE_PREFIX_PATH=C:\Qt\6.10.0\msvc2022_64
cmake --build --preset msvc
ctest --preset msvc
C:\Qt\6.10.0\msvc2022_64\bin\windeployqt.exe build\msvc\Release\NurbforgeCAD.exe
```

The RHI uses Direct3D 11 by default; set `NFCAD_RHI_BACKEND=d3d12` (or `opengl`, `vulkan`) to
choose another one.

## Installer

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNFCAD_BUILD_TESTS=OFF
cmake --build build
cmake --install build --component app --prefix "$PWD\stage"   # runs windeployqt (absolute prefix!)
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DSourceDir=$PWD\stage /DOutputDir=$PWD\dist packaging\windows\nurbforge-cad.iss
```

The installer (`NurbforgeCAD-<version>-win64-setup.exe`) installs to *Program Files*, creates
start-menu (optionally desktop) shortcuts and registers `.nfcad` files. CI: workflow
`Packages`, job `windows` (artifact `windows-installer`).

## Notes

- Shortcuts shown as Cmd on macOS are Ctrl here (Qt maps them).
