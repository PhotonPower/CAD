"""The facetted reflector: settings -> cells (al_layout) -> facets (al_design) -> connecting faces
(al_transitions), its parts in the front view with points on them for the photometry, and its
faces in the .nfcad geometry format. Design coordinates (origin = filament centre, +X the driving
direction, +Y left, +Z up); `origin` places it in the model. NumPy only."""

from __future__ import annotations

import math
from dataclasses import dataclass, field, replace
from typing import Callable

import numpy as np

import al_layout
import al_transitions
from al_design import DesignError, Facet, build_facet, cutoff_residual
from al_layout import Cell, Zone
from al_optics import Cutoff, Filament, Lamp

FACET, STRIP_BAND, ZONE_BAND = "facet", "strip band", "zone band"


@dataclass(frozen=True)
class Settings:
    lamp: Lamp
    cutoff: Cutoff
    zones: tuple[Zone, ...]
    focal: float = 25.0            # mm, base paraboloid (focus at the filament centre)
    strip_width: float = 10.0      # mm
    hole_radius: float = 12.0      # mm, lamp hole about the axis
    connect: bool = True           # connecting faces between the facets
    draft: float = 5.0             # degrees between their walls and the demoulding direction X
    fillet: float = 0.3            # mm, radius at the foot and the head of the walls
    step: float = 0.5              # mm, spacing of the characteristic strips
    knot_spacing: float = 1.5      # mm, of the facets' B-splines
    margin: float = 3.5            # mm, the facets reach this far beyond their cells


@dataclass
class Part:
    """A face of the mirror: its polygons in the front view and its surface."""

    kind: str                      # FACET, STRIP_BAND or ZONE_BAND
    label: str
    polygons: list[np.ndarray]
    height: Callable               # (y, z) -> x, ∇x (n, 2)
    surface: Callable              # origin -> .nfcad surface
    parameters: Callable           # (y, z) -> the surface's (u, v)
    cell: int = -1                 # index of the facet's cell

    @property
    def area(self) -> float:
        return sum(al_layout.area(p) for p in self.polygons)


@dataclass
class Reflector:
    settings: Settings
    frame: np.ndarray              # (n, 2) outline in the front view (y, z), counter-clockwise
    cells: list[Cell]
    facets: list[Facet]
    filament: Filament
    transitions: al_transitions.Transitions | None = None
    warnings: list[str] = field(default_factory=list)
    cutoff_error: float = 0.0      # degrees: the largest distance of a filament image's top from its cut-off

    @property
    def area(self) -> float:
        return sum(c.area for c in self.cells)

    def parts(self) -> list[Part]:
        """The faces of the mirror: the facets (between the connecting faces, if any) and the bands."""
        out = []
        for index, (cell, facet) in enumerate(zip(self.cells, self.facets)):
            polygons = self.transitions.cell_polygons[index] if self.transitions else cell.pieces
            if polygons:
                out.append(Part(FACET, f"Facet {cell.zone + 1}.{cell.strip}", polygons, _facet_height(facet),
                                lambda origin, f=facet: facet_surface(f, origin), facet.spec.frame.pq, index))
        if self.transitions:
            for band in self.transitions.strip_bands:
                if band.polygons:
                    out.append(Part(STRIP_BAND, f"Connection {band.zones[0] + 1}.{band.strip - 1}|{band.strip}",
                                    band.polygons, band.evaluate, band.surface, band.border.uxi))
            for band in self.transitions.zone_bands:
                if band.polygons:
                    out.append(Part(ZONE_BAND, f"Connection zone {band.zones[0] + 1}|{band.zones[1] + 1}",
                                    band.polygons, band.evaluate, band.surface, band.border.uxi))
        return out

    def samples(self, spacing: float, kinds: tuple[str, ...] | None = None):
        """Points on the mirror on a front-view lattice of `spacing`: positions (n, 3), unit normals
        (n, 3), mirror area per point (n,) and the index of each point's part in parts() (n,)."""
        P, N, A, part_index = [], [], [], []
        for index, part in enumerate(self.parts()):
            if kinds is not None and part.kind not in kinds:
                continue
            for piece in part.polygons:
                lo, hi = piece.min(axis=0), piece.max(axis=0)
                ys = (np.arange(math.floor(lo[0] / spacing), math.ceil(hi[0] / spacing)) + 0.5) * spacing
                zs = (np.arange(math.floor(lo[1] / spacing), math.ceil(hi[1] / spacing)) + 0.5) * spacing
                if not len(ys) or not len(zs):
                    continue
                Y, Z = np.meshgrid(ys, zs, indexing="ij")
                pts = np.stack([Y.ravel(), Z.ravel()], axis=1)
                pts = pts[al_layout.contains(piece, pts)]
                if not len(pts):
                    continue
                x, grad = part.height(pts[:, 0], pts[:, 1])
                n = np.column_stack([np.ones(len(pts)), -grad])
                n /= np.linalg.norm(n, axis=1, keepdims=True)
                P.append(np.column_stack([x, pts]))
                N.append(n)
                A.append(spacing * spacing / n[:, 0])
                part_index.append(np.full(len(pts), index))
        if not P:
            return np.zeros((0, 3)), np.zeros((0, 3)), np.zeros(0), np.zeros(0, dtype=int)
        return np.concatenate(P), np.concatenate(N), np.concatenate(A), np.concatenate(part_index)

    def depth(self) -> tuple[float, float]:
        """Smallest and largest x of the mirror."""
        x = self.samples(2.0)[0][:, 0]
        return (float(x.min()), float(x.max())) if len(x) else (0.0, 0.0)


def _facet_height(facet: Facet) -> Callable:
    def height(y, z):
        fr = facet.spec.frame
        f, fp, fq = facet.graph.evaluate(*fr.pq(np.asarray(y, dtype=float), np.asarray(z, dtype=float)))
        e_p, e_q = fr.e_p, fr.e_q
        return f, np.stack([fp * e_p[0] + fq * e_q[0], fp * e_p[1] + fq * e_q[1]], axis=-1)

    return height


def build(settings: Settings, frame: np.ndarray, progress=None) -> Reflector:
    """The reflector within `frame` (front view, about the lamp axis). progress(fraction, text) is
    called along the way and may raise to cancel."""
    fil = Filament.of(settings.lamp)
    share = 0.7 if settings.connect else 1.0
    for _ in range(3):
        cells = al_layout.layout(frame, settings.hole_radius, list(settings.zones), settings.cutoff,
                                 settings.focal, settings.strip_width, settings.margin)
        facets, warnings = [], []
        for i, cell in enumerate(cells):
            if progress is not None:
                progress(share * i / len(cells), f"facet {i + 1} of {len(cells)}")
            try:
                facet = build_facet(cell.spec, fil, step=settings.step, knot_spacing=settings.knot_spacing)
            except ValueError as e:
                raise type(e)(f"zone {cell.zone + 1}, strip {cell.strip}: {e}") from e
            if facet.extrapolated:
                warnings.append(f"zone {cell.zone + 1}, strip {cell.strip}: the strips do not reach every corner "
                                "of the facet (continued by their slope there)")
            facets.append(facet)
        reflector = Reflector(settings, frame, cells, facets, fil, warnings=warnings)
        if not settings.connect:
            break
        try:
            reflector.transitions = al_transitions.build(
                reflector, settings.draft, settings.fillet,
                None if progress is None else lambda f, text: progress(share + (1.0 - share) * f, text))
            reflector.warnings += reflector.transitions.warnings
            break
        except al_transitions.NeedsMargin as need:      # higher steps than the facets' margin allows for
            settings = replace(settings, margin=need.margin)
    else:
        raise DesignError("the connecting faces need more room than the facets can give – reduce the draft "
                          "angle or the fillet radius")
    parts = reflector.parts()
    P, N, _, index = reflector.samples(1.0, kinds=(FACET,))
    for i, part in enumerate(parts):
        sel = index == i
        if sel.any():
            m = cells[part.cell].spec.m
            reflector.cutoff_error = max(reflector.cutoff_error,
                                         float(np.degrees(np.abs(cutoff_residual(P[sel], N[sel], m, fil)).max())))
    return reflector


# ------------------------------------------------------------------ geometry for CAD
def _polyline(points: np.ndarray) -> dict:
    """A closed polyline in the parameter plane (z = 0) as a degree-1 curve."""
    pts = np.vstack([points, points[:1]])
    n = len(pts)
    knots = [0.0] + [i / (n - 1) for i in range(n)] + [1.0]
    return {"degree": 1, "knots": knots,
            "points": [round(float(c), 8) for p in pts for c in (p[0], p[1], 0.0)]}


def facet_surface(facet: Facet, origin=(0.0, 0.0, 0.0)) -> dict:
    """The facet's B-spline as a .nfcad surface; its parameters (u, v) are the zone's (p, q) in mm."""
    g, fr = facet.graph, facet.spec.frame
    Pg, Qg = np.meshgrid(g.sites_p, g.sites_q, indexing="ij")
    y, z = fr.yz(Pg, Qg)
    pts = np.stack([g.coeff + origin[0], y + origin[1], z + origin[2]], axis=-1)   # (n_p, n_q, 3): i along u
    return {"degree_u": 3, "degree_v": 3, "knots_u": [float(k) for k in g.knots_p],
            "knots_v": [float(k) for k in g.knots_q], "num_u": g.n_p, "num_v": g.n_q,
            "points": [round(float(c), 8) for c in pts.reshape(-1)]}


def faces(reflector: Reflector, origin=(0.0, 0.0, 0.0)) -> list[tuple[Part, dict]]:
    """(part, trimmed surface) for every polygon of every part: the part's surface, trimmed with
    the polygon's outline in the surface's parameters."""
    out = []
    for part in reflector.parts():
        surface = part.surface(origin)
        for polygon in part.polygons:
            loop = np.stack(part.parameters(polygon[:, 0], polygon[:, 1]), axis=1)
            if al_layout.area(loop) < 0.0:
                loop = loop[::-1]
            out.append((part, {"type": "trimmed_surface", "surface": surface, "outer": [_polyline(loop)],
                               "label": part.label}))
    return out


def filament_surface(fil: Filament, origin=(0.0, 0.0, 0.0)) -> dict:
    """The coil's cylinder as an exact NURBS surface (u around, v along the axis)."""
    w = math.sqrt(0.5)
    unit = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0)]
    e1 = np.cross(fil.axis, [0.0, 0.0, 1.0])
    e1 = e1 / np.linalg.norm(e1) if np.linalg.norm(e1) > 1e-9 else np.array([0.0, 1.0, 0.0])
    e2 = np.cross(fil.axis, e1)
    pts = []
    for a, b in unit:
        ring = fil.radius * (a * e1 + b * e2) + np.asarray(origin, dtype=float)
        pts += [ring - fil.half_length * fil.axis, ring + fil.half_length * fil.axis]
    weights = [x for wt in (1, w, 1, w, 1, w, 1, w, 1) for x in (wt, wt)]
    return {"type": "surface", "degree_u": 2, "degree_v": 1,
            "knots_u": [0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1], "knots_v": [0.0, 0.0, 1.0, 1.0],
            "num_u": 9, "num_v": 2, "points": [float(c) for p in pts for c in p], "weights": weights}
