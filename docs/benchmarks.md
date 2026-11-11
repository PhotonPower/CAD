# Benchmarks

`NurbforgeCAD --benchmark <frames> <file>` loads a file, waits for the tessellation, orbits the
perspective view for `<frames>` frames and prints timings. The demo model comes from
`tools/make_demo_model` (plates with holes from Booleans, spheres, tori, cones, cylinders):

```bash
cmake --preset release && cmake --build --preset release
build/release/tools/make_demo_model demo.step 20 20          # 400 solids, 1200 faces
build/release/NurbforgeCAD.app/Contents/MacOS/NurbforgeCAD --benchmark 300 demo.step
```

| Date | Machine | Model | Read + tessellate | Triangles | Orbit |
|---|---|---|---|---|---|
| 2026-11-11 | MacBook (Apple Silicon), macOS 26, Qt 6.12, Metal, MSAA 4× | demo 20×20 (1200 faces) | 2.9 s | 504 320 | 60 fps (v-sync limit) |

The M2 target (≥ 60 fps with ~1000 faces) is met; the frame rate is capped by v-sync.
