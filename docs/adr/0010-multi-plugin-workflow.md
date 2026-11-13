# ADR 0010 – One node workflow, blocks from many plugins

Status: **proposed** 2026-11-13 (draft for the maintainer's approval; chosen direction: "Weg 2")

**Context.** The node workflow (RTT-Fusion, [ui-concept.md](../ui-concept.md)) is run today by a
single plugin, the *Raytracer*: it holds the block catalogue, checks and plans the graph, executes
it, reports block states and turns display blocks into results. Solver blocks of other kernels
(the grating of FourierGrating, the particle of CliffordBem) run *inside* that plugin's Python
process through RTT-Fusion's adapters. Many more kernels are to follow as plugins (FourierGrating
first, taking over most of GUI-FourierGrating; then CliffordBem, hp-FEM, SpeckleBem, BWPM,
Scattuccino, …), each with its own actions and its own blocks for the workflow. Bundling every
kernel into the Raytracer's process does not scale: one environment for all kernels, one plugin
that must know every block type, no independent signing and release per kernel.

ADR 0008 stays: one Python process per plugin, JSON over a local socket, large arrays as mapped
files, one signing key. This ADR adds what a shared workflow needs.

## Decision (proposed)

**D1 – A workflow engine of its own.** A built-in process *Workflow* (Python, RTT-Fusion's model,
unrolling, checks, planner, executor and result cache) runs the node workflow; it is shipped and
signed with CAD and is no kernel plugin. It owns *Check workflow*, *Run workflow*, block states,
display blocks and the editor's diagnostics. The Raytracer becomes one block provider among others
(its own actions – load, trace, optimise, drawing – stay).

*Alternative rejected:* the engine in C++ inside CAD – RTT-Fusion's planner and executor are
Python; a C++ port of the graph (RTT-Fusion W2) can replace the engine's inside later without
changing this protocol.

**D2 – Plugins declare block types.** A plugin lists its block types in its `hello`
(`@block` in `nfcad`): type id with the plugin's id as prefix (`fg.grating`, `rtt.section`),
category, sockets and ports, parameters (the schema of the *Parameters…* dialog, including the
editable data fields of RTT-Fusion's ADR "Bidirektionale Ports, Posen, Äste und Gruppen", Nachtrag 1
– the plugin describes its blocks' data), description and a cost estimate. CAD merges the
catalogues; the editor's *Add block* menu groups them by plugin. Type ids are stable; a renamed
type keeps an alias.

**D3 – Execution by request.** The engine evaluates a block by sending `block.evaluate`
(node id, type, parameters, socket, input handles, request id) to the plugin that owns the type;
the plugin answers with output handles or an error. Messages go through CAD (plugins never talk to
each other: each process keeps its own token). One plugin evaluates one block at a time (its
worker thread); blocks of different plugins may run at the same time when the plan allows it.
*Cancel* reaches every plugin with a running evaluation.

**D4 – A common format for port data.** Values on links – ray bundles, field bundles, plane
waves, gridded fields, images, curves, numbers – are written in one versioned format defined by
RTT-Fusion (`rtt_fusion.portdata`): JSON metadata plus arrays in mapped files, as ADR 0008's large
arrays. A handle names such a file set; the engine passes handles, not data, and deletes the files
when no cached result refers to them. Every plugin with blocks depends on RTT-Fusion's port-data
package (they use its adapters anyway).

**D5 – Caches and operators.** The engine keeps the result cache (content keys as today, R23);
a cached result is a handle. Solver state – prepared handles, factorisations, L4 operators – stays
in the owning plugin's process, in its own store keyed by content hash; the engine sends the key
with the request. A restarted plugin has an empty store and recomputes.

**D6 – Kernels that make their own structure.** Some blocks need more than their inputs: the
Raytracer generates one Raytatouille system from all `rtt.*` blocks (RTT-Fusion ADR, Nachtrag 1,
stage S2). Before a
run the engine sends `blocks.prepare` with the blocks of a plugin (the model part it owns) to that
plugin; the plugin compiles what it needs and keeps it for the run.

**D7 – Missing or older plugins.** The workflow file records the provider (plugin id and version)
of every block type it uses. A block whose plugin is missing or too old stays in the document,
shown greyed with its data, and the check reports `graph.provider_missing`; nothing is lost.

**D8 – Trust.** Signing is unchanged (ADR 0009): every block provider is a signed plugin; the
engine is part of the signed application.

## Stages (proposal)

| Stage | Content | Acceptance |
|---|---|---|
| **W1 Engine** | The *Workflow* process with RTT-Fusion's executor; the Raytracer's workflow actions move there; `rtt.*` and built-in blocks still evaluated in the engine's process | every workflow test of the Raytracer plugin unchanged; the user's Linnik computes as before |
| **W2 Protocol** | `@block`, `block.evaluate`, `blocks.prepare`, `rtt_fusion.portdata`, routing in CAD, provider records | a workflow with blocks from two test plugins equals the same workflow in one process, bitwise; a crashing plugin fails its block with a diagnostic, the others keep running; a missing plugin gives `graph.provider_missing` |
| **W3 Raytracer as provider** | `rtt.*` blocks evaluated in the Raytracer's process (D6 for the system) | Linnik node-wise equals today's result to 1e−12; overhead of the transport measured and reported |
| **W4a CAD core for kernel plugins** | structured parameters and result tables (gaps G1, G2 below) | testable without a kernel, with the test plugin |
| **W4 FourierGrating plugin** | blocks `fg.*` (grating as Jones table, as S-matrix operator, stacks) and the functions of GUI-FourierGrating as actions (scope below) | the Linnik with a grating sample equals today's in-process grating (1e−12); the GUI's reference examples give the same numbers |
| **W5…** | CliffordBem, hp-FEM, SpeckleBem, BWPM, … as plugins of their own | per plugin |

The grating and particle blocks move from the Raytracer's catalogue to their kernels' plugins
(W4, then CliffordBem); old workflows keep working through type aliases.

## Scope of the FourierGrating plugin (W4)

Inventory of GUI-FourierGrating (v0.10.1, Streamlit, 31 files, about 4 100 lines; about 1 300 of
them server infrastructure – job queue, sessions, users, system page – that a desktop plugin does
not need: CAD's job runner, documents and undo take their place).

| GUI function | In the plugin | CAD facility |
|---|---|---|
| Structure: dimension, solver, truncation, periods, superstrate/substrate, incidence (number, list or range), solver options (RK, RCWA slices, backend, precision, threads, …) | parameters | panel (`Param.*`); ranges as text |
| Materials (n, ε, principal ε, tensor) and the layer stack (add, reorder, delete; homogeneous, lamellar, profile, polygon, contour, 2D profile; other types as JSON); config as JSON | parameters | **gap G1**: lists of records and tables in the panel, a JSON editor field |
| Load / save the configuration, examples | actions | `Param.file`, actions |
| Single solve: ΣR, ΣT, absorption, energy balance, order table, efficiency per order, warnings | action | **gap G2**: result tables and bar charts; warnings in the log |
| Scan over λ, angles, polarisation, sweeps: efficiency of chosen orders (total, s, p) over an axis, runs overlaid | action | curves in the Plot dock, one result set per run (live per point) |
| Near field: horizontal and vertical cuts, E, H, \|E\|², \|H\|², Poynting; \|·\|, Re, Im, phase; material boundaries | action | `ctx.image` with extent; boundaries as lines |
| CAD layers: STEP bodies → materials, slicing options, check with findings and slice preview | action | CAD-core STEP import, `Param.entity`, entity attributes; preview as image or lines |
| Optimisation: L-BFGS-B with FourierGrating's exact gradients, objective Σw·eff(side, n, m), parameters by JSON pointer with bounds, Littrow preset, apply the optimum | action | live curve; objective terms and parameters need **G1**; apply = new configuration (one undo step) |
| Cost estimate, exports (orders CSV, efficiencies.csv, near-field .npy, run archive) | actions | log; `Param.file(save=)` |
| — (new) | unit cell in the 3D view, near field on it | `ctx.add`/`ctx.field` (optional, later) |

**Gaps in the CAD core (W4a, before W4):**
- **G1 – structured parameters:** `Param.list(of=schema)` (ordered records with add / move /
  delete, each record a small form – a layer, a material, an objective term), `Param.table(columns)`
  (lamellae, polygon vertices) and `Param.json()` (a validated text editor for the rest). Without
  them the plugin could only edit the stack as raw JSON.
- **G2 – result tables and bar charts:** `ctx.table(set, name, columns, rows)` and
  `ctx.bars(set, name, labels, values)` in the *Results* dock and the *Plot* dock.
- Not needed: job queue, sessions, users, health checks (server features of the GUI).

Known limits: a running FourierGrating solve can be stopped only by ending the plugin process
(the 5 s rule of ADR 0008) – the plugin cancels between scan points and optimisation steps.

## Consequences

Each kernel becomes a plugin of its own – own environment, release and signature – and adds
blocks without touching the others; the workflow stays one graph with one plan and one cache.
Costs: a request protocol and a port-data format to maintain; data crossing process borders
(mapped files, no copy through CAD); the engine is one more process. The single-process path of
today remains the reference for W1–W4 (bitwise or 1e−12).

## Open questions for the maintainer

1. **Engine as a process of its own (D1)**, or inside the Raytracer plugin until a second provider
   exists? (Proposal: its own process from W1 on – the Raytracer must not stay special.)
2. **Parallel blocks across plugins (D3)** from the start, or sequential first? (Proposal:
   sequential in W2, parallel later – the plan is a DAG, so this is an executor change only.)
3. **Type ids:** plugin id as prefix (`fg.grating`) as proposed?
4. **FourierGrating plugin scope (W4):** as in the table above – everything of GUI-FourierGrating
   except its server infrastructure, plus the gaps G1 and G2 in the CAD core first?
5. **GUI-FourierGrating afterwards:** kept for server use (it is a web app with a job queue), or
   retired once the plugin covers it?
