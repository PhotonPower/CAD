"""FourierGrating plugin (ADR 0010, W4): configuration <-> panel round trip (without
FourierGrating) and, if fouriergrating is installed, load -> solve -> save (skips with 77 else for
that part only)."""

import json
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "python"))
sys.path.insert(0, str(root / "python" / "plugins" / "fouriergrating"))

import fg_config  # noqa: E402

LAMELLAR = {
    "dimension": "1D", "solver": "rcwa", "truncation": {"Nx": 4}, "period_um": {"x": 0.7},
    "incidence": {"lambda_um": 0.6, "phi_deg": 10, "psi_deg": 0, "delta_deg": 0},
    "materials": {"air": {"n": 1.0}, "glass": {"n": 1.5}, "si": {"n": [3.88, 0.02]}},
    "superstrate": "air", "substrate": "glass",
    "layers": [{"type": "lamellar1d", "thickness_um": 0.2, "breakpoints_um": [0, 0.3, 0.7],
                "materials": ["glass", "air"]},
               {"type": "homogeneous", "thickness_um": 0.05, "material": "si"}],
}
OTHER = {  # forms the panel keeps as JSON: tensor material, other layer type, scan, sweep, output
    "dimension": "2D", "period_um": {"x": 1.0, "z": 1.0}, "truncation": {"Nx": 3, "Nz": 3, "li": True},
    "incidence": {"lambda_um": {"from": 0.5, "to": 0.7, "steps": 21}, "phi_deg": [0, 10], "psi_deg": 0,
                  "delta_deg": 90, "type": "plane_wave"},
    "materials": {"air": {"n": 1.0}, "co": {"eps_tensor": [[1, 0, 0], [0, 1, 0], [0, 0, 1]]}},
    "superstrate": "air", "substrate": "co",
    "layers": [{"type": "rings", "thickness_um": 0.1, "radii_um": [0.2], "materials": ["co", "air"]}],
    "sweep": {"parameter": "/layers/0/thickness_um", "values": [0.1, 0.2]}, "output": {"dir": "out"},
    "nearfield": {"enabled": True, "y_range_um": [-0.3, 0.6], "nx": 100, "quantities": ["E"]},
}

for cfg in (LAMELLAR, OTHER):
    p = json.loads(json.dumps(fg_config.to_params(cfg)))  # as the panel stores it
    back = fg_config.to_config(p)
    assert back == cfg and list(back) == list(cfg), (cfg, back)

p = fg_config.to_params(LAMELLAR)
assert p["layers"][0]["lamellae"] == [{"from": 0.0, "material": "glass"}, {"from": 0.3, "material": "air"}]
assert p["materials"][2] == {"name": "si", "kind": "n", "n": 3.88, "k": 0.02, "json": None}
assert fg_config.to_params(OTHER)["lambda_um"] == "0.5:0.7:21"
assert fg_config.incidence_value("0.5, 0.6") == [0.5, 0.6]
assert "nearfield" not in fg_config.to_config(fg_config.to_params(LAMELLAR))  # not added
p_nf = fg_config.to_params(OTHER)
assert (p_nf["nf_y_from"], p_nf["nf_nx"], p_nf["nf_ny"]) == (-0.3, 100, 300)
p_nf["nf_periods"] = 3  # changed: written; the file's keys stay, the defaults stay out
assert fg_config.to_config(p_nf)["nearfield"] == {**OTHER["nearfield"], "periods": 3}
assert fg_config.nearfield(p_nf, complete=True)["ny"] == 300
# Littrow: sin φ = −n λ / (2 Λ ν cos ψ); λ 0.6 µm, Λ 0.7 µm, air
import math  # noqa: E402

assert abs(fg_config.littrow_phi_deg(p, -1) - math.degrees(math.asin(0.6 / 1.4))) < 1e-12
assert abs(fg_config.littrow_phi_deg(p, 1) + math.degrees(math.asin(0.6 / 1.4))) < 1e-12
for order, why in ((-3, "no Littrow angle"), (0, "no Littrow angle")):
    try:
        fg_config.littrow_phi_deg(p, order)
        raise AssertionError("not refused")
    except ValueError as e:
        assert why in str(e)
p["nz"] = 5  # a value the file lacked, set off its default by the user, is written
assert fg_config.to_config(p)["truncation"] == {"Nx": 4, "Nz": 5}

try:
    import fg_scan  # numpy
except ImportError:
    print("numpy not installed: scan and solve skipped")
    sys.exit(77)

# scan curves from columns (one row per order and point), without FourierGrating: λ in 3 values,
# φ in 2; R0 everywhere, T1 only at λ = 0.5 (evanescent above), R1 always below the threshold
cols: dict = {k: [] for k in ("sweep_value", "lambda_um", "phi_deg", "psi_deg", "delta_deg", "side", "n", "m",
                              "efficiency")}
for lam in (0.5, 0.6, 0.7):
    for phi in (0.0, 10.0):
        for side, n, eff in (("R", 0, 0.1 + phi / 100), ("R", 1, 0.001), ("T", 0, 0.5)) + (
                (("T", 1, 0.3),) if lam == 0.5 else ()):
            for k, v in zip(cols, (0.0, lam, phi, 0.0, 0.0, side, n, 0, eff), strict=True):
                cols[k].append(v)
assert fg_scan.varying(cols) == ["lambda_um", "phi_deg"]
assert fg_scan.x_axis(cols) == "lambda_um"  # most values
assert fg_scan.x_axis(cols, "phi_deg") == "phi_deg"
for bad in ("psi_deg",):
    try:
        fg_scan.x_axis(cols, bad)
        raise AssertionError("not refused")
    except ValueError as e:
        assert "does not vary" in str(e)
found = {c.name: c for c in fg_scan.curves(cols, "lambda_um", 0.01)}
assert sorted(found) == sorted(f"{s} (φ = {p})" for p in (0, 10) for s in ("ΣR", "ΣT", "R 0", "T 0", "T 1")), found
assert found["R 0 (φ = 10)"].y == [0.2, 0.2, 0.2] and found["R 0 (φ = 10)"].x == [0.5, 0.6, 0.7]
assert found["T 1 (φ = 0)"].x == [0.5]  # only where it propagates
assert found["ΣT (φ = 0)"].y == [0.8, 0.5, 0.5]
# near-field cuts as pictures and lines (synthetic): rows by y, y descending is turned round
import fg_nearfield  # noqa: E402
import numpy as np  # noqa: E402

cut = {"axis1_name": "x", "axis2_name": "y", "axis1_um": [0.0, 0.1, 0.2], "axis2_um": [0.5, 0.0],
       "Ex": np.array([[1 + 1j, 2, 3], [-4, 5, 6]]), "absE2": np.array([[1.0, 2, 3], [4, 5, 6]])}
pic = fg_nearfield.show(cut, "Re Ex")
assert (pic.width, pic.height) == (3, 2) and pic.values.tolist() == [-4, 5, 6, 1, 2, 3]
assert np.allclose(pic.extent, (-0.05, 0.25, -0.25, 0.75)) and pic.value_range == (-6.0, 6.0)
assert pic.colormap == "coolwarm" and fg_nearfield.show(cut, "|E|²").value_range is None
line = fg_nearfield.show({"axis1_um": [0.0, 0.1], "absE2": np.array([1.0, 2.0])}, "|E|²")
assert isinstance(line, fg_nearfield.Line) and line.y == [1.0, 2.0]
try:
    fg_nearfield.show(cut, "|H|²")
    raise AssertionError("not refused")
except KeyError as e:
    assert "absH2" in str(e)

with tempfile.TemporaryDirectory() as d:
    assert fg_scan.write_csv({**cols, "amp": [1 + 2j] * len(cols["n"])}, Path(d) / "s.csv") == len(cols["n"])
    head = (Path(d) / "s.csv").read_text().splitlines()[0]
    assert head.endswith("efficiency,amp_re,amp_im"), head

# optimisation without FourierGrating: pointers, checks, L-BFGS-B on a known function
import fg_optimize  # noqa: E402

assert fg_optimize.pointer_get(LAMELLAR, "/layers/0/breakpoints_um/1") == 0.3
for bad_rows, why in (([{"pointer": "/layers/9/thickness_um", "lower": 0, "upper": 1}], "not in"),
                      ([{"pointer": "/superstrate", "lower": 0, "upper": 1}], "not a number"),
                      ([{"pointer": "/layers/0/thickness_um", "lower": 0.5, "upper": 1}], "outside"),
                      ([], "no parameters")):
    try:
        fg_optimize.parameters_of(bad_rows, LAMELLAR)
        raise AssertionError(f"not refused: {bad_rows}")
    except (KeyError, ValueError) as e:
        assert why in str(e), (why, e)
assert fg_optimize.objective_of([{"side": "R", "n": -1, "m": 0, "weight": 1.0},
                                 {"side": "R", "n": -1, "m": 0, "weight": 0.5}]) == {("R", -1, 0): 1.5}
try:
    import scipy  # noqa: F401

    pars = [fg_optimize.Parameter("/a", 0.0, 1.0), fg_optimize.Parameter("/b", -1.0, 0.25)]
    seen = []
    out = fg_optimize.optimize(lambda x: (-(x[0] - 0.3) ** 2 - (x[1] - 0.5) ** 2, [-2 * (x[0] - 0.3), -2 * (x[1] - 0.5)]),
                               [0.9, 0.0], pars, step=lambda k, L, x: seen.append(L))
    assert abs(out.values[0] - 0.3) < 1e-6 and abs(out.values[1] - 0.25) < 1e-12, out  # b at its bound
    assert out.objective >= out.start_objective and seen and not out.stopped
    out = fg_optimize.optimize(lambda x: (x[0], [1.0]), [0.5], pars[:1], stop=lambda: len(seen) > 0)
    assert out.stopped and out.values == [0.5] and out.steps == 0
    cfg_opt = fg_optimize.Outcome([0.123], [0.2], 0, 0, 1, "", False).config(
        LAMELLAR, [fg_optimize.Parameter("/layers/0/thickness_um", 0, 1)])
    assert cfg_opt["layers"][0]["thickness_um"] == 0.123 and LAMELLAR["layers"][0]["thickness_um"] == 0.2
except ImportError:
    print("scipy not installed: optimisation skipped")

try:
    import fouriergrating  # noqa: F401
except ImportError:
    print("fouriergrating not installed: solve skipped")
    sys.exit(77)

from fg_plugin import FourierGratingPlugin  # noqa: E402


class Ctx:
    def __init__(self, params):
        self.params, self.out = params, {}

    def progress(self, *a):
        pass

    def log(self, m):
        self.out.setdefault("log", []).append(m)

    def result_set(self, name):
        return 1

    def table(self, run, name, columns, rows, **k):
        self.out["table"] = (columns, rows)

    def bars(self, run, name, labels, values, **k):
        self.out["bars"] = (labels, values)

    def curve(self, run, name, x=(), y=(), **k):
        self.out.setdefault("curves", {})[name] = (x, y)

    def image(self, run, name, values, width, height, **k):
        self.out.setdefault("images", {})[name] = (len(values), width, height, k)

    def set_params(self, values):
        self.params.update(values)

    def append(self, curve, x, y):
        self.out.setdefault("appended", []).append((x, y))

    def cancelled(self):
        return False


with tempfile.TemporaryDirectory() as d:
    src, dst = Path(d) / "in.json", Path(d) / "out.json"
    src.write_text(json.dumps(LAMELLAR))
    ctx = Ctx({"config_file": str(src), "save_as": str(dst)})
    plugin = FourierGratingPlugin()
    plugin.load_config(ctx)
    ctx.params["solve_csv"] = str(Path(d) / "orders.csv")
    plugin.solve(ctx)
    plugin.save_config(ctx)
    assert json.loads(dst.read_text()) == LAMELLAR
    lines = (Path(d) / "orders.csv").read_text().splitlines()
    assert lines[0] == "side,n,m,efficiency,eff_s,eff_p" and len(lines) == 1 + len(ctx.out["table"][1])
    ctx.params["littrow_order"] = -1
    message = plugin.set_littrow(ctx)
    assert ctx.params["phi_deg"].startswith("25.37"), (ctx.params["phi_deg"], message)
    plugin.solve(ctx)  # at Littrow the reflected -1 order propagates
    assert any(r[0] == "R" and r[1] == -1 for r in ctx.out["table"][1])
columns, rows = ctx.out["table"]
assert columns[:4] == ["side", "n", "m", "efficiency"]
total = sum(r[3] for r in rows)
assert len(ctx.out["bars"][0]) == len(rows)
assert 0.0 < total <= 1.0 + 1e-9, total  # absorbing si: R + T < 1
print("ok", len(rows), "orders, R+T =", total)

# a real scan: λ over 5 values, two polarisations -> curves per δ
with tempfile.TemporaryDirectory() as d:
    scan_cfg = dict(LAMELLAR, incidence={"lambda_um": {"from": 0.5, "to": 0.7, "steps": 5}, "phi_deg": 10,
                                         "psi_deg": 0, "delta_deg": [0, 90]})
    ctx = Ctx({**fg_config.to_params(scan_cfg), "scan_x": "auto", "scan_min": 0.01, "scan_max_curves": 24,
               "scan_csv": str(Path(d) / "scan.csv")})
    message = plugin.scan(ctx)
    assert "10 points" in message, message
    sums = ctx.out["curves"]["ΣR (δ = 90)"]
    assert len(sums[0]) == 5 and all(0 < v < 1 for v in sums[1]), sums
    assert (Path(d) / "scan.csv").exists()
print("scan ok:", message)

# a real near field: the vertical cut as an image, the horizontal one (1D) as a curve; all as .npz
npz_dir = tempfile.TemporaryDirectory()
npz = Path(npz_dir.name) / "nf.npz"
ctx = Ctx({**fg_config.to_params(LAMELLAR), "nf_show": "|E|²", "nf_nx": 40, "nf_ny": 30, "nf_npz": str(npz)})
message = plugin.near_field(ctx)
with np.load(npz) as z:
    assert z["vertical/Ex"].shape == (30, 40) and z["vertical/Ex"].dtype == np.complex128
    assert "normalization" in json.loads(str(z["vertical/meta"]))
npz_dir.cleanup()
count, width, height, extra = ctx.out["images"]["|E|² – vertical cut"]
assert (count, width, height) == (1200, 40, 30) and extra["colormap"] == "inferno", ctx.out["images"]
x = ctx.out["curves"]["|E|² – horizontal cut"][0]
assert len(x) == 40 and abs(x[-1] - x[0] - 2 * 0.7) < 0.1, (len(x), x[0], x[-1])  # nx points over two periods
print("near field ok:", message)

# a real optimisation: the depth of the lamellar layer for the most light in R -1, from 0.25 µm
# (L = 0.00059); a scan of the depth in steps of 0.05 µm (adjoint gradient = finite differences)
# puts the maximum near 0.17 µm (L ≈ 0.00127; 0.001216 at 0.15, 0.00119 at 0.2)
start_params = fg_config.to_params(LAMELLAR)
start_params["layers"][0]["thickness"] = 0.25
ctx = Ctx({**start_params, "opt_goal": "maximise", "opt_iterations": 15, "opt_apply": True,
           "opt_terms": [{"side": "R", "n": -1, "m": 0, "weight": 1.0}],
           "opt_params": [{"pointer": "/layers/0/thickness_um", "lower": 0.05, "upper": 0.6}]})
message = plugin.optimise(ctx)
start_L, best_L = ctx.out["appended"][0][1], max(L for _, L in ctx.out["appended"])
assert abs(start_L - 0.000592) < 1e-5 and best_L > 0.00125, (start_L, best_L, message)
depth = ctx.params["layers"][0]["thickness"]  # applied to the panel
assert 0.15 < depth < 0.2, depth
print("optimisation ok:", message, "depth", depth)
