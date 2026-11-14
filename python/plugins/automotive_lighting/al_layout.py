"""The front view of the reflector: its frame, the zones (sectors about the lamp axis) and their
strips, as polygons in (y, z) about the lamp axis – y to the left, z up, seen in the driving
direction. Strip borders are straight lines or circles (al_design.StripFrame), so every cell is cut
out of the frame with half-planes and circles. NumPy only."""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from al_design import DesignError, FacetSpec, StripFrame
from al_optics import Cutoff

ARC_TOLERANCE = 0.002   # mm, chord error of the polylines that stand for arcs
MIN_AREA = 0.01         # mm², smaller pieces are dropped


# ------------------------------------------------------------------ polygons
def area(poly: np.ndarray) -> float:
    """Signed area (positive: counter-clockwise)."""
    x, y = poly[:, 0], poly[:, 1]
    return 0.5 * float(np.sum(x * np.roll(y, -1) - np.roll(x, -1) * y))


def centroid(poly: np.ndarray) -> np.ndarray:
    x, y = poly[:, 0], poly[:, 1]
    cross = x * np.roll(y, -1) - np.roll(x, -1) * y
    a = 0.5 * np.sum(cross)
    return np.array([np.sum((x + np.roll(x, -1)) * cross), np.sum((y + np.roll(y, -1)) * cross)]) / (6.0 * a)


def contains(poly: np.ndarray, points: np.ndarray) -> np.ndarray:
    """Even-odd test of points (n, 2)."""
    x, y = points[:, 0], points[:, 1]
    inside = np.zeros(len(points), dtype=bool)
    a, b = poly, np.roll(poly, -1, axis=0)
    for (ax, ay), (bx, by) in zip(a, b):
        if ay == by:
            continue
        hit = ((ay > y) != (by > y)) & (x < (bx - ax) * (y - ay) / (by - ay) + ax)
        inside ^= hit
    return inside


def simplified(poly: np.ndarray, tolerance: float) -> np.ndarray:
    """The polygon with the vertices left out that lie within `tolerance` of the line between
    their neighbours that are kept (a quick outline for inside tests)."""
    keep = [0]
    for i in range(1, len(poly) - 1):
        a, b = poly[keep[-1]], poly[i + 1]
        d = b - a
        off = abs(d[0] * (poly[i, 1] - a[1]) - d[1] * (poly[i, 0] - a[0])) / max(float(np.hypot(*d)), 1e-300)
        if off > tolerance:
            keep.append(i)
    keep.append(len(poly) - 1)
    return poly[keep]


def _tidy(points: list) -> np.ndarray | None:
    """A counter-clockwise polygon without repeated points, or None if it has no area."""
    out: list[np.ndarray] = []
    for p in points:
        if not out or np.hypot(*(p - out[-1])) > 1e-9:
            out.append(np.asarray(p, dtype=float))
    while len(out) > 1 and np.hypot(*(out[0] - out[-1])) <= 1e-9:
        out.pop()
    if len(out) < 3:
        return None
    poly = np.array(out)
    a = area(poly)
    if abs(a) < MIN_AREA:
        return None
    return poly if a > 0 else poly[::-1]


@dataclass(frozen=True)
class Line:
    """Keeps normal · x >= offset."""

    normal: np.ndarray
    offset: float

    def value(self, pts: np.ndarray) -> np.ndarray:
        return pts @ self.normal - self.offset

    def crossings(self, a: np.ndarray, b: np.ndarray, ga: float, gb: float) -> list[float]:
        return [ga / (ga - gb)] if (ga >= 0.0) != (gb >= 0.0) else []

    def position(self, pt: np.ndarray) -> float:
        return float(pt[0] * -self.normal[1] + pt[1] * self.normal[0])

    closed = False

    def path(self, a: np.ndarray, b: np.ndarray, forward: bool) -> list[np.ndarray]:
        return []


@dataclass(frozen=True)
class Circle:
    """Keeps the inside of the circle (inside = True) or the outside."""

    centre: np.ndarray
    radius: float
    inside: bool

    def value(self, pts: np.ndarray) -> np.ndarray:
        g = self.radius - np.hypot(pts[..., 0] - self.centre[0], pts[..., 1] - self.centre[1])
        return g if self.inside else -g

    def crossings(self, a: np.ndarray, b: np.ndarray, ga: float, gb: float) -> list[float]:
        d, f = b - a, a - self.centre
        A, B, C = d @ d, 2.0 * (f @ d), f @ f - self.radius ** 2
        disc = B * B - 4.0 * A * C
        roots: list[float] = []
        if disc > 0.0 and A > 0.0:
            s = math.sqrt(disc)
            roots = [t for t in ((-B - s) / (2 * A), (-B + s) / (2 * A)) if 0.0 < t < 1.0]
        differ = (ga >= 0.0) != (gb >= 0.0)
        if differ and len(roots) != 1:      # rounding at an end point: one crossing it must be
            roots = [min(max(ga / (ga - gb), 0.0), 1.0)] if len(roots) != 1 else roots
        if not differ and len(roots) == 1:
            roots = []
        return roots

    def position(self, pt: np.ndarray) -> float:
        return math.atan2(pt[1] - self.centre[1], pt[0] - self.centre[0])

    closed = True

    def path(self, a: np.ndarray, b: np.ndarray, forward: bool) -> list[np.ndarray]:
        """Points on the arc from a to b, counter-clockwise if `forward` (without a and b)."""
        ta, tb = self.position(a), self.position(b)
        sweep = (tb - ta) % (2.0 * math.pi) if forward else -((ta - tb) % (2.0 * math.pi))
        step = math.sqrt(8.0 * ARC_TOLERANCE / self.radius) if self.radius > 8.0 * ARC_TOLERANCE else 1.0
        n = max(1, math.ceil(abs(sweep) / step))
        return [self.centre + self.radius * np.array([math.cos(ta + sweep * i / n), math.sin(ta + sweep * i / n)])
                for i in range(1, n)]


@dataclass(frozen=True)
class Wedge:
    """Keeps the sector from the ray az0 to the ray az1 (radians, counter-clockwise, any width but
    180° and 360°), moved inwards by `inset`: one clip curve of two rays from an apex."""

    az0: float
    az1: float
    inset: float = 0.0

    closed = False

    @property
    def _lines(self):
        n0 = np.array([-math.sin(self.az0), math.cos(self.az0)])     # left of the ray az0
        n1 = np.array([math.sin(self.az1), -math.cos(self.az1)])     # right of the ray az1
        return n0, n1

    @property
    def narrow(self) -> bool:
        return (self.az1 - self.az0) % (2.0 * math.pi) < math.pi

    @property
    def apex(self) -> np.ndarray:
        n0, n1 = self._lines
        return np.linalg.solve(np.array([n0, n1]), np.array([self.inset, self.inset]))

    def value(self, pts: np.ndarray) -> np.ndarray:
        n0, n1 = self._lines
        g0, g1 = pts @ n0 - self.inset, pts @ n1 - self.inset
        return np.minimum(g0, g1) if self.narrow else np.maximum(g0, g1)

    def crossings(self, a: np.ndarray, b: np.ndarray, ga: float, gb: float) -> list[float]:
        apex, roots = self.apex, []
        for normal, az in zip(self._lines, (self.az0, self.az1)):
            fa, fb = (a - apex) @ normal, (b - apex) @ normal
            if (fa >= 0.0) != (fb >= 0.0):
                t = fa / (fa - fb)
                hit = a + t * (b - a) - apex
                if hit @ np.array([math.cos(az), math.sin(az)]) >= 0.0:      # on the ray, not behind the apex
                    roots.append(t)
        differ = (ga >= 0.0) != (gb >= 0.0)
        if differ and len(roots) != 1:
            roots = [min(max(ga / (ga - gb), 0.0), 1.0)]
        if not differ and len(roots) == 1:
            roots = []
        return roots

    def position(self, pt: np.ndarray) -> float:
        n0, n1 = self._lines
        d = pt - self.apex
        return -float(np.hypot(*d)) if abs(d @ n0) <= abs(d @ n1) else float(np.hypot(*d))

    def path(self, a: np.ndarray, b: np.ndarray, forward: bool) -> list[np.ndarray]:
        return [self.apex] if self.position(a) * self.position(b) < 0.0 else []


def clip(poly: np.ndarray, curve) -> list[np.ndarray]:
    """The parts of a simple polygon on the kept side of a line or circle (Weiler–Atherton for one
    clip curve): the polygon's boundary inside, joined along the curve."""
    g = curve.value(poly)
    g = np.where(np.abs(g) < 1e-9, 1e-9, g)      # points on the curve count as kept
    nodes: list[tuple[np.ndarray, int]] = []     # (point, kind): 0 vertex, +1 entering, -1 leaving
    keep: list[bool] = []
    n = len(poly)
    for i in range(n):
        a, b, ga, gb = poly[i], poly[(i + 1) % n], float(g[i]), float(g[(i + 1) % n])
        nodes.append((a, 0))
        keep.append(ga >= 0.0)
        state = ga >= 0.0
        for t in sorted(curve.crossings(a, b, ga, gb)):
            state = not state
            nodes.append((a + t * (b - a), 1 if state else -1))
            keep.append(True)
    cross = [k for k, (_, kind) in enumerate(nodes) if kind != 0]
    if not cross:
        surrounds = curve.closed and contains(poly, curve.centre[None, :])[0]
        if np.all(g >= 0.0):
            if surrounds and not curve.inside:
                raise DesignError("a cell of the reflector would have a round hole (the lamp hole or a strip "
                                  "border lies completely inside it) – use strips narrower than the lamp hole "
                                  "or less sideways aim")
            return [poly]
        if surrounds and curve.inside:               # the whole circle lies inside the polygon
            start = curve.centre + np.array([curve.radius, 0.0])
            turn = curve.centre - np.array([curve.radius, 0.0])
            ring = [start, *curve.path(start, turn, True), turn, *curve.path(turn, start, True)]
            return [np.array(ring)]
        return []
    # the curve runs inside the polygon between crossings that follow each other along it
    order = sorted(cross, key=lambda k: curve.position(nodes[k][0]))
    first = 0
    if curve.closed:
        a0, a1 = curve.position(nodes[order[0]][0]), curve.position(nodes[order[1]][0])
        mid = 0.5 * (a0 + a1)
        probe = curve.centre + curve.radius * np.array([math.cos(mid), math.sin(mid)])
        first = 0 if contains(poly, probe[None, :])[0] else 1
    partner: dict[int, tuple[int, bool]] = {}    # crossing -> (crossing, forward along the curve)
    m = len(order)
    for s in range(first, first + m - (0 if curve.closed else 1), 2):
        u, w = order[s % m], order[(s + 1) % m]
        partner[u], partner[w] = (w, True), (u, False)
    out: list[np.ndarray] = []
    visited: set[int] = set()
    total = len(nodes)
    for start in cross:
        if nodes[start][1] != 1 or start in visited:
            continue
        pts: list[np.ndarray] = []
        k = start
        for _ in range(4 * total):
            visited.add(k)
            pts.append(nodes[k][0])
            k = (k + 1) % total
            while nodes[k][1] == 0:              # along the polygon, inside
                pts.append(nodes[k][0])
                k = (k + 1) % total
            pts.append(nodes[k][0])              # leaving: along the curve to the next entry
            if k not in partner:
                break
            nxt, forward = partner[k]
            pts.extend(curve.path(nodes[k][0], nodes[nxt][0], forward))
            k = nxt
            if k == start or k in visited:
                break
        piece = _tidy(pts)
        if piece is not None:
            out.append(piece)
    return out


def clip_all(polys: list[np.ndarray], curve) -> list[np.ndarray]:
    return [piece for poly in polys for piece in clip(poly, curve)]


# ------------------------------------------------------------------ the frame
def rounded_rectangle(width: float, height: float, radius: float, centre=(0.0, 0.0)) -> np.ndarray:
    """Counter-clockwise outline, corners as arcs within ARC_TOLERANCE."""
    r = min(max(radius, 0.0), 0.5 * width, 0.5 * height)
    hx, hy = 0.5 * width, 0.5 * height
    pts: list[np.ndarray] = []
    corners = [(hx - r, hy - r, 0.0), (-hx + r, hy - r, 0.5 * math.pi),
               (-hx + r, -hy + r, math.pi), (hx - r, -hy + r, 1.5 * math.pi)]
    for cx, cy, start in corners:
        if r <= 0.0:
            pts.append(np.array([cx, cy]))
            continue
        n = max(2, math.ceil(0.5 * math.pi / math.sqrt(8.0 * ARC_TOLERANCE / r)))
        for i in range(n + 1):
            t = start + 0.5 * math.pi * i / n
            pts.append(np.array([cx + r * math.cos(t), cy + r * math.sin(t)]))
    poly = _tidy(pts)
    if poly is None:
        raise DesignError("the frame has no area")
    return poly + np.asarray(centre, dtype=float)


def chain(curves: list[np.ndarray]) -> np.ndarray:
    """One closed counter-clockwise outline from sampled curves (n, 2) that join end to end."""
    if not curves:
        raise DesignError("no frame curve")
    rest = [np.asarray(c, dtype=float) for c in curves]
    size = max(float(np.ptp(np.concatenate(rest), axis=0).max()), 1e-9)
    loop = list(rest.pop(0))
    while rest:
        end = loop[-1]
        best = min(((min(np.hypot(*(c[0] - end)), np.hypot(*(c[-1] - end))), i) for i, c in enumerate(rest)))
        if best[0] > 1e-3 * size:
            raise DesignError("the frame curves do not join to one outline (gap of "
                              f"{best[0]:.3g} mm) – select curves that meet end to end")
        c = rest.pop(best[1])
        loop.extend(c if np.hypot(*(c[0] - end)) <= np.hypot(*(c[-1] - end)) else c[::-1])
    if np.hypot(*(loop[0] - loop[-1])) > 1e-3 * size:
        raise DesignError("the frame is not closed (its ends are "
                          f"{np.hypot(*(loop[0] - loop[-1])):.3g} mm apart)")
    poly = _tidy(loop)
    if poly is None:
        raise DesignError("the frame has no area")
    return poly


# ------------------------------------------------------------------ zones and strips
@dataclass(frozen=True)
class Zone:
    """A sector about the lamp axis, azimuth atan2(z, y) from … to (degrees, counter-clockwise seen
    in the driving direction: 0 = left, 90 = up, 180 = right), with the cut-off branch of its
    facets, the aims (H in degrees, positive to the right) of each strip's right and left border
    and how far below the cut-off its images are placed."""

    az_from: float
    az_to: float
    branch: str
    aim_right: float
    aim_left: float
    below: float = 0.0


@dataclass
class Cell:
    zone: int
    strip: int
    spec: FacetSpec
    pieces: list[np.ndarray]       # polygons (y, z), counter-clockwise

    @property
    def area(self) -> float:
        return sum(area(p) for p in self.pieces)


def sector_curve(az_from: float, az_to: float, inset: float = 0.0):
    """The clip curve of the sector az_from … az_to (degrees), moved inwards by `inset`: a wedge,
    a line for a half plane, None for the full turn."""
    span = (az_to - az_from) % 360.0 or 360.0
    if span >= 360.0 - 1e-9:
        return None
    a0, a1 = math.radians(az_from), math.radians(az_from + span)
    if abs(span - 180.0) < 1e-9:
        return Line(np.array([-math.sin(a0), math.cos(a0)]), inset)
    return Wedge(a0, a1, inset)


def zone_region(frame_poly: np.ndarray, zone: "Zone", inset: float = 0.0) -> list[np.ndarray]:
    """The frame within the zone's sector (moved inwards by `inset`); the lamp hole is cut out of
    the strips afterwards (a whole zone around it would be a polygon with a hole)."""
    curve = sector_curve(zone.az_from, zone.az_to, inset)
    return [frame_poly] if curve is None else clip(frame_poly, curve)


def outside_hole(polys: list[np.ndarray], hole_radius: float) -> list[np.ndarray]:
    return clip_all(polys, Circle(np.zeros(2), hole_radius, inside=False)) if hole_radius > 0.0 else polys


def strip_curve(frame: StripFrame, c: float, keep_above: bool, inset: float = 0.0):
    """The border c = const of a zone's strips as a clip curve keeping c >= const (or <=), moved
    by `inset` into the kept side."""
    if abs(frame.kappa) < 1e-7:
        normal = frame.e_p if keep_above else -frame.e_p
        return Line(normal, (c if keep_above else -c) + inset)
    scale = 1.0 + frame.kappa * c / frame.focal
    if scale <= 0.0:
        return None if (frame.kappa > 0.0) == keep_above else "nothing"
    centre = np.array(frame.yz(-2.0 * frame.focal / frame.kappa, 0.0))
    radius = 2.0 * frame.focal / abs(frame.kappa) * math.sqrt(scale)
    inside = (frame.kappa < 0.0) == keep_above
    radius += -inset if inside else inset
    if radius <= 0.0:
        return "nothing" if inside else None
    return Circle(centre, radius, inside=inside)


def _deepest_q(polys_pq: list[np.ndarray]) -> float:
    """The section of the cell nearest to the lamp axis' level (q = 0) – there the base paraboloid is
    deepest along the strip. The start curve lies in this section: from it the strips of a facet
    whose aims diverge run forward (towards the rim) only and fan out; started further out they
    would cross on the way in."""
    q = np.concatenate([poly[:, 1] for poly in polys_pq])
    return float(min(max(0.0, q.min()), q.max()))


def layout(frame_poly: np.ndarray, hole_radius: float, zones: list[Zone], cutoff: Cutoff, focal: float,
           strip_width: float, margin: float = 1.5) -> list[Cell]:
    """The cells of the reflector: every zone cut into strips of `strip_width` (measured where the
    strips pass the lamp axis' level), within the frame and outside the lamp hole. Each cell's
    facet covers the cell's bounding rectangle in the zone's frame plus `margin`."""
    if strip_width <= 0.0:
        raise DesignError("the strip width must be positive")
    if area(frame_poly) < 0:
        frame_poly = frame_poly[::-1]
    cells: list[Cell] = []
    for zi, zone in enumerate(zones):
        m = cutoff.plane(zone.branch, zone.below)
        t_right, t_left = math.tan(math.radians(zone.aim_right)), math.tan(math.radians(zone.aim_left))
        frame = StripFrame.of(m, 0.5 * (t_right + t_left), focal)
        pieces = zone_region(frame_poly, zone)
        if not pieces:
            continue
        c_all = np.concatenate([frame.strip_coordinate(*frame.pq(p[:, 0], p[:, 1])) for p in pieces])
        k_lo = math.floor(c_all.min() / strip_width + 0.5)
        k_hi = math.floor(c_all.max() / strip_width + 0.5)
        for k in range(k_lo, k_hi + 1):
            c0, c1 = (k - 0.5) * strip_width, (k + 0.5) * strip_width
            strip = pieces
            for curve in (strip_curve(frame, c0, True), strip_curve(frame, c1, False)):
                if curve is None:
                    continue
                strip = [] if isinstance(curve, str) else clip_all(strip, curve)
            strip = outside_hole(strip, hole_radius)
            if not strip:
                continue
            pq = [np.stack(frame.pq(p[:, 0], p[:, 1]), axis=1) for p in strip]
            both = np.concatenate(pq)
            rect = (both[:, 0].min() - margin, both[:, 0].max() + margin,
                    both[:, 1].min() - margin, both[:, 1].max() + margin)
            q_start = _deepest_q(pq)
            spec = FacetSpec(m, frame, (c0, c1), (t_right, t_left), rect, q_start)
            cells.append(Cell(zi, k, spec, strip))
    if not cells:
        raise DesignError("no reflector area: the zones do not cover the frame outside the lamp hole")
    return cells
