# Example models

| File | Content |
|---|---|
| `demo-parts.step` | 12 solids – plates with holes (Booleans), spheres, tori, cones, cylinders; made with `tools/make_demo_model examples/demo-parts.step 4 3` (larger grids for benchmarks, see [docs/benchmarks.md](../docs/benchmarks.md)) |
| [`../python/plugins/raytracer/examples/singlet_optim.rtt.json`](../python/plugins/raytracer/examples/singlet_optim.rtt.json) | Raytatouille system: a singlet to optimise (constant index, no glass catalogue needed) – open it with the Raytracer plugin (*Plugin* dock → *System file* → *Load system*) |

More optical systems come with Raytatouille (`examples/`, `tests/reference/`); systems with real
glasses need its catalogues (`tests/catalogs/*.agf`) under *Glass catalogues*.
