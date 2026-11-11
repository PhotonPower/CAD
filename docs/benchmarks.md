# Benchmarks

`RaytatouilleFusion --benchmark <frames> <file>` loads a file, waits for the tessellation, orbits the
perspective view for `<frames>` frames and prints timings. The demo model comes from
`tools/make_demo_model` (plates with holes from Booleans, spheres, tori, cones, cylinders):

```bash
cmake --preset release && cmake --build --preset release
build/release/tools/make_demo_model demo.step 20 20          # 400 solids, 1200 faces
build/release/RaytatouilleFusion.app/Contents/MacOS/RaytatouilleFusion --benchmark 300 demo.step
```

| Date | Machine | Model | Read + tessellate | Triangles | Orbit |
|---|---|---|---|---|---|
| 2026-11-11 | MacBook (Apple Silicon), macOS 26, Qt 6.12, Metal, MSAA 4× | demo 20×20 (1200 faces) | 2.9 s | 504 320 | 60 fps (v-sync limit) |
| 2026-11-11 (1.0, M8) | same | demo 20×20 (1200 faces) | 2.5 s (read 1.8 s) | 504 320 | not re-measured (needs a visible window) |

The M2 target (≥ 60 fps with ~1000 faces) is met; the frame rate is capped by v-sync.

## Picking and snapping

Measured by unit tests (release build, Apple Silicon):

| Test | Result | Target |
|---|---|---|
| `test_picking pickTimingOnDenseMesh` (65 k triangles) | ~1 µs per pick (1.0: 1.2 µs) | interactive |
| `test_snap_engine performanceTenThousandCurves` (10 000 lines and circles) | 0.34 ms per snap query (1.0: 0.27 ms) | < 2 ms (M4) |

Performance pass for 1.0 (M8): no regressions since M7; the plugin milestones did not touch
tessellation, picking or snapping.
