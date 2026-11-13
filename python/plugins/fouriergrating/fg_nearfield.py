"""Near-field cuts of FourierGrating (``fouriergrating.near_field``) as images or curves for the
CAD's Image and Plot windows. numpy only, no FourierGrating."""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

#: what can be shown: name in the panel -> (array key, function, colormap, symmetric range)
QUANTITIES = {
    "|E|²": ("absE2", None, "inferno", False),
    "|H|²": ("absH2", None, "inferno", False),
    "Sy (power flow)": ("Sy", None, "coolwarm", True),
    **{f"|{c}|": (c, "abs", "inferno", False) for c in ("Ex", "Ey", "Ez", "Hx", "Hy", "Hz")},
    **{f"Re {c}": (c, "real", "coolwarm", True) for c in ("Ex", "Ey", "Ez", "Hx", "Hy", "Hz")},
    "material": ("material", None, "viridis", False),
}


@dataclass
class Picture:
    """A 2D cut: values[j * width + i], row 0 at the smallest axis2 value; extent = cell edges."""
    values: np.ndarray
    width: int
    height: int
    extent: tuple[float, float, float, float]
    x_label: str
    y_label: str
    colormap: str
    value_range: tuple[float, float] | None = None


@dataclass
class Line:
    """A 1D cut (the horizontal cut of a 1D grating)."""
    x: list[float]
    y: list[float]
    x_label: str
    extra: dict = field(default_factory=dict)


def _edges(axis: np.ndarray) -> tuple[float, float]:
    if len(axis) < 2:
        return float(axis[0]) - 0.5, float(axis[0]) + 0.5
    step = (axis[-1] - axis[0]) / (len(axis) - 1)
    return float(axis[0] - step / 2), float(axis[-1] + step / 2)


def values_of(cut: dict, quantity: str) -> np.ndarray:
    """The real values of `quantity` in a cut; KeyError if the cut lacks it."""
    key, fn, _, _ = QUANTITIES[quantity]
    if key not in cut:
        raise KeyError(f"the near field has no {key} (add it to nearfield.quantities under Advanced)")
    a = np.asarray(cut[key])
    if fn == "abs":
        return np.abs(a)
    if fn == "real":
        return np.real(a)
    return np.real(a).astype(float)


def show(cut: dict, quantity: str) -> Picture | Line:
    """A cut as a picture (2D) or a line (1D)."""
    _, _, colormap, symmetric = QUANTITIES[quantity]
    v = values_of(cut, quantity)
    a1 = np.asarray(cut["axis1_um"], float)
    name1 = f"{cut.get('axis1_name', 'x')} / µm"
    if v.ndim == 1:
        return Line(a1.tolist(), v.tolist(), name1)
    a2 = np.asarray(cut["axis2_um"], float)
    if v.shape != (len(a2), len(a1)):
        raise ValueError(f"near field {v.shape} does not match its axes ({len(a2)}, {len(a1)})")
    if len(a2) > 1 and a2[0] > a2[-1]:  # row 0 at the smallest value
        a2, v = a2[::-1], v[::-1]
    finite = v[np.isfinite(v)]
    value_range = None
    if symmetric and finite.size:
        m = float(np.abs(finite).max()) or 1.0
        value_range = (-m, m)
    x0, x1 = _edges(a1)
    y0, y1 = _edges(a2)
    return Picture(np.ascontiguousarray(v, dtype=np.float32).ravel(), len(a1), len(a2), (x0, x1, y0, y1),
                   name1, f"{cut.get('axis2_name', 'y')} / µm", colormap, value_range)
