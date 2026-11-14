"""Lamps and the cut-off of the low beam.

Coordinates of the design (mm): the origin is the centre of the filament, +X the driving direction
(the light leaves along +X), +Y to the left, +Z up. A direction d is seen on the measuring screen at
h = -d_y / d_x (to the right) and v = d_z / d_x (up) – the tangents of the angles H and V; a straight
line on the screen is a plane through the lamp, so every branch of the cut-off is a plane m · d = 0
with the dark side m · d > 0. NumPy only."""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np


# ------------------------------------------------------------------ lamps
@dataclass(frozen=True)
class Lamp:
    """A filament lamp as the design sees it: the coil as a cylinder (length, diameter), its
    luminous flux, and what of it reaches the reflector."""

    name: str
    length: float                 # mm, filament (coil) length
    diameter: float               # mm, coil diameter
    flux: float                   # lm at the test voltage
    axial: bool = True            # coil along the lamp axis (X); else transverse (along Y) – not yet
                                  # supported by the facets: both coil ends touch the cut-off over whole areas
    window: tuple[float, float] | None = None   # light only into azimuths atan2(z, y) in this range / deg
    note: str = ""


# Luminous flux: the nominal values of UN/ECE R37 (12 V lamps). Filament length and diameter:
# typical values of the coil – check the lamp's R37 sheet or data sheet; "Custom" takes the panel's.
LAMPS: dict[str, Lamp] = {
    "H1": Lamp("H1", 5.0, 1.4, 1550.0, note="55 W, axial coil"),
    "H4 (low beam)": Lamp("H4 (low beam)", 5.5, 1.5, 1000.0, window=(-15.0, 180.0),
                          note="55 W coil above its shield: light only into the upper 195°"),
    "H7": Lamp("H7", 4.1, 1.3, 1500.0, note="55 W, axial coil"),
    "H8": Lamp("H8", 3.7, 1.2, 800.0, note="35 W, axial coil"),
    "H9": Lamp("H9", 4.8, 1.4, 2100.0, note="65 W, axial coil"),
    "H11": Lamp("H11", 4.5, 1.3, 1350.0, note="55 W, axial coil"),
    "HB3": Lamp("HB3", 4.2, 1.4, 1700.0, note="60 W, axial coil"),
    "HB4": Lamp("HB4", 5.1, 1.4, 1000.0, note="51 W, axial coil"),
}
CUSTOM = "Custom"


@dataclass(frozen=True)
class Filament:
    """The coil as a solid cylinder about the origin."""

    axis: np.ndarray       # unit vector
    half_length: float
    radius: float

    @staticmethod
    def of(lamp: Lamp) -> "Filament":
        axis = np.array([1.0, 0.0, 0.0]) if lamp.axial else np.array([0.0, 1.0, 0.0])
        return Filament(axis, 0.5 * lamp.length, 0.5 * lamp.diameter)

    def support(self, u: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        """(σ, a) for unit vectors u (n, 3): σ = max over the cylinder of u · Q, a = u · axis."""
        a = u @ self.axis
        return self.half_length * np.abs(a) + self.radius * np.sqrt(np.maximum(1.0 - a * a, 0.0)), a

    def contact(self, u: np.ndarray, sign: np.ndarray | None = None) -> np.ndarray:
        """The point of the cylinder lowest along u (n, 3): where a plane with normal u touches it
        from below. `sign` forces the end (±1) instead of the sign of u · axis."""
        a = u @ self.axis
        s = np.where(a >= 0.0, 1.0, -1.0) if sign is None else sign
        perp = u - a[:, None] * self.axis
        norm = np.linalg.norm(perp, axis=1, keepdims=True)
        perp = perp / np.maximum(norm, 1e-300)
        return -self.half_length * s[:, None] * self.axis - self.radius * perp

    def surface_samples(self, n_axial: int, n_around: int) -> tuple[np.ndarray, np.ndarray, float]:
        """Points on the lateral surface with their outward normals and the area of one sample."""
        t = (np.arange(n_axial) + 0.5) / n_axial * 2.0 - 1.0
        phi = (np.arange(n_around) + 0.5) / n_around * 2.0 * math.pi
        e1 = np.cross(self.axis, [0.0, 0.0, 1.0])
        if np.linalg.norm(e1) < 1e-9:
            e1 = np.cross(self.axis, [0.0, 1.0, 0.0])
        e1 = e1 / np.linalg.norm(e1)
        e2 = np.cross(self.axis, e1)
        normals = np.cos(phi)[:, None] * e1 + np.sin(phi)[:, None] * e2            # (n_around, 3)
        points = (t[:, None, None] * self.half_length * self.axis
                  + self.radius * normals[None, :, :]).reshape(-1, 3)
        area = 2.0 * math.pi * self.radius * 2.0 * self.half_length / (n_axial * n_around)
        return points, np.tile(normals, (n_axial, 1)), area

    def outline_samples(self, n_around: int = 24) -> np.ndarray:
        """Points on the two end circles: their convex hull is the cylinder."""
        pts, _, _ = self.surface_samples(1, n_around)
        ring = pts - (pts @ self.axis)[:, None] * self.axis
        return np.concatenate([ring - self.half_length * self.axis, ring + self.half_length * self.axis])


# ------------------------------------------------------------------ screen and cut-off
def screen(d: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(h, v) of directions (n, 3) with d_x > 0."""
    return -d[..., 1] / d[..., 0], d[..., 2] / d[..., 0]


def direction(h, v) -> np.ndarray:
    """Unit directions (n, 3) of screen points."""
    h, v = np.broadcast_arrays(np.asarray(h, dtype=float), np.asarray(v, dtype=float))
    d = np.stack([np.ones_like(h), -h, v], axis=-1)
    return d / np.linalg.norm(d, axis=-1, keepdims=True)


HORIZONTAL, RISING = "horizontal", "rising"


@dataclass(frozen=True)
class Cutoff:
    """The cut-off line of an asymmetric low beam: horizontal at V (below the horizon) on the side
    of the oncoming traffic, rising by `rise` from the kink at H = kink on the driver's own side."""

    v_deg: float = -0.57          # 1 % below the horizon
    rise_deg: float = 15.0
    kink_deg: float = 0.0
    right_hand_traffic: bool = True

    @property
    def slope(self) -> float:
        """dv/dh of the rising branch (negative for left-hand traffic: it rises to the left)."""
        k = math.tan(math.radians(self.rise_deg))
        return k if self.right_hand_traffic else -k

    def plane(self, branch: str, below_deg: float = 0.0) -> np.ndarray:
        """Unit normal m of the branch's plane, lowered by `below`; m · d > 0 is the dark side."""
        v0 = math.tan(math.radians(self.v_deg - below_deg))
        hk = math.tan(math.radians(self.kink_deg))
        k = self.slope if branch == RISING else 0.0
        m = np.array([-(v0 - k * hk), k, 1.0])   # m · (1, -h, v) = v - v0 - k (h - hk)
        return m / np.linalg.norm(m)

    def line(self, h: np.ndarray) -> np.ndarray:
        """v of the cut-off at screen positions h: the higher of the two branches."""
        v0 = math.tan(math.radians(self.v_deg))
        hk = math.tan(math.radians(self.kink_deg))
        return np.maximum(v0, v0 + self.slope * (np.asarray(h, dtype=float) - hk))

    def above(self, d: np.ndarray) -> np.ndarray:
        """Sine of the angle by which directions (n, 3) lie above the cut-off (dark side > 0)."""
        return np.minimum(d @ self.plane(HORIZONTAL), d @ self.plane(RISING))
