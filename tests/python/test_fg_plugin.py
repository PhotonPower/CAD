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
    "sweep": {"path": "layers.0.thickness_um", "values": [0.1, 0.2]}, "output": {"dir": "out"},
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
with tempfile.TemporaryDirectory() as d:
    assert fg_scan.write_csv({**cols, "amp": [1 + 2j] * len(cols["n"])}, Path(d) / "s.csv") == len(cols["n"])
    head = (Path(d) / "s.csv").read_text().splitlines()[0]
    assert head.endswith("efficiency,amp_re,amp_im"), head

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

    def curve(self, run, name, x, y, **k):
        self.out.setdefault("curves", {})[name] = (x, y)

    def set_params(self, values):
        self.params.update(values)


with tempfile.TemporaryDirectory() as d:
    src, dst = Path(d) / "in.json", Path(d) / "out.json"
    src.write_text(json.dumps(LAMELLAR))
    ctx = Ctx({"config_file": str(src), "save_as": str(dst)})
    plugin = FourierGratingPlugin()
    plugin.load_config(ctx)
    plugin.solve(ctx)
    plugin.save_config(ctx)
    assert json.loads(dst.read_text()) == LAMELLAR
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
