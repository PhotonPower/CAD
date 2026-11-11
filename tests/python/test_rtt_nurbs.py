"""Bridge Raytatouille -> NURBS (python/plugins/raytracer/rtt_nurbs.py): every drawn surface of
the example system stays within the tolerance of the exact optical surface. Exit code 77 (ctest:
skipped) without raytatouille or nurbforge."""
import sys
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
compiled = rt.compile(rt.load(str(ROOT / "python/plugins/raytracer/examples/singlet_optim.rtt.json")))
for i, L in enumerate(rt.layout.surfaces(compiled)):
    face = rtt_nurbs.surface_face(compiled, i, tol=1e-5)
    s = to_nurbforge({"type": "surface", **face.geometry["surface"]})
    kind, hx, hy, _ = rtt_nurbs._aperture(L, 10.0)
    rng = np.random.default_rng(i)
    t, r = rng.uniform(0, 2 * np.pi, 200), np.sqrt(rng.uniform(0, 1, 200))
    x, y = hx * r * np.cos(t), hy * r * np.sin(t)
    exact = np.stack([x, y, rt.layout.sag(compiled, i, x, y)], -1) @ np.asarray(L.rotation).T + L.translation
    drawn = np.array([s.evaluate(float(u), float(v)) for u, v in zip(x, y)])
    err = float(np.abs(drawn - exact).max())
    print(f"{L.id}: {L.shape}, {face.geometry['surface']['num_u']} control points per direction, error {err:.2e} mm")
    assert err <= 1.5e-5, (L.id, err)
    if L.kind == "stop":
        assert "holes" in face.geometry, "the stop is drawn as a ring"
rims = [rim for e in rt.layout.elements(compiled) for rim in rtt_nurbs.rim_faces(compiled, e)]
assert len(rims) == 1, len(rims)
print("ok")
