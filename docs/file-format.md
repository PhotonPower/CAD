# Document format `.rttf`

Documents are saved with the extension **`.rttf`** (Raytatouille Fusion file). Files with the
extension `.nfcad` from before 1.0 have the same content and are still opened; *Save As*
offers them as `.rttf`. Inside, the format is still called `"nfcad"`.

Version 2 (P1; version 1 files are still read). UTF-8 JSON, written atomically (`QSaveFile`).
Units: millimetres.
Geometry is stored exactly: degrees, knot vectors, control points and weights – no tessellation.

```json
{
  "format": "nfcad",
  "version": 1,
  "units": "mm",
  "layers":   [ { "id": 0, "name": "Default", "color": "#bec6d2", "visible": true } ],
  "groups":   [ { "id": 1, "name": "objective", "parent": 0, "visible": true } ],   // v2
  "entities": [ { "id": "1", "name": "part_0_0", "layer": 0, "visible": true,
                  "color": "#ff0000",            // optional, default: layer colour
                  "group": 1,                    // v2, optional (0 / missing: top level)
                  "attributes": { "rtt.role": "mirror", "sim.inactive": true },   // v2, optional
                  "geometry": { "type": "shell", "faces": [ ... ] } } ],
  "plugins":  { "demo": { "params": { "rays": 300, "origin": [0, 0, 0] } } },  // v2, optional
  "workflow": { "graph": { "format": "rtt-fusion-graph", ... }, "types": [], "diagnostics": [], "plan": {} }  // v2, optional
}
```

| `type` | Fields |
|---|---|
| `point` | `point`: [x, y, z] |
| `curve` | `degree`, `knots`, `points` (flat x, y, z list), `weights` (omitted if all 1) |
| `surface` | `degree_u`, `degree_v`, `knots_u`, `knots_v`, `num_u`, `num_v`, `points` (row-major, i along u), `weights` |
| `trimmed_surface` | `surface`, `outer` and `holes` (lists of 2D curves in parameter space, z = 0; omitted if untrimmed), `label` |
| `shell` | `faces`: list of trimmed surfaces; topology (edges, vertices) is rebuilt by sewing on load |

Groups form the model tree (`parent` 0 = top level). Attributes are free key / value data
(strings, numbers, booleans); plugins use namespaced keys (`rtt.*`), `sim.inactive = true`
excludes an object from computations ([plugins.md](plugins.md), D11).

`plugins` holds the values of each plugin's parameter panel by plugin id (P4,
[plugin-api.md](plugin-api.md)); readers that do not know a plugin keep its entry unchanged.

Entity ids are strings (64-bit). Readers reject newer versions and invalid geometry with a
message; the document is unchanged in that case.

Planned: binary blocks for large control nets (version 2), stored B-rep topology instead of
re-sewing, units other than mm.
