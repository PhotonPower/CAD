# Building on AlmaLinux / RHEL 10

> Status: supported (M7). CI builds and runs all tests in an `almalinux:10` container with the
> distribution's Qt; the Packages workflow builds and installs the RPM.

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
default; `NFCAD_RHI_BACKEND=vulkan` selects Vulkan.

## RPM

```bash
sudo dnf install -y rpm-build
rpmbuild -bb packaging/linux/nurbforge-cad.spec --define "_srcdir $PWD" --define "_topdir $PWD/build/rpmbuild"
sudo dnf install ./build/rpmbuild/RPMS/*/nurbforge-cad-*.rpm
```

The package installs `/usr/bin/NurbforgeCAD`, a desktop entry, an icon and AppStream metadata;
it depends on the distribution's Qt 6 packages. CI: workflow `Packages`, job `almalinux10`
(artifact `almalinux10-rpm`).
