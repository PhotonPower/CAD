# Plugin plan: computation kernels in Raytatouille Fusion

Status: **accepted** (decisions of 2026-11-11 in section 5).

Several computation kernels build on Nurbforge – a ray tracer (Raytatouille), field solvers
(BEM / FEM / FMM), grating and writing-path tools. Raytatouille Fusion becomes their common
front end: show and edit the model, set parameters, start computations, look at results on the
model, optimise and see the optimised geometry right away.

## 1. Where does a feature go? (the hierarchy)

| Layer | Language | Contains | Rule of thumb |
|---|---|---|---|
| **Library** (Raytatouille, BEM, …) | C++ (+ Python bindings) | the physics and numerics: tracing, materials, sources, detectors, merit functions, optimisers, result data | useful without a GUI – in scripts, on a cluster, in tests |
| **Nurbforge** | C++ (+ Python) | general geometry needed by several libraries | geometry that more than one kernel needs |
| **CAD core** | C++ | *generic* visualisation and interaction: large line sets, colour maps, fields and images on faces, model tree and visibility, entity attributes, parameter panel, jobs with progress, plots, the Python host | needed by every kernel's plugin, never kernel-specific |
| **Plugin** | Python, signed | the glue for one kernel: which CAD objects are mirrors / lenses / detectors, the parameter definitions, the actions (Trace, Optimise), the conversion of results into CAD results | specific to one kernel and one workflow |

Test for "CAD core or plugin?": if a second kernel would need the same thing (a BEM solver
also shows a scalar field on faces, a FMM run also has progress and a cancel button), it belongs
in the CAD core; if only the ray tracer needs it, in the plugin; if it does not need a GUI at
all, in the library.

## 2. Example: the ray tracer

| Step | User sees | Library | CAD core (C++) | Plugin (Python) |
|---|---|---|---|---|
| Load / build model | parts in four views | – | STEP import, model tree | – |
| Assign roles | mark surfaces as mirror, lens, detector, source; materials | material catalogue | entity **attributes**, attribute editor | role / material choices (enums from the library) |
| Show / hide parts | eye icons in the model tree, *Isolate* | – | **model tree** with groups and visibility | – |
| Parameters | right panel: rays, wavelengths, source, detector resolution | defaults, valid ranges | **parameter panel** generated from a schema (numbers with units, enums, files, *pick an entity*, *pick a point with snaps*) | schema of the parameters |
| Trace | progress bar, Cancel, log | `trace(scene, params)` → ray segments, hits, detector grid (numpy arrays) | **job runner** (progress, cancel, log), **ray layer**: millions of coloured segments on the GPU, colour by wavelength / intensity / path, filter, every n-th ray | builds the scene from the CAD model, starts the job, hands the arrays to CAD |
| Detector result | irradiance as texture on the detector face, legend | detector grid in (u, v) | **field / image on a face** (needs (u, v) in display meshes), **colour map + legend**, 2D image in a **plot dock** | maps the grid to the face |
| Rays as CAD lines | selected rays become curves (snap, measure) | – | *promote result → entities* | – |
| Optimise mirror | live merit plot, mirror updates every few iterations | optimiser on NURBS control points, merit function | **live geometry replacement** (throttled, one undo step at the end), **plot dock** (convergence) | free variables, merit settings, callback per iteration |
| Compare | before / after, runs side by side | – | **result sets** per run (show / hide / delete, saved with the document) | names the runs |

## 2a. What Raytatouille already provides (v0.9.0) and what is missing

Read from the repository (`PhotonPower/Raytatouille`, `docs/architecture.md`,
`docs/feedback/gui-anforderungen.md`) and the Streamlit explorer `GUI-Raytatouille`.

| Need of the plugin | Raytatouille today | Consequence |
|---|---|---|
| Optical model | `.rtt.json` system file (JSON schema), typed read-only copies in Python | the optical model is the **source of truth**; the CAD geometry is derived from it |
| Editing with undo | `rt.Editor`: JSON Patch (RFC 6902) with inverse patches, replayable history | CAD undo steps for plugin edits store the inverse patch and call back into the plugin |
| 3D geometry | 2D only: sag / normals, profiles in planes containing the local axis, lens outlines; "tessellable 3D mesh" is still open (G2) | **bridge rtt → Nurbforge**: Nurbforge builds conics, aspheres, Zernike and XY surfaces exactly as NURBS with circular apertures; edges (cylinders) and solids from that; placed with `rt.layout.node_frames` (4×4) |
| Rays | `rt.trace.trace(record_path=True)` → `RayPaths`: positions (N, S, 3), status, weight, OPL as NumPy views; default 10 000 rays | directly a coloured line set in the ray layer (one segment per slot pair, lost rays end at `lost_at`) |
| Progress / cancel | `progress` and `cancel` in trace, optimise, tolerancing; cancel ends with status `CANCELLED`, no exception | maps 1:1 onto the CAD job runner |
| Optimisation | `rt.optim.optimize` → new `System` + patch; progress reports stages | the final result is easy; **live** updates per iteration need an iteration callback with the current system (library feature) |
| Detector image | sequential: final ray positions on the image surface (spot, PSF, MTF); detectors with irradiance come with M9 (non-sequential) | until M9: the plugin bins the hits on the image surface into a grid → face field; later the library's detector grid |
| Parameters | fields, wavelengths, aperture, paths, configurations, parameter table, material library | parameter panel schema generated from these (enums from the material library) |
| Own GUI | planned M10: PySide6 + pyvista/VTK on the public API (ADR 0012) | ❓ D9: the CAD plugin could take over the 3D part of M10 |

Hierarchy applied:

| Feature | Goes to |
|---|---|
| rtt surface → exact NURBS (conic, asphere, Zernike, XY, grid), apertures, lens bodies as solids | **library level**, because it is useful without a GUI too (STEP export of optical systems for the mechanics) – as an optional bridge module (`raytatouille.nurbs`, depends on `nurbforge`) or first in the plugin and moved later (❓ D10) |
| Iteration callback with the current system in `rt.optim.optimize` | **Raytatouille** – [#378](https://github.com/PhotonPower/Raytatouille/issues/378) |
| Binning hits on a surface into an irradiance grid | **Raytatouille** (`rt.analysis`), generally useful – [#379](https://github.com/PhotonPower/Raytatouille/issues/379) |
| Ray layer, face fields, colour maps, plot dock, model tree, job runner, parameter panel | **CAD core** |
| Excluding elements from a trace without deleting them ("inactive") | **Raytatouille** (e.g. a flag per element or via configurations) – [#380](https://github.com/PhotonPower/Raytatouille/issues/380) |
| Mapping CAD entities ↔ rtt JSON pointers, scene building, actions Trace / Optimise | **plugin** |

## 3. Architecture

```
┌─────────────────────────── Raytatouille Fusion (C++) ───────────────────────────┐
│  model tree · attributes · parameter panel · plot dock · result layers   │
│  ray layer · face fields · colour maps · job runner · undo               │
│            ▲ plugin API (versioned, docs/plugin-api.md)                   │
│  Plugin host  ── local socket (commands, JSON) + shared memory (arrays) ──┼──┐
└──────────────────────────────────────────────────────────────────────────┘  │
                                                                              ▼
┌──────────── plugin process (Python, one per plugin) ───────────────────────────┐
│  nfcad  (client package: parameters, model access, results, progress)          │
│  plugin code (signed)  ──►  raytatouille / nurbforge Python bindings (C++)     │
└───────────────────────────────────────────────────────────────────────────────┘
```

- **Separate process per plugin** (decided, D2): a crash or a hanging computation does not take
  the CAD down, Cancel can always stop it, every plugin can use its own Python environment
  (numpy, its kernel's wheels), and the UI never waits for the Python GIL. Started and
  controlled by CAD; for the user it runs "inside" the application.
- **Data exchange:** commands and small data as JSON lines over TCP on 127.0.0.1, authenticated
  with a per-process token (a Windows named pipe cannot be read and written from two threads at
  the same time); large arrays (rays, fields) as temporary files that CAD memory-maps and
  deletes – no copies through JSON.
- **Geometry exchange:** NURBS data (degrees, knots, control points, weights, trim loops) –
  the `.nfcad` JSON schema in binary form. The client package turns it into `nurbforge` Python
  objects and back, so CAD and plugins do not have to share a compiled module (no ABI
  coupling between the CAD build and the plugin's wheels).
- **API versioning:** `nfcad` plugin API with semantic versions; a plugin declares the range it
  supports, CAD refuses incompatible plugins with a clear message.

### Plugin package

```
raytracer-plugin/
  plugin.toml          id, name, version, api = ">=1,<2", entry point, Python requirements,
                       kernel library versions
  raytracer_plugin/    Python code
  icons/               optional SVG icons for the tool column group "Raytracer"
  SIGNATURE            Ed25519 signature over plugin.toml and the hashes of all files
```

Python side (sketch):

```python
from nfcad import plugin, Param, Entity, Point, Units

class Raytracer(plugin.Plugin):
    params = plugin.Schema(
        rays=Param.integer(100_000, min=1, label="Number of rays"),
        wavelengths=Param.floats([486.1, 587.6, 656.3], unit=Units.nm),
        source=Param.entity(kinds=["surface", "point"], label="Source"),
        detector=Param.entity(kinds=["surface"], label="Detector"),
    )

    @plugin.action("Trace")
    def trace(self, ctx):
        scene = build_scene(ctx.model, ctx.params)        # CAD model -> raytatouille scene
        result = raytatouille.trace(scene, progress=ctx.progress, cancel=ctx.cancelled)
        ctx.results.rays("Rays", result.segments, color_by=result.wavelength)
        ctx.results.face_field("Irradiance", ctx.params.detector, result.detector_grid,
                               colormap="inferno", unit="W/m²")

    @plugin.action("Optimise mirror")
    def optimise(self, ctx):
        for it in raytatouille.optimise(...):
            ctx.model.replace(ctx.params.mirror, it.surface, live=True)
            ctx.plots.append("Merit", it.index, it.merit)
```

Actions on the node workflow:

- `@action(..., on_workflow_edit=True)`: CAD also runs it, quietly, about 0.6 s after the user
  edited the node workflow, if the plugin is running and idle (the Raytracer redraws with it).
- `@action(..., block_types=("rtt.section",))`: an action on **one block**. It is not in the
  menu or the plugin panel; the node editor shows it in the context menu of blocks of these
  types. `ctx.target` names the block: `{"block": id, "scope": [group ids from the top level]}`
  (the level of the editor the block lies on). The Raytracer's *Section → group* is one.

### Signing (decided, D3: one publisher)

- Ed25519 signature over the manifest and the SHA-256 of every file (`nfcad-plugin sign`,
  `nfcad-plugin verify`; the private key never leaves the signer's machine / a CI secret).
  Implemented in P5: file `SIGNATURE` next to `plugin.json`, Monocypher, checked private copy –
  [ADR 0009](adr/0009-plugin-signatures.md), [plugin-api.md](plugin-api.md#signing).
- Exactly **one** trusted key – the project owner's – is built into the application; there is no
  user-managed list of publishers. Replacing the key needs a new CAD build.
- Unsigned or modified plugins are not loaded; a developer mode (explicit switch, red banner)
  loads unsigned plugins from a development folder.
- Important: a signature proves **who** wrote a plugin and that it is unchanged – it does not
  sandbox it. Signed Python code can do everything the user can do.

## 3a. Result display (P2)

`results::ResultStore` holds one `ResultSet` per run with items of three kinds – the data
interface the plugin host (P3) fills:

| Kind | Data | Display |
|---|---|---|
| `LineSet` | polylines (points + start indices), optional value per point, colour map, range, unit; `on_top` | coloured lines in all views (on top of the model by default, so rays inside lenses stay visible); NaN ends a polyline (lost ray) |
| `FaceField` | entity + face, grid of values (or RGBA image) over the face's normalised (u, v) | texture on the face (shaded: lit; surfaces / wireframe: flat), colour bar with name, unit and range in every view |
| `Curve` | x / y with labels; `appendToCurve` for live updates | Plot dock (appears with the first curve) |

Results are listed in the *Results* dock (visibility per run and item, delete a run, *Convert to
CAD curves* for rays). Colour maps: viridis, inferno, plasma, coolwarm, grey. 2D images (`ctx.image`) open in the *Image* window. Not yet: saving results with the document.

## 4. Milestones

| # | Content | Testable without a kernel? |
|---|---|---|
| **P1** ✅ | CAD core, part 1: model tree (groups, visibility, isolate), entity attributes (saved in `.nfcad`, edited in the property panel), (u, v) in display meshes | yes |
| **P2** ✅ | CAD core, part 2: result sets and layers – line sets with per-vertex colour, face fields / images with colour maps and legend, plot dock (curves), promote rays to CAD curves; *Help → Demo results* | yes, with generated demo data |
| **P3** ✅ | Plugin host: process start / stop, `nfcad` client package, channel (TCP on 127.0.0.1 with a token; arrays as mapped temporary files), model access, geometry exchange, results API, job runner (progress, cancel, log), one undo step per run – see [plugin-api.md](plugin-api.md) | yes, with a test plugin |
| **P4** ✅ | Parameter panel from the schema (numbers + units, enums, lists, files, entity pick, point pick with snaps), parameter sets saved in the document | yes |
| **P5** ✅ | Signing: manifest, `nfcad-plugin sign / verify`, trusted keys, developer mode, CI check | yes |
| **P6** ✅ | Reference plugin *Raytracer*: load / edit `.rtt.json`, bridge to Nurbforge geometry, model tree from the rtt node tree, trace with rays, image-surface irradiance as face field, optimise (final result; live once rtt has the iteration callback), convergence plot, undo via inverse patches | needs Raytatouille + the library items of section 2a |
| | P6 as built: [python/plugins/raytracer](../python/plugins/raytracer/README.md) – load (bridge rtt → NURBS in the plugin, D10), trace (rays; irradiance from `rt.analysis.irradiance_map`, #379), optimise live (#378), save; undo follows the system revision (`rtt.revision`). Open: inactive elements (#380), editing optical parameters in CAD | |
| **P7** ✅ | Distribution: Python environment check / venv (D5: no bundled Python), `.nfplugin` packages with install / uninstall, manual update check | yes |

Each milestone is one PR with tests, like M0–M7. P1 and P2 also help without plugins (model
tree, attributes, result display from scripts).

## 5. Decisions

| | Question | Decision (2026-11-11) |
|---|---|---|
| D1 | Raytatouille interface | from the repository (section 2a): Python package `raytatouille`, NumPy results, `.rtt.json` model, JSON Patch editing |
| D2 | Separate process or embedded Python? | **separate process per plugin** |
| D3 | Who signs? | **only the project owner** (one built-in key); unsigned plugins only in the explicit developer mode |
| D4 | Panel position | right dock, tab next to Properties / Layers; one group per plugin in the tool column |
| D5 | Python for plugins | **user-selected environment** (venv / conda with the kernel libraries); bundling later if needed |
| D6 | Rays | **display layer** (millions of rays) + *promote selected rays* to CAD curves |
| D7 | What do the other kernels need? | step by step; essentially **textures on geometry** (scalar fields and images on faces) – P2 makes that the central result type |
| D8 | Order | **plugins first** (P1 starts now), release 1.0 later |
| D9 | Replace the 3D part of Raytatouille's own GUI (M10)? | **yes** |
| D10 | Where does the bridge rtt → Nurbforge live? | **first in the plugin**, moved to an optional module `raytatouille.nurbs` once stable (STEP export of optical systems, meshing for other solvers) |
| D11 | Switch parts on / off | **both**: hide in the CAD view, and exclude from the computation ("inactive" attribute) – the exclusion needs support in Raytatouille (library item) |
| D12 | Mechanical parts in the trace | display only in sequential mode; obstacles once non-sequential tracing (rtt M9) exists |
