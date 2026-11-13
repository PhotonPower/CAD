# Writing plugins (API version 1)

A plugin connects a computation kernel to Raytatouille Fusion ([plugins.md](plugins.md)). It is a
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

## Parameters

`params` describes what the user enters in the **Plugin** dock (right side, tab next to
*Results*). CAD builds the form from it; the values are saved **with the document**
(`.nfcad`, per plugin) and passed to every action as `ctx.params`:

```python
from nfcad import Param, Plugin, action

class Raytracer(Plugin):
    params = {                                   # display order = dict order
        "rays":     Param.integer(10_000, min=1, label="Number of rays", group="Source"),
        "wl":       Param.numbers([486.1, 587.6, 656.3], unit="nm", group="Source"),
        "origin":   Param.point((0, 0, -100), group="Source"),
        "detector": Param.entity(kinds=["surface", "trimmed_surface", "shell"]),
        "method":   Param.choice(["sequential", "non-sequential"]),
    }
```

| Builder | Editor | `ctx.params[...]` |
|---|---|---|
| `Param.number(default, min=, max=, step=, decimals=, unit=)` | spin box with unit | `float` |
| `Param.integer(default, min=, max=, unit=)` | spin box | `int` |
| `Param.boolean(default)` | check box | `bool` |
| `Param.choice(options, default)` | combo box | `str` |
| `Param.text(default)` | line edit | `str` |
| `Param.file(default, filter=, save=)` | path + file dialog | `str` |
| `Param.files(default, filter=)` | list of files with *Add…* (several at once) / *Remove* | `list[str]` |
| `Param.numbers(default, unit=)` | comma-separated list (decimal point); invalid input is marked red and blocks the actions | `list[float]` |
| `Param.point(default)` | x, y, z + **Pick** (click in a view, with object snaps; Esc cancels) | `(x, y, z)` |
| `Param.entity(kinds=, multiple=)` | **Use selection** takes the selected objects of the given kinds (`point`, `curve`, `surface`, `trimmed_surface`, `shell`) | id, or a list with `multiple=True`; `None` if empty |
| `Param.list(of={...}, default=[], item_label=)` | ordered entries with *Add*, *Edit…* (a dialog with a form of the parameters `of`, nested lists and tables included), *Remove*, ↑, ↓; `item_label` names an entry in the list | `list[dict]`, each entry converted per field |
| `Param.table(columns={...}, default=[])` | a table edited in place; columns are number, integer, text or choice parameters (head = label and unit); a wrong cell is marked red and blocks the actions | `list[dict]` per row |
| `Param.json(default)` | any JSON value as text; invalid text is marked red and blocks the actions | the value (a copy) |

All builders also take `label=`, `group=` (a box in the panel) and `description=` (tooltip).
Structured parameters (ADR 0010, W4a) describe e.g. a grating's layer stack:

```python
lamella = {"x": Param.number(0.0, unit="µm"), "material": Param.text("air")}
params = {"layers": Param.list({"kind": Param.choice(["homogeneous", "lamellar"]),
                                 "thickness": Param.number(0.1, unit="µm"),
                                 "lamellae": Param.table(lamella)}, item_label="kind")}
```
*Reset parameters* in the panel goes back to the defaults. Changing a parameter marks the
document as modified but is not an undo step.

## The context (`ctx`)

| Call | Effect |
|---|---|
| `ctx.progress(value, text)` | progress bar (0 … 1) and status text |
| `ctx.log(text)` | *Plugin log* dock |
| `ctx.cancelled()` / `ctx.check_cancel()` | the user pressed Cancel (or Esc); `check_cancel` raises `Cancelled` |
| `ctx.selection` | ids of the selected objects when the action started |
| `ctx.params` | parameter values from the *Plugin* panel, converted to Python types (see below) |
| `ctx.set_params({name: value})` | sets parameters of this plugin (e.g. from a configuration file the user loads): saved in the document, shown in the panel, given to the next action; an unknown name is an error |
| `ctx.entities(ids=None, selection=False)` | objects as dicts; `geometry` in the [.nfcad format](file-format.md) |
| `ctx.add(geometry, name, attributes, group=0)` | new object (in a model tree group), returns its id |
| `ctx.remove(id)` | removes an object |
| `ctx.groups()` / `ctx.add_group(name, parent=0)` / `ctx.remove_group(id)` | model tree groups (id, name, parent); only empty groups can be removed |
| `ctx.replace(id, geometry)` | new geometry for an object – shown immediately (live optimisation) |
| `ctx.set_attribute(id, key, value)` | attribute (None removes it) |
| `ctx.result_set(name)` | a new run in the *Results* dock |
| `ctx.lines(set, name, points, starts, values, colormap=, unit=, on_top=, value_range=)` | polylines; `points` (n, 3), `starts` = first point of each polyline plus the total; NaN ends a polyline |
| `ctx.field(set, name, entity, values, width, height, face=0, colormap=, unit=, value_range=)` | field on a face over its normalised (u, v): `values[j * width + i]` |
| `ctx.curve(set, name, x, y, x_label=, y_label=)` / `ctx.append(curve, x, y)` | curve in the *Plot* dock, extended live |
| `ctx.image(set, name, values, width, height, extent=(x0, x1, y0, y1), x_label=, y_label=, colormap=, unit=, value_range=, description=)` | 2D image in the *Image* window (e.g. a camera image): `values[j * width + i]`, row 0 at y0, extent in physical units |
| `ctx.workflow()` / `ctx.set_workflow(wf)` | Node workflow of the document (RTT-Fusion): `{"graph": rtt-fusion-graph, "types": [...], "diagnostics": [...], "plan": planner result}`; the editor shows the planned representation per link and the conversions per block; shown and edited in the *Node workflow* window, saved in the `.rttf` file |

### Blocks for the node workflow (ADR 0010)

A plugin can provide block types to the shared node workflow; the *Workflow* plugin (the engine)
evaluates them by asking the plugin:

```python
from rtt_fusion.graph.remote import BlockProvider

class Gratings(Plugin):
    provider = BlockProvider([grating_type], {"fg.grating": evaluate_grating})

    def blocks(self):                       # sent with the hello: the catalogue entries
        return self.provider.catalogue()

    def evaluate_block(self, request):      # one block for the engine; inputs/outputs as port data
        return self.provider.handle(request)  # also "prepare": the run's graph first (ADR 0010 D6)
```

CAD routes these requests and starts a plugin when its blocks are asked for; evaluations of one
plugin run one after the other in a thread of their own (not the action's). The engine uses
`ctx.block_catalogues()` (the other plugins' types with provider id and version) and
`ctx.evaluate_block(provider, request)`; a provider that stops while evaluating fails the request.

Raise `nfcad.UserError("…")` for problems the user can fix (missing input, wrong file): CAD
shows the message as it is. Any other exception is reported as a plugin error with its
traceback in the *Plugin log*.

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
  The example plugins are not signed: switch on the developer mode (see *Signing*) to load them.
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

## Distribution

**Requirements.** `plugin.json` lists the Python packages a plugin needs:
`"requires": ["numpy", "raytatouille>=0.9"]` (import name, optionally `>=` or `==` a version).
*Plugins → Settings… → Check environment* runs the chosen interpreter and shows what is
missing, with the `pip install` command; *Create environment…* makes a new venv (with numpy)
from the interpreter in the field. Installing a plugin checks its requirements too. CAD does
not ship a Python (decision D5): plugins use the environment with your kernel libraries.

**Packages.** A plugin is distributed as a `.nfplugin` file – a zip archive of the plugin
folder with its `SIGNATURE`:

```bash
T=build/release/tools/nfcad-plugin
$T sign my_plugin ~/.raytatouille-fusion/publisher.key
$T pack my_plugin                       # -> <id>-<version>.nfplugin
```

*Plugins → Install plugin…* (or opening / passing the file to the application) checks the
signature – unsigned packages only in developer mode – and installs into the folder for
installed plugins (`<app data>/plugins/<id>`, shown in the settings; `NFCAD_PLUGIN_INSTALL_DIR`
overrides it). A newer version replaces the installed one; installed plugins take precedence
over plugins with the same id in other folders. *Plugins → Uninstall plugin* removes them.
Archive paths must stay inside the plugin folder; packages are limited to 256 MB unpacked.

**Updates.** *Help → Check for updates…* asks, only when chosen, the GitHub releases of
Raytatouille Fusion and every plugin with an `update_url` in its `plugin.json` – a URL of a
JSON document `{"version": "1.2.0", "url": "<download page or .nfplugin>"}` – and lists newer
versions with their links. Requests send no personal data (user agent
`RaytatouilleFusion/<version>`); downloaded packages are installed with *Install plugin…*.

## Signing

CAD loads only plugins **signed by the publisher** whose public key is built into the
application (`keys/trusted-publishers.txt`, decision D3; [ADR 0009](adr/0009-plugin-signatures.md)).
A signed plugin runs from a private copy of exactly the signed files.

```bash
T=build/release/tools/nfcad-plugin
$T keygen ~/.raytatouille-fusion/publisher.key   # once; prints "ed25519:…" for keys/trusted-publishers.txt
$T sign   path/to/my_plugin ~/.raytatouille-fusion/publisher.key   # writes my_plugin/SIGNATURE
$T verify path/to/my_plugin                      # against the built-in keys
```

Sign again after every change – a modified file invalidates the signature. Keep the secret
key out of the repository and back it up; whoever has it can publish plugins that CAD runs.
Signed plugins inside the repository are checked by the `signed_plugins` test.

**Developer mode** (*Plugins → Settings… → Developer mode*, or `NFCAD_DEVELOPER_MODE=1`)
also loads unsigned plugins – e.g. the one you are writing – from their folder. They appear as
*Name [unsigned]*, and a red *DEVELOPER MODE* banner stays in the status bar. A signature proves
who wrote a plugin and that it is unchanged; it is not a sandbox.
