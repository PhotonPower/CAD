# FourierGrating plugin

Diffraction by periodic structures with [FourierGrating](https://github.com/PhotonPower/FourierGrating)
in Raytatouille Fusion (ADR 0010, stage W4). Needs the Python package `fouriergrating` (≥ 0.18) in
the plugin environment.

**Step 1 (this version)**

- **Panel:** structure (dimension, solver, orders, period, super- and substrate), incidence
  (λ, φ, ψ, δ: a number, a list `0.5, 0.6` or a range `0.5:0.7:21`), materials (n + i k, any
  other form as JSON) and layers bottom to top (homogeneous, lamellar with a lamella table, any
  other layer type as JSON). Every further setting of the file (solver options, sweeps, near
  field, output) stays unchanged under *Advanced*.
- **Load configuration / Save configuration:** FourierGrating's JSON format
  (`docs/config_format.md`); loading and saving again gives the same file.
- **Solve:** the efficiencies of every reflected and transmitted order in the *Table* window and
  as bars in the *Plot* window; ΣR, ΣT, absorption and the energy balance in the log. For a scan
  (list or range) the first point is solved.

**Step 2: Scan.** Every point of the λ, angle and polarisation lists and ranges and of a
`sweep` (under *Advanced*, e.g. `{"parameter": "/layers/0/thickness_um", "values": {"from": 0.1,
"to": 0.3, "steps": 11}}`), computed in parallel by FourierGrating. The *Plot* window shows ΣR,
ΣT and every order that reaches *Orders from efficiency* somewhere, over the quantity with the
most values (or the chosen *x axis*); every further varying quantity gives its own curves
(e.g. `R 0 (δ = 90)`). An order that is evanescent at some points has no points there.
*Also write CSV*: every order at every point, as FourierGrating's `efficiencies.csv`.

**Step 3: Near field.** The field in the grating at the first incidence point: the vertical
cut (x–y) in the *Image* window, the horizontal cut at *Horizontal cut at y* as a curve (1D
grating) or an image (x–z, 2D grating). *Show*: |E|², |H|², the power flow Sy, the amount or the
real part of a component (real parts and Sy with a colour scale symmetric about 0), or the
material. Range, points and periods are fields of the *Near field* group; they go into the
configuration file only if it had a `nearfield` block or you changed them. Further keys
(`quantities`, `reconstruction`, `field`) stay under *Advanced*. Normalisation as in
FourierGrating: |E_inc| = 1, H as Z0·H.

**Step 4: The grating block in the node workflow.** The plugin provides `fg.grating` (in older
RTT-Fusion `fusion.grating`; old workflows load through RTT-Fusion's type aliases): a periodic
sample that sends the diffraction orders on, from a stored Jones table or – with a FourierGrating
configuration – a table built around the incident rays. The workflow engine sends this block to
the plugin's process; without the plugin the engine computes it itself, with the same result
(RTT-Fusion's evaluator in both, compared bit for bit in `test_multi_plugin_workflow`).

**Step 5: Optimise.** L-BFGS-B (scipy) with FourierGrating's exact gradients (adjoint, about 1.3
to 2.5 solves per step whatever the number of parameters) at the first incidence point:
- *Objective*: rows (side R/T, n, m, weight), L = Σ weight · efficiency, maximised or minimised;
- *Parameters*: numbers of the configuration by JSON pointer (`/layers/0/thickness_um`,
  `/layers/0/breakpoints_um/1`, `/period_um/x`, …) with bounds; the start must lie within them;
- L per evaluation as a live curve, the parameters (start, optimum, bounds) as a table; *Cancel*
  stops after the current evaluation with the best point so far; *Apply the optimum* writes it
  into the panel.

**Next steps:** layers from CAD (STEP), exports, a Littrow preset, further blocks (`fg.*`: grating
as S-matrix operator, stacks).
