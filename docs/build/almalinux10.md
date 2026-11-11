# Building on AlmaLinux / RHEL 10

> Status: prepared, becomes a supported platform in milestone M7. The CI job builds already but
> is allowed to fail.

## Requirements

```bash
sudo dnf install -y epel-release            # AlmaLinux; on RHEL enable CodeReady Builder + EPEL
sudo dnf install -y gcc-c++ git cmake ninja-build \
     qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtshadertools-devel qt6-qtsvg-devel \
     mesa-libGL-devel mesa-vulkan-drivers
```

Check that the distribution's Qt is ≥ 6.8: `rpm -q qt6-qtbase`. If it is older, install Qt with
the online installer and pass `-DCMAKE_PREFIX_PATH=~/Qt/6.x/gcc_64`.

`qt6-qtbase-private-devel` is needed because `QRhi` headers are part of the semi-public RHI API.

## Build

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/NurbforgeCAD
```

Runs under Wayland and X11 (`QT_QPA_PLATFORM=xcb` to force X11). The RHI backend is OpenGL by
default; Vulkan can be selected in the settings (M7).

## Packaging (M7)

RPM spec in `packaging/linux/`, optionally AppImage or Flatpak.
