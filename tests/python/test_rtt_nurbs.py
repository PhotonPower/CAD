"""Bridge Raytatouille -> NURBS (python/plugins/raytracer/rtt_nurbs.py): every drawn surface stays
within the tolerance of the exact optical surface – measured independently by evaluating the
NURBS with nurbforge and comparing with the sag in the surface's local frame. Exit code 77
(ctest: skipped) without raytatouille or nurbforge."""
import json
import sys
import tempfile
import warnings
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "python"), str(ROOT / "python" / "plugins" / "raytracer")]
try:
    import numpy as np
    import nurbforge  # noqa: F401 - independent evaluation of the NURBS
    import raytatouille as rt
except ImportError as e:
    print(f"skipped: {e}")
    sys.exit(77)

import rtt_nurbs
from nfcad.geometry import to_nurbforge

warnings.simplefilter("ignore")
TOL = 1e-5


def check(compiled, name):
    for i, L in enumerate(rt.layout.surfaces(compiled)):
        face = rtt_nurbs.surface_face(compiled, i, tol=TOL)
        g = face.geometry["surface"]
        s = to_nurbforge({"type": "surface", **g})
        us = np.linspace(g["knots_u"][0], g["knots_u"][-1], 41)
        vs = np.linspace(g["knots_v"][0], g["knots_v"][-1], 41)
        world = np.array([s.evaluate(float(u), float(v)) for u in us for v in vs])
        local = (world - L.translation) @ np.asarray(L.rotation)
        _, hx, hy, _ = rtt_nurbs._aperture(L, 10.0)
        inside = (local[:, 0] / hx) ** 2 + (local[:, 1] / hy) ** 2 <= 1.0 + 1e-9
        z = np.asarray(rt.layout.sag(compiled, i, local[inside, 0], local[inside, 1]))
        err = float(np.max(np.abs(z - local[inside, 2])))
        how = "revolved" if g["num_v"] == 9 and g["degree_v"] == 2 else "x-y"
        print(f"{name} {L.id}: {L.shape}, {how}, error {err:.2e} mm (estimate {face.max_error:.2e})")
        assert err <= 1.5 * TOL, (name, L.id, err)
        if L.kind == "stop":
            assert "holes" in face.geometry, "the stop is drawn as a ring"


example = ROOT / "python/plugins/raytracer/examples/singlet_optim.rtt.json"
compiled = rt.compile(rt.load(str(example)))
check(compiled, "singlet")
rims = [rim for e in rt.layout.elements(compiled) for rim in rtt_nurbs.rim_faces(compiled, e)]
assert len(rims) == 1, len(rims)

# A nearly hemispherical front lens (aperture 0.97 R, as in microscope objectives): the x-y
# parametrisation cannot follow the steep rim; the surface of revolution must.
steep = json.loads(example.read_text())
lens = next(n for n in steep["root"]["children"] if n.get("type") == "lens")
lens["surfaces"][0]["shape"]["base"]["radius"] = {"value": 5.15, "variable": False}
lens["surfaces"][0]["aperture"]["radius"] = 5.0
lens["surfaces"][1]["aperture"]["radius"] = 5.0
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / "steep.rtt.json"
    path.write_text(json.dumps(steep))
    check(rt.compile(rt.load(str(path))), "steep")
print("ok")
