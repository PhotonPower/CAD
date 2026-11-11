# Writing plugins (API version 1)

A plugin connects a computation kernel to Nurbforge CAD ([plugins.md](plugins.md)). It is a
folder with a manifest and Python code; CAD runs it in its own Python process.

```
my_plugin/
  plugin.json
  my_plugin.py
```

`plugin.json`:

```json
{ "id": "raytracer", "name": "Raytracer", "version": "0.1.0", "api": 1,
  "entry": "my_plugin:Raytracer", "description": "…" }
```

`my_plugin.py`:

```python
from nfcad import Plugin, action
from nfcad import geometry as g

class Raytracer(Plugin):
    @action("Trace")
    def trace(self, ctx):
        surfaces = ctx.entities(selection=True)        # id, name, group, attributes, geometry
        ...
        run = ctx.result_set("Trace 1")
        ctx.lines(run, "Rays", points, starts, wavelengths, unit="nm")       # NumPy or lists
        ctx.field(run, "Irradiance", detector_id, grid, w, h, unit="W/m²")
        ctx.progress(1.0, "done")
        return "100 000 rays"                            # shown in the status bar
```

Every method marked with `@action` appears in the *Plugins* menu and as a button in the
plugin's group of the tool column. A complete example: `python/examples/demo_plugin/`.

## The context (`ctx`)

| Call | Effect |
|---|---|
| `ctx.progress(value, text)` | progress bar (0 … 1) and status text |
| `ctx.log(text)` | *Plugin log* dock |
| `ctx.cancelled()` / `ctx.check_cancel()` | the user pressed Cancel (or Esc); `check_cancel` raises `Cancelled` |
| `ctx.selection` | ids of the selected objects when the action started |
| `ctx.params` | parameter values (from P4 on: the plugin panel) |
| `ctx.entities(ids=None, selection=False)` | objects as dicts; `geometry` in the [.nfcad format](file-format.md) |
| `ctx.add(geometry, name, attributes)` | new object, returns its id |
| `ctx.replace(id, geometry)` | new geometry for an object – shown immediately (live optimisation) |
| `ctx.set_attribute(id, key, value)` | attribute (None removes it) |
| `ctx.result_set(name)` | a new run in the *Results* dock |
| `ctx.lines(set, name, points, starts, values, colormap=, unit=, on_top=, value_range=)` | polylines; `points` (n, 3), `starts` = first point of each polyline plus the total; NaN ends a polyline |
| `ctx.field(set, name, entity, values, width, height, face=0, colormap=, unit=, value_range=)` | field on a face over its normalised (u, v): `values[j * width + i]` |
| `ctx.curve(set, name, x, y, x_label=, y_label=)` / `ctx.append(curve, x, y)` | curve in the *Plot* dock, extended live |

All model changes of one action are **one undo step**, also when the action is cancelled or
fails. Exceptions are reported in the status bar and the plugin log; the process keeps running.

Geometry helpers (`nfcad.geometry`): `point`, `line`, `polyline`, `curve(degree, knots,
points, weights)`; with the `nurbforge` package installed `to_nurbforge(dict)` and
`from_nurbforge(obj)` convert curves and surfaces.

## Installation and environment

- *Plugins → Settings…*: the **Python interpreter** (an environment with the kernel's
  packages, e.g. a venv with `nurbforge` and `raytatouille`) and the **plugin folders**
  (every sub-folder with a `plugin.json` is a plugin).
- Environment variables: `NFCAD_PYTHON`, `NFCAD_PLUGIN_PATH` (list), `NFCAD_PYTHONPATH`
  (location of the `nfcad` package; normally found next to the application).
- Development builds use the repository's `.venv` and `python/examples` when nothing is set.
  Create the venv with:
  ```bash
  python3.11 -m venv .venv && .venv/bin/pip install numpy ../Nurbforge
  ```

## How it works

CAD starts `python -m nfcad.host --server 127.0.0.1:<port> --plugin <folder>` with a random
token in the environment; the plugin connects over TCP on the loopback interface and
authenticates with the token (other local programs are rejected). Messages are JSON lines;
large arrays travel as temporary files that CAD maps and deletes. Actions run in a worker
thread of the plugin process, so Cancel is always received; a plugin that does not stop within
5 s is terminated. A crash of the plugin process ends only that action.

Not yet (later milestones): the parameter panel (P4), signatures and the developer mode (P5) –
until then CAD loads every plugin found in the plugin folders, so only add folders you trust.
