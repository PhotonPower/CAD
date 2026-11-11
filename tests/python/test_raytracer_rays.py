"""Raytracer plugin: the start rays it builds for folded / tilted paths (where make_rays cannot
aim) are identical to make_rays where both work – stop first, object at infinity, several field
angles. Exit code 77 (skipped) without raytatouille."""
import json
import sys
import tempfile
import warnings
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "python"), str(ROOT / "python" / "plugins" / "raytracer")]
try:
    import numpy as np
    import raytatouille as rt
except ImportError as e:
    print(f"skipped: {e}")
    sys.exit(77)

from raytracer_plugin import RaytracerPlugin

warnings.simplefilter("ignore")
d = json.loads((ROOT / "python/plugins/raytracer/examples/singlet_optim.rtt.json").read_text())
d["fields"] = {"type": "angle_deg", "points": [{}, {"x": 1.5, "y": 3.0}, {"x": -4.0, "y": 0.5}]}
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "s.rtt.json"
    path.write_text(json.dumps(d))
    system = rt.load(str(path))
compiled = rt.compile(system)


def columns(r, names):
    return np.stack([np.asarray(getattr(r, n), dtype=float) for n in names], -1)


for rings in (1, 5, 6):
    for fields in (None, [1]):
        a = rt.trace.make_rays(compiled, rt.trace.HexapolarPupil(rings=rings), fields=fields)
        b = RaytracerPlugin._start_rays(system, compiled, rings, 0, fields, None, "test")
        assert a.size == b.size, (a.size, b.size)
        assert np.array_equal(np.asarray(a.field), np.asarray(b.field))
        assert np.allclose(columns(a, ("pupil_x", "pupil_y")), columns(b, ("pupil_x", "pupil_y")), atol=1e-12)
        rt.trace.trace(compiled, a)
        rt.trace.trace(compiled, b)
        pos = np.abs(columns(a, ("pos_x", "pos_y", "pos_z")) - columns(b, ("pos_x", "pos_y", "pos_z"))).max()
        dirs = np.abs(columns(a, ("dir_x", "dir_y", "dir_z")) - columns(b, ("dir_x", "dir_y", "dir_z"))).max()
        print(f"rings {rings}, fields {fields}: {a.size} rays, image position {pos:.1e} mm, direction {dirs:.1e}")
        assert pos < 1e-9 and dirs < 1e-12, (rings, fields, pos, dirs)
print("ok")
