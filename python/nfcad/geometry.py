"""Geometry dictionaries in the .nfcad format (docs/file-format.md) and, if the ``nurbforge``
package is installed, conversion to and from its objects."""

from __future__ import annotations

from typing import Sequence


def _flat(points: Sequence) -> list[float]:
    out: list[float] = []
    for p in points:
        out.extend(float(c) for c in p)
    return out


def point(p) -> dict:
    return {"type": "point", "point": _flat([p])}


def curve(degree: int, knots: Sequence[float], points: Sequence, weights: Sequence[float] | None = None) -> dict:
    d = {"type": "curve", "degree": int(degree), "knots": [float(k) for k in knots], "points": _flat(points)}
    if weights is not None:
        d["weights"] = [float(w) for w in weights]
    return d


def polyline(points: Sequence) -> dict:
    """Degree-1 curve through the points (chord-length knots are not needed for display)."""
    n = len(points)
    knots = [0.0] + [i / (n - 1) for i in range(n)] + [1.0]
    return curve(1, knots, points)


def line(p0, p1) -> dict:
    return polyline([p0, p1])


def to_nurbforge(g: dict):
    """A nurbforge object (NurbsCurve / NurbsSurface) from a geometry dict (curves and surfaces)."""
    import nurbforge as nf

    pts = g.get("points", [])
    xyz = [tuple(pts[i:i + 3]) for i in range(0, len(pts), 3)]
    if g["type"] == "curve":
        return nf.NurbsCurve(g["degree"], g["knots"], xyz, g.get("weights", []))
    if g["type"] == "surface":
        return nf.NurbsSurface(g["degree_u"], g["degree_v"], g["knots_u"], g["knots_v"],
                               g["num_u"], g["num_v"], xyz, g.get("weights", []))
    raise ValueError(f"to_nurbforge: unsupported geometry type {g['type']!r}")


def from_nurbforge(obj) -> dict:
    """A geometry dict from a nurbforge NurbsCurve / NurbsSurface."""
    import nurbforge as nf

    def rows(points) -> list:
        try:
            import numpy as np

            return [tuple(map(float, p)) for p in np.asarray(points, dtype=float).reshape(-1, 3)]
        except ImportError:
            return [tuple(p) for p in points]

    if isinstance(obj, nf.NurbsCurve):
        return curve(obj.degree, list(obj.knots), rows(obj.control_points), list(obj.weights))
    if isinstance(obj, nf.NurbsSurface):
        return {"type": "surface", "degree_u": obj.degree_u, "degree_v": obj.degree_v,
                "knots_u": list(obj.knots_u), "knots_v": list(obj.knots_v),
                "num_u": obj.num_u, "num_v": obj.num_v,
                "points": _flat(rows(obj.control_points)),
                "weights": [float(w) for w in obj.weights]}
    raise TypeError(f"from_nurbforge: unsupported object {type(obj).__name__}")
