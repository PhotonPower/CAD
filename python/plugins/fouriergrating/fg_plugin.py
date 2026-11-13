"""FourierGrating plugin (ADR 0010, stage W4): the functions of GUI-FourierGrating in Raytatouille
Fusion. Step 1: configuration in the panel (structure, materials, layers, incidence; everything
else as JSON), load and save, single solve with the order table and the efficiency per order.
"""

from __future__ import annotations

import json
from pathlib import Path

from nfcad import Param, Plugin, UserError, action

import fg_config

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
        "advanced": Param.json({}, label="Further settings (JSON)", group="Advanced",
                               description="solver options, sweeps, near field, output – as in the configuration file"),
        "_order": Param.json([], hidden=True),
        "_present": Param.json(None, hidden=True),
    }

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
