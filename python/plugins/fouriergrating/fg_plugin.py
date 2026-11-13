"""FourierGrating plugin (ADR 0010, stage W4): the functions of GUI-FourierGrating in Raytatouille
Fusion. Step 1: configuration in the panel (structure, materials, layers, incidence; everything
else as JSON), load and save, single solve with the order table and the efficiency per order.
"""

from __future__ import annotations

import importlib
import json
from pathlib import Path

from nfcad import Param, Plugin, UserError, action

import fg_config
import fg_nearfield
import fg_optimize
import fg_scan

_LAMELLA = {
    "from": Param.number(0.0, decimals=6, unit="µm", label="from x"),
    "material": Param.text("air", label="material"),
}
_LAYER = {
    "type": Param.choice(["homogeneous", "lamellar1d", "json"], "homogeneous", label="Type",
                         description="json: any other layer type of FourierGrating, edited as JSON"),
    "thickness": Param.number(0.1, min=0.0, decimals=6, unit="µm", label="Thickness"),
    "material": Param.text("air", label="Material (homogeneous)"),
    "lamellae": Param.table(_LAMELLA, label="Lamellae (lamellar1d)",
                            description="each lamella from x up to the next one; the last up to 'End'"),
    "end": Param.number(0.0, decimals=6, unit="µm", label="End (lamellar1d)"),
    "json": Param.json(None, label="Layer as JSON (type json)"),
}
_MATERIAL = {
    "name": Param.text("air", label="Name"),
    "kind": Param.choice(["n", "json"], "n", label="Given as",
                         description="n: refractive index n + i k; json: any other form (ε, tensors)"),
    "n": Param.number(1.0, decimals=6, label="n"),
    "k": Param.number(0.0, decimals=6, label="k"),
    "json": Param.json(None, label="Material as JSON (json)"),
}


def _fg():
    try:
        import fouriergrating
    except ImportError as e:
        raise UserError("The FourierGrating plugin needs the Python package fouriergrating in the plugin "
                        "environment (pip install fouriergrating, or the wheel of the FourierGrating repo).") from e
    return fouriergrating


class FourierGratingPlugin(Plugin):
    params = {
        "config_file": Param.file("", filter="FourierGrating configuration (*.json)", label="Configuration",
                                  group="File", description="Load configuration reads it into the panel"),
        "save_as": Param.file("", filter="FourierGrating configuration (*.json)", save=True, label="Save as",
                              group="File"),
        "dimension": Param.choice(["1D", "2D"], "1D", label="Dimension", group="Structure"),
        "solver": Param.choice(["rcwa", "rk"], "rcwa", label="Solver", group="Structure",
                               description="rcwa: slices; rk: differential method (implicit Runge-Kutta)"),
        "nx": Param.integer(10, min=0, max=200, label="Orders Nx", group="Structure"),
        "nz": Param.integer(0, min=0, max=200, label="Orders Nz", group="Structure"),
        "period_x": Param.number(1.0, min=0.0, decimals=6, unit="µm", label="Period x", group="Structure"),
        "period_z": Param.number(0.0, min=0.0, decimals=6, unit="µm", label="Period z", group="Structure"),
        "superstrate": Param.text("air", label="Superstrate", group="Structure"),
        "substrate": Param.text("glass", label="Substrate", group="Structure"),
        "lambda_um": Param.text("0.6328", label="λ / µm", group="Incidence",
                                description="a number, a list (0.5, 0.6) or a range from:to:steps"),
        "phi_deg": Param.text("0", label="φ / deg", group="Incidence"),
        "psi_deg": Param.text("0", label="ψ / deg", group="Incidence"),
        "delta_deg": Param.text("0", label="δ / deg", group="Incidence", description="0: s (TE), 90: p (TM)"),
        "materials": Param.list(_MATERIAL, [{"name": "air", "kind": "n", "n": 1.0, "k": 0.0, "json": None},
                                            {"name": "glass", "kind": "n", "n": 1.5, "k": 0.0, "json": None}],
                                item_label="name", label="Materials"),
        "layers": Param.list(_LAYER, item_label="type", label="Layers (bottom to top)"),
        "scan_x": Param.choice(["auto", *fg_scan.AXES], "auto", label="x axis", group="Scan",
                               description="auto: the quantity with the most values; the other varying ones give separate curves"),
        "scan_min": Param.number(0.01, min=0.0, max=1.0, decimals=4, label="Orders from efficiency", group="Scan",
                                 description="an order gets a curve if it reaches this efficiency somewhere"),
        "scan_max_curves": Param.integer(24, min=1, max=500, label="At most curves", group="Scan"),
        "scan_csv": Param.file("", filter="CSV (*.csv)", save=True, label="Also write CSV", group="Scan",
                               description="every order at every point, as FourierGrating's efficiencies.csv"),
        "nf_show": Param.choice(list(fg_nearfield.QUANTITIES), "|E|²", label="Show", group="Near field"),
        "nf_y_from": Param.number(-0.2, decimals=6, unit="µm", label="y from", group="Near field",
                                  description="the vertical cut spans y from … to (0 = bottom of the stack)"),
        "nf_y_to": Param.number(0.5, decimals=6, unit="µm", label="y to", group="Near field"),
        "nf_horizontal_y": Param.number(0.1, decimals=6, unit="µm", label="Horizontal cut at y", group="Near field"),
        "nf_vertical_z": Param.number(0.0, decimals=6, unit="µm", label="Vertical cut at z (2D)", group="Near field"),
        "nf_nx": Param.integer(400, min=2, max=4000, label="Points x", group="Near field"),
        "nf_ny": Param.integer(300, min=2, max=4000, label="Points y", group="Near field"),
        "nf_nz": Param.integer(200, min=2, max=4000, label="Points z (2D)", group="Near field"),
        "nf_periods": Param.integer(2, min=1, max=20, label="Periods", group="Near field"),
        "opt_goal": Param.choice(["maximise", "minimise"], "maximise", label="Goal", group="Optimisation"),
        "opt_terms": Param.table({"side": Param.choice(["R", "T"], "R", label="side"),
                                  "n": Param.integer(-1, min=-1000, max=1000, label="n"),
                                  "m": Param.integer(0, min=-1000, max=1000, label="m"),
                                  "weight": Param.number(1.0, decimals=4, label="weight")},
                                 label="Objective L = Σ weight · efficiency", group="Optimisation"),
        "opt_params": Param.table({"pointer": Param.text("/layers/0/thickness_um", label="JSON pointer"),
                                   "lower": Param.number(0.0, decimals=6, label="lower"),
                                   "upper": Param.number(1.0, decimals=6, label="upper")},
                                  label="Parameters", group="Optimisation",
                                  description="numbers of the configuration by JSON pointer, e.g. "
                                              "/layers/0/thickness_um or /layers/0/breakpoints_um/1"),
        "opt_iterations": Param.integer(50, min=1, max=10000, label="Iterations", group="Optimisation"),
        "opt_apply": Param.boolean(True, label="Apply the optimum to the panel", group="Optimisation"),
        "advanced": Param.json({}, label="Further settings (JSON)", group="Advanced",
                               description="solver options, sweeps, near field, output – as in the configuration file"),
        "_order": Param.json([], hidden=True),
        "_present": Param.json(None, hidden=True),
    }

    # ------------------------------------------------------------------ blocks (ADR 0010, W4)
    def _block_provider(self):
        """RTT-Fusion's provider of this plugin's blocks: the grating block (`fg.grating`, before
        RTT-Fusion's rename `fusion.grating`) with RTT-Fusion's evaluator, evaluated in this process.
        None without RTT-Fusion or one with remote blocks. A grating with a stored Jones table needs
        no FourierGrating; one with a configuration builds its table with fouriergrating."""
        if getattr(self, "_provider", None) is None:
            try:
                remote = importlib.import_module("rtt_fusion.graph.remote")
                grating = importlib.import_module("rtt_fusion.graph.grating")
            except ImportError:
                return None
            self._provider = remote.BlockProvider([grating.TYPE], dict(grating.EVALUATORS))
        return self._provider

    def blocks(self) -> list:
        provider = self._block_provider()
        return provider.catalogue() if provider is not None else []

    def evaluate_block(self, request: dict) -> dict:
        provider = self._block_provider()
        if provider is None:
            return {"ok": False, "error": "the FourierGrating plugin needs RTT-Fusion with remote blocks"}
        return provider.handle(request) if hasattr(provider, "handle") else provider.evaluate(request)

    # ------------------------------------------------------------------ configuration
    def config(self, ctx) -> dict:
        """The configuration of the panel (FourierGrating's JSON format)."""
        try:
            return fg_config.to_config(ctx.params)
        except (ValueError, KeyError) as e:
            raise UserError(f"The panel does not give a valid configuration: {e}") from e

    @action("Load configuration", description="Read a FourierGrating configuration (.json) into the panel")
    def load_config(self, ctx):
        path = ctx.params["config_file"]
        if not path:
            raise UserError("Choose the configuration first: File → Configuration.")
        try:
            cfg = json.loads(Path(path).read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            raise UserError(f"{Path(path).name} cannot be read: {e}") from e
        values = fg_config.to_params(cfg)
        ctx.set_params(values)
        return (f"{Path(path).name}: {len(values['materials'])} materials, {len(values['layers'])} layers"
                + (" (some as JSON)" if any(x["type"] == "json" for x in values["layers"]) else ""))

    @action("Save configuration", description="Write the panel's configuration as .json (Save as, else Configuration)")
    def save_config(self, ctx):
        path = ctx.params["save_as"] or ctx.params["config_file"]
        if not path:
            raise UserError("Choose where to save: File → Save as.")
        Path(path).write_text(json.dumps(self.config(ctx), indent=2) + "\n", encoding="utf-8")
        return f"saved {Path(path).name}"

    # ------------------------------------------------------------------ solve
    @action("Solve", description="Efficiencies of every order for the incidence (the first point of a scan)")
    def solve(self, ctx):
        fg = _fg()
        cfg = self.config(ctx)
        ctx.progress(0.1, "solving …")
        try:
            sim = fg.Simulation(cfg)
            res = sim.solve()
        except Exception as e:  # noqa: BLE001 - FourierGrating's message is for the user
            raise UserError(f"FourierGrating: {e}") from e
        run = ctx.result_set(f"FourierGrating λ = {fg_config.incidence_text(cfg['incidence'].get('lambda_um', ''))} µm")
        rows, labels, values = [], [], []
        total = {"R": 0.0, "T": 0.0}
        for side, orders in (("R", res.reflected), ("T", res.transmitted)):
            for o in orders:
                n, m = int(o["n"]), int(o["m"])
                order = f"{n}" if cfg.get("dimension", "1D") == "1D" else f"{n},{m}"
                rows.append([side, n, m, float(o["eff"]), float(o["eff_s"]), float(o["eff_p"])])
                labels.append(f"{side} {order}")
                values.append(float(o["eff"]))
                total[side] += float(o["eff"])
        ctx.table(run, "Orders", ["side", "n", "m", "efficiency", "s part", "p part"], rows)
        ctx.bars(run, "Efficiency per order", labels, values, y_label="efficiency")
        absorption = float(res.absorption)
        diagnostics = res.diagnostics if isinstance(res.diagnostics, dict) else {}
        balance = diagnostics.get("energy_balance")
        for w in diagnostics.get("warnings", []) or []:
            ctx.log(f"warning: {w}")
        ctx.log(f"ΣR = {total['R']:.6g}, ΣT = {total['T']:.6g}, A = {absorption:.6g}"
                + (f", energy balance {balance:.6g}" if balance is not None else ""))
        return f"ΣR {total['R']:.4f}, ΣT {total['T']:.4f}, A {absorption:.4f} ({len(rows)} orders)"

    @action("Scan", description="Every point of the λ / angle / polarisation lists and ranges and of a sweep: "
                                "efficiency per order as curves")
    def scan(self, ctx):
        fg = _fg()
        cfg = self.config(ctx)
        ctx.progress(0.05, "scanning …")
        try:
            res = fg.solve_scan(cfg)
        except Exception as e:  # noqa: BLE001 - FourierGrating's message is for the user
            raise UserError(f"FourierGrating: {e}") from e
        columns = res.columns
        try:
            x_key = fg_scan.x_axis(columns, ctx.params["scan_x"])
        except ValueError as e:
            raise UserError(str(e)) from e
        sweep = res.sweep_parameter or "sweep"
        found = fg_scan.curves(columns, x_key, float(ctx.params["scan_min"]), sweep_label=sweep)
        limit = int(ctx.params["scan_max_curves"])
        if len(found) > limit:
            ctx.log(f"warning: {len(found)} curves, the first {limit} are shown – raise 'Orders from efficiency' "
                    "or 'At most curves'")
            found = found[:limit]
        x_label = sweep if x_key == "sweep_value" else fg_scan.AXES[x_key]
        run = ctx.result_set(f"FourierGrating scan over {x_label}")
        for c in found:
            ctx.curve(run, c.name, c.x, c.y, x_label=x_label, y_label="efficiency")
        points = len(res.points)
        written = ""
        if ctx.params["scan_csv"]:
            rows = fg_scan.write_csv(columns, ctx.params["scan_csv"])
            written = f", {rows} rows in {Path(ctx.params['scan_csv']).name}"
        return f"{points} points, {len(found)} curves over {x_label}{written}"

    @action("Near field", description="The field in the grating at the first incidence point: the vertical cut "
                                      "(x–y) and the horizontal cut as images or a curve")
    def near_field(self, ctx):
        fg = _fg()
        cfg = self.config(ctx)
        cfg["nearfield"] = {**cfg.get("nearfield", {}), **fg_config.nearfield(ctx.params, complete=True),
                            "enabled": True}
        ctx.progress(0.05, "near field …")
        try:
            cuts = fg.near_field(cfg)
        except Exception as e:  # noqa: BLE001 - FourierGrating's message is for the user
            raise UserError(f"FourierGrating: {e}") from e
        quantity = ctx.params["nf_show"]
        inc = cuts.get("incidence", {})
        run = ctx.result_set(f"FourierGrating near field λ = {inc.get('lambda_um', '?')} µm")
        shown = []
        for name in ("vertical", "horizontal"):
            if name not in cuts:
                continue
            try:
                view = fg_nearfield.show(cuts[name], quantity)
            except (KeyError, ValueError) as e:
                raise UserError(str(e)) from e
            title = f"{quantity} – {name} cut"
            if isinstance(view, fg_nearfield.Line):
                ctx.curve(run, title, view.x, view.y, x_label=view.x_label, y_label=quantity)
            else:
                ctx.image(run, title, view.values, view.width, view.height, extent=view.extent,
                          x_label=view.x_label, y_label=view.y_label, colormap=view.colormap,
                          value_range=view.value_range,
                          description=str(cuts[name].get("meta", {}).get("normalization", "")))
            shown.append(name)
        return f"{quantity}: {', '.join(shown)} cut" + ("s" if len(shown) > 1 else "")

    @action("Optimise", description="L-BFGS-B with FourierGrating's exact gradients: the parameters within their "
                                    "bounds for the objective, at the first incidence point")
    def optimise(self, ctx):
        fg = _fg()
        cfg = self.config(ctx)
        try:
            terms = fg_optimize.objective_of(ctx.params["opt_terms"])
            parameters = fg_optimize.parameters_of(ctx.params["opt_params"], cfg)
        except (KeyError, ValueError) as e:
            raise UserError(str(e).strip("'\"")) from e
        pointers = [p.pointer for p in parameters]
        start = [float(fg_optimize.pointer_get(cfg, p)) for p in pointers]
        try:
            sim = fg.Simulation(cfg)
        except Exception as e:  # noqa: BLE001 - FourierGrating's message is for the user
            raise UserError(f"FourierGrating: {e}") from e

        def value_and_grad(x):
            for p, v in zip(pointers, x, strict=True):
                sim.set_parameter(p, v)
            L, g = sim.value_and_grad(terms, pointers)
            return float(L), [float(v) for v in g]

        run = ctx.result_set("FourierGrating optimisation")
        curve = ctx.curve(run, "objective L", x_label="evaluation", y_label="L")
        limit = int(ctx.params["opt_iterations"])

        def step(k, L, x):
            ctx.append(curve, k, L)
            ctx.progress(min(0.99, k / (2.0 * limit)), f"L = {L:.6g}")

        try:
            out = fg_optimize.optimize(value_and_grad, start, parameters,
                                       maximize=ctx.params["opt_goal"] == "maximise", iterations=limit,
                                       step=step, stop=ctx.cancelled)
        except Exception as e:  # noqa: BLE001 - FourierGrating's message is for the user
            raise UserError(f"FourierGrating: {e}") from e
        ctx.table(run, "Parameters", ["parameter", "start", "optimum", "lower", "upper"],
                  [[p.pointer, s, v, p.lower, p.upper] for p, s, v in zip(parameters, out.start, out.values, strict=True)])
        ctx.log(f"L {out.start_objective:.6g} → {out.objective:.6g} in {out.steps} evaluations ({out.message})")
        applied = ""
        if ctx.params["opt_apply"] and out.steps > 0:
            ctx.set_params(fg_config.to_params(out.config(cfg, parameters)))
            applied = ", applied to the panel"
        return (f"L {out.start_objective:.4g} → {out.objective:.4g}" + (" (stopped)" if out.stopped else "")
                + applied)
