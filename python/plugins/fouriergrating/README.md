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

**Next steps:** near-field sections as images,
layers from CAD (STEP), optimisation with exact gradients, exports, and the grating blocks `fg.*`
in the node workflow.
