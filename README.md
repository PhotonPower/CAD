# Raytatouille Fusion

[![CI](https://github.com/PhotonPower/Raytatouille-Fusion/actions/workflows/ci.yml/badge.svg)](https://github.com/PhotonPower/Raytatouille-Fusion/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Desktop CAD front end for the [Nurbforge](https://github.com/PhotonPower/Nurbforge) NURBS kernel
and the computation kernels built on it** – [Raytatouille](https://github.com/PhotonPower/Raytatouille)
and the solvers of the *Raytatouille Fusion* ([prototype](https://github.com/PhotonPower/RTT-Fusion)) as
signed Python plugins ([plugin plan](docs/plugins.md)). C++20, Qt 6, native rendering
(Metal / Direct3D / OpenGL·Vulkan via Qt RHI).

> Formerly *Nurbforge CAD* (renamed 2026-11-11). Internal identifiers stay: the C++ namespace and
> the Python package `nfcad`, the document format `.nfcad`, the `NFCAD_*` environment variables.

> **Status: 1.0 – milestones M0–M8 and plugin milestones P1–P7** ([user guide](docs/user-guide.md)). STEP files and `.nfcad` documents open and display in four
> navigable views (wireframe / surfaces / shaded); objects, faces, edges and vertices can be
> selected, with property and layer panels; the magnetic cursor snaps to points, ends, mids,
> centres, axis cut points, intersections, face corners and edges; curves are drawn with
> commands, snaps and typed coordinates ([commands](docs/commands.md)); solids, Booleans,
> transforms, arrays, surfaces from curves (extrude, revolve, loft, sweep, Coons, Gordon,
> offsets, fillets), editing (join, explode, cap, split, trim, intersect), analysis and STEP /
> STL export work, on macOS, Windows 11 (installer) and AlmaLinux / RHEL 10 (RPM).
> Next: release 1.0 ([project plan](docs/plan.md)).

## Features (planned)

| Area | Content | Milestone |
|---|---|---|
| Layout | tool column (~10 % width) with collapsible icon groups, 2×2 viewports Front / Left / Top / 3D | M0–M1 |
| Display | wireframe, surfaces, shaded – one click per mode, per view or global | M2 |
| Selection | pick, window / crossing, hover highlight, property panel | M3 |
| Object snaps | point, end, mid, centre, quadrant, near, intersection, face corners and edges – magnetic cursor | M4 |
| Commands | points, lines, circles, arcs, polygons, helices, coordinate input | M5 |
| Modelling | extrude, revolve, loft, sweep, Coons/Gordon, fillets, solids, Booleans, transforms | M6 |
| Platforms | macOS ✅ (development), Windows 11 and AlmaLinux / RHEL 10 | M7 |

## Quick start (macOS)

```bash
brew install qt cmake ninja
git clone https://github.com/PhotonPower/Raytatouille-Fusion.git
cmake --preset debug && cmake --build --preset debug
open build/debug/RaytatouilleFusion.app
```

Nurbforge is taken from an installed package, a checkout in `../Nurbforge`, or downloaded –
see [docs/build/macos.md](docs/build/macos.md).

## Platforms

| Platform | Toolchain | Guide | Status |
|---|---|---|---|
| macOS ≥ 14 (Apple Silicon, Intel) | Apple Clang, Qt ≥ 6.8, CMake ≥ 3.24 | [macos.md](docs/build/macos.md) | development platform |
| Windows 11 | MSVC 2022/2026, Qt ≥ 6.8 | [windows11.md](docs/build/windows11.md) | supported (installer) |
| AlmaLinux / RHEL 10 | GCC 14, Qt ≥ 6.8 | [almalinux10.md](docs/build/almalinux10.md) | supported (RPM) |

## Documentation

- [Project plan](docs/plan.md) · [UI concept](docs/ui-concept.md) · [Object snaps](docs/snapping.md)
- [Rendering](docs/rendering.md) · [Architecture](docs/architecture.md) · [ADRs](docs/adr/README.md)
- [Contributing](CONTRIBUTING.md) · [Changelog](CHANGELOG.md)

## Repository layout

```
src/        app, ui, viewport, render, commands, snap, document, tessellation, kernel, platform
resources/  icons (SVG), shaders, styles, platform files (Info.plist, .rc, .desktop)
tests/      Qt Test unit tests
docs/       documentation and ADRs
cmake/      CMake helpers
packaging/  Windows installer, RPM spec, macOS DMG script
```

## License

MIT, see [LICENSE](LICENSE). Qt is used under the LGPL-3 (dynamic linking).
