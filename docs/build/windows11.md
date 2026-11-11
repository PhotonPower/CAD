# Building on Windows 11

> Status: prepared, becomes a supported platform in milestone M7. The CI job builds already but
> is allowed to fail.

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

The RHI uses Direct3D 11 by default; `QSG_RHI_BACKEND` is not used, select the backend in the
application settings (M7) for debugging.

## Notes

- Cmd shortcuts on macOS map to Ctrl here (`src/platform/`).
- Installer: WiX or Inno Setup, scripts in `packaging/windows/` (M7).
