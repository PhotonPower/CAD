"""Raytracer plugin: Raytatouille systems in Raytatouille Fusion (docs/plugins.md, P6).

* **Load system** – reads a ``.rtt.json``, compiles it with the glass catalogues and draws every
  surface as an exact-placed NURBS face (bridge ``rtt_nurbs``), lenses with their rims, in a
  model tree that follows the rtt node tree (assemblies, elements).
* **Trace rays** – rays along a path as a line set (coloured by wavelength or field) and the
  irradiance on the image surface as a field on its face.
* **Optimise** – Raytatouille's optimiser; the changed surfaces are redrawn after every accepted
  iteration and the merit function grows in the plot dock.
* **Save system** – writes the current (e.g. optimised) system.

The optical system is the source of truth. The CAD geometry is derived from it; every state of
the system the plugin creates gets a revision number, stored as the attribute ``rtt.revision``
of the system's anchor entity – undo / redo in CAD restore that attribute, and the plugin then
continues with the system of that revision.
"""

from __future__ import annotations

import glob
import importlib
import math
import os
import re
import warnings
from pathlib import Path

import numpy as np

from nfcad import Param, Plugin, UserError, action

import rtt_nurbs

try:
    import raytatouille as rt
except ImportError:  # reported when an action runs
    rt = None

TOP_GROUP_PREFIX = "Raytatouille: "


def rings_for(rays: int) -> int:
    """Hexapolar rings r whose 1 + 3 r (r + 1) rays come closest to `rays` (at least 1 ring)."""
    r = (-3 + np.sqrt(max(9 + 12 * (rays - 1), 0))) / 6
    lo, hi = max(1, int(np.floor(r))), max(1, int(np.ceil(r)))
    count = lambda k: 1 + 3 * k * (k + 1)
    return lo if abs(count(lo) - rays) <= abs(count(hi) - rays) else hi


def _require_rt():
    if rt is None:
        raise UserError("The Python package 'raytatouille' is not installed in the plugin's Python "
                        "environment. Plugins → Settings… → Check environment shows how to install it.")


class RaytracerPlugin(Plugin):
    params = {
        "system": Param.file("", filter="Raytatouille system (*.rtt.json *.json)", label="System file",
                             group="System"),
        "catalogs": Param.files([], filter="Glass catalogues (*.agf *.AGF);;All files (*)", label="Glass catalogues",
                                group="System",
                                description="AGF catalogues (Add…). Always used as well: *.agf next to the system "
                                            "file and in its sub-folder 'catalogs'."),
        "tolerance": Param.number(1e-4, min=1e-7, max=1.0, decimals=7, unit="mm", label="Drawing tolerance",
                                  group="System", description="Largest deviation of the drawn NURBS from the "
                                                              "optical surface (only the drawing; rays are traced "
                                                              "on the exact surfaces)"),
        "path": Param.text("", label="Paths", group="Trace",
                           description="Names of the paths, separated by commas; empty: every path"),
        "fields": Param.integer(0, min=0, label="Field", group="Trace", description="0: all fields"),
        "wavelengths": Param.choice(["all", "reference"], "all", label="Wavelengths", group="Trace"),
        "rays": Param.integer(127, min=1, max=20_000, label="Rays (drawn)", group="Trace",
                              description="Rays drawn per field and wavelength (hexapolar pupil; the next possible "
                                          "number 1, 7, 19, 37, … 127, … is used)"),
        "colour_by": Param.choice(["wavelength", "field", "path"], "wavelength", label="Colour by", group="Trace"),
        "irradiance": Param.boolean(False, label="Quick irradiance", group="Trace",
                                    description="A coarse irradiance with the trace; for a real camera image use "
                                                "Camera image"),
        "irradiance_rays": Param.integer(5_000, min=7, max=500_000, label="Rays (quick irradiance)", group="Trace"),
        "grid": Param.integer(128, min=8, max=1024, label="Quick irradiance grid", group="Trace"),
        "camera_rays": Param.integer(200_000, min=1_000, max=50_000_000, label="Rays per field and wavelength",
                                     group="Camera image",
                                     description="Many rays for a smooth image; time grows linearly"),
        "camera_pixels": Param.integer(512, min=16, max=4096, label="Pixels (long side)", group="Camera image"),
        "camera_on_surface": Param.boolean(True, label="Also on the surface in 3D", group="Camera image"),
        "workflow_rays": Param.integer(20_000, min=7, max=500_000, label="Rays per field and wavelength",
                                       group="Workflow",
                                       description="For Run workflow (coherent image; every ray path is "
                                                   "recorded, so memory grows with rays × events)"),
        "workflow_pixels": Param.integer(512, min=16, max=4096, label="Pixels (long side)", group="Workflow"),
        "workflow_window": Param.number(0.0, min=0.0, label="Image window (half width, mm)", group="Workflow",
                                        description="0: the detector aperture. For a focus computed as a field "
                                                    "(link into the camera set to H) a small window, e.g. 0.05 mm"),
        "workflow_gaussian_waist": Param.number(0.0, min=0.0, max=10.0, label="Gaussian illumination (w / pupil radius)",
                                                group="Workflow",
                                                description="0: uniform (a hard-edged beam, whose Fresnel diffraction a "
                                                            "field link shows); e.g. 0.5 for a laser beam"),
        "save_as": Param.file("", filter="Raytatouille system (*.rtt.json)", save=True, label="Save as",
                              group="Optimise", description="Empty: <system>.optimised.rtt.json"),
    }

    def __init__(self):
        self.revisions: dict[int, object] = {}  # revision -> rt.System
        self.next_revision = 1
        self.path: str | None = None
        self.library = None
        self.loaded_from: str | None = None
        self.detector_half: tuple[float, float] | None = None

    # ------------------------------------------------------------------ helpers
    def _library(self, ctx, system_path: str):
        lib = rt.MaterialLibrary()
        files = list(ctx.params["catalogs"])
        folder = Path(system_path).parent
        files += sorted(glob.glob(str(folder / "*.agf"))) + sorted(glob.glob(str(folder / "catalogs" / "*.agf")))
        loaded = []
        for f in dict.fromkeys(files):  # unique, in order
            try:
                lib.add_catalog(f)
                loaded.append(Path(f).name)
            except Exception as e:  # noqa: BLE001 - a broken catalogue should not stop the others
                ctx.log(f"catalogue {f}: {e}")
        if loaded:
            ctx.log("glass catalogues: " + ", ".join(loaded))
        return lib

    def _entities(self, ctx) -> list[dict]:
        return [e for e in ctx.entities() if "rtt.surface" in e["attributes"] or "rtt.rim" in e["attributes"]]

    def _anchor(self, ctx) -> dict | None:
        for e in self._entities(ctx):
            if e["attributes"].get("rtt.anchor"):
                return e
        return None

    def _current(self, ctx):
        """The system of the revision CAD shows (follows undo / redo); loads it if needed."""
        anchor = self._anchor(ctx)
        if anchor is not None:
            rev = int(anchor["attributes"].get("rtt.revision", 0))
            if rev in self.revisions:
                return self.revisions[rev], rev
        # plugin restarted, or the model came from a saved document: load the file again
        self.load(ctx, quiet=True)
        anchor = self._anchor(ctx)
        rev = int(anchor["attributes"].get("rtt.revision", 0))
        return self.revisions[rev], rev

    def _new_revision(self, system) -> int:
        rev = self.next_revision
        self.next_revision += 1
        self.revisions[rev] = system
        return rev

    def _compile(self, system):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            return rt.compile(system, self.library)

    @staticmethod
    def _detector_index(compiled) -> int:
        layouts = rt.layout.surfaces(compiled)
        for i in range(len(layouts) - 1, -1, -1):
            if layouts[i].kind == "detector":
                return i
        return len(layouts) - 1

    def _detector_half(self, system, compiled) -> tuple[float, float]:
        """Size of an image surface without aperture: the spread of a quick trace, plus margin."""
        idx = self._detector_index(compiled)
        L = rt.layout.surfaces(compiled)[idx]
        try:
            rays = self._make_rays(system, compiled, 4, 0, None, None)
            rt.trace.trace(compiled, rays)
            ok = (np.asarray(rays.status) == 0) & (np.asarray(rays.last_surface) == idx)
            p = np.stack([np.asarray(rays.pos_x), np.asarray(rays.pos_y), np.asarray(rays.pos_z)], -1)[ok]
            local = (p - np.asarray(L.translation)) @ np.asarray(L.rotation)
            h = float(np.abs(local[:, :2]).max()) if len(local) else 0.0
        except Exception:  # noqa: BLE001 - no paraxial data etc.: fall back to the apertures
            h = 0.0
        radii = [a.aperture[1].get("radius", 0) for a in rt.layout.surfaces(compiled) if a.aperture]
        scale = max(radii + [5.0])
        if h <= 0:
            h = scale / 4
        # a sharp focus would give a detector too small to see next to the system
        h = max(h * 1.25, 0.05 * scale)
        return h, h

    def _faces(self, system, compiled, tol: float):
        """(surface index, Face) for every surface, the detector with its derived size."""
        layouts = rt.layout.surfaces(compiled)
        radii = [a.aperture[1].get("radius", 0) for a in layouts if a.aperture]
        fallback = max(radii + [10.0])
        det = self._detector_index(compiled)
        faces = []
        for i, L in enumerate(layouts):
            override = None
            if i == det and L.aperture is None:
                override = self.detector_half or self._detector_half(system, compiled)
                self.detector_half = override
            faces.append((i, rtt_nurbs.surface_face(compiled, i, tol=tol, fallback_half=fallback,
                                                    half_override=override)))
        return faces

    # ------------------------------------------------------------------ rays
    def _make_rays(self, system, compiled, rings: int, path, fields, wavelength, **run):
        """rt.trace.make_rays, or – for folded / tilted paths, which its paraxial aiming does not
        accept – start rays built here: possible exactly when the stop is the first surface the
        path meets (nothing to aim through), see _start_rays."""
        try:
            return rt.trace.make_rays(compiled, rt.trace.HexapolarPupil(rings=rings), path=path, fields=fields,
                                      wavelength=wavelength, **run)
        except rt.ParaxialError as e:
            return self._start_rays(system, compiled, rings, path, fields, wavelength, e)

    @staticmethod
    def _start_rays(system, compiled, rings: int, path, fields, wavelength, why):
        """Start rays (global RayBatch) on a hexapolar grid over the stop opening: parallel along
        each field angle (object at infinity; direction ∝ (tan αx, tan αy, 1) as in Raytatouille)
        or from each object point (finite object). Exact without aiming because the stop is the
        first event of the path."""
        import json as _json
        d = _json.loads(system.to_json())
        layouts = rt.layout.surfaces(compiled)
        stop_id = (d.get("aperture", {}).get("stop") or {}).get("surface")
        stop = next((L for L in layouts if (L.id == stop_id if stop_id else L.kind == "stop")), None)
        paths = d.get("paths", [])
        chosen = (paths[path] if isinstance(path, int) else next((q for q in paths if q.get("name") == path), None)) \
            if paths else None
        events = chosen.get("events") if chosen else None
        first = events[0].get("surface") if isinstance(events, list) and events else layouts[0].id
        if stop is None or stop.aperture is None or first != stop.id:
            raise UserError(f"Raytatouille cannot aim rays into this path ({why}). The plugin can start them "
                            "itself only when the aperture stop is the first surface of the path.")
        kind, params = stop.aperture
        rx, ry = (params["radius"], params["radius"]) if kind == "circular" else \
                 (params.get("semi_axis_x", params.get("half_width_x")), params.get("semi_axis_y", params.get("half_width_y")))
        # hexapolar grid in the stop (local x, y) exactly as rt.trace.HexapolarPupil: rings start
        # on +y and run clockwise
        px, py = [0.0], [0.0]
        for k in range(1, rings + 1):
            for j in range(6 * k):
                a = 2 * np.pi * j / (6 * k)
                px.append(k / rings * np.sin(a)); py.append(k / rings * np.cos(a))
        px, py = np.array(px), np.array(py)
        on_stop = np.stack([px * rx, py * ry, np.zeros_like(px)], -1) @ np.asarray(stop.rotation).T + stop.translation
        field_points = d.get("fields", {}).get("points", [{}])
        field_type = d.get("fields", {}).get("type", "angle_deg")
        at_infinity = d.get("object", {}).get("at_infinity", True)
        indices = range(len(field_points)) if fields is None else fields
        pos, dirs, fid = [], [], []
        for fi in indices:
            f = field_points[fi]
            fx, fy = float(f.get("x", 0.0)), float(f.get("y", 0.0))
            if at_infinity and field_type == "angle_deg":
                direction = np.array([np.tan(np.radians(fx)), np.tan(np.radians(fy)), 1.0])
                direction = np.tile(direction / np.linalg.norm(direction), (len(px), 1))
                start = on_stop - direction * (1.0 + max(rx, ry))
            elif not at_infinity and field_type == "object_height":
                obj = np.array([fx, fy, compiled.object_z])
                direction = on_stop - obj
                direction /= np.linalg.norm(direction, axis=1, keepdims=True)
                start = np.tile(obj, (len(px), 1))
            else:
                raise UserError(f"Raytatouille cannot aim rays into this path ({why}); the plugin builds start "
                                f"rays only for field angles at infinity or object heights, not '{field_type}'.")
            pos.append(start); dirs.append(direction); fid += [fi] * len(px)
        pos, dirs = np.concatenate(pos), np.concatenate(dirs)
        rays = rt.trace.RayBatch(len(pos))
        for name, values in (("pos_x", pos[:, 0]), ("pos_y", pos[:, 1]), ("pos_z", pos[:, 2]),
                             ("dir_x", dirs[:, 0]), ("dir_y", dirs[:, 1]), ("dir_z", dirs[:, 2]),
                             ("pupil_x", np.tile(px, len(indices))), ("pupil_y", np.tile(py, len(indices)))):
            np.asarray(getattr(rays, name))[:] = values
        np.asarray(rays.field)[:] = fid
        np.asarray(rays.wl)[:] = compiled.reference_wavelength if wavelength is None else wavelength
        return rays

    # ------------------------------------------------------------------ actions
    @action("Load system", description="Read the .rtt.json and draw it (replaces a drawing loaded before)")
    def load(self, ctx, quiet: bool = False):
        _require_rt()
        path = ctx.params["system"]
        if not path:
            raise UserError("Choose the optical system first: Plugin panel → System file → … "
                            "(a .rtt.json file, e.g. examples/singlet_optim.rtt.json of this plugin).")
        if not os.path.isfile(path):
            raise UserError(f"The system file does not exist:\n{path}")
        ctx.progress(0.05, "loading the system …")
        self.library = self._library(ctx, path)
        try:
            system = rt.load(path)
            compiled = self._compile(system)
        except rt.RaytatouilleError as e:
            hint = ("\n\nGlass catalogues: put the *.agf files next to the system file or list them under "
                    "Glass catalogues." if "catalog" in str(e) else "")
            raise UserError(f"{Path(path).name} cannot be used:\n{e}{hint}") from e
        self.loaded_from = path
        self.detector_half = None
        rev = self._new_revision(system)

        # remove the previous drawing (entities, then its groups, innermost first)
        for e in self._entities(ctx):
            ctx.remove(e["id"])
        groups = ctx.groups()
        tops = [g["id"] for g in groups if g["parent"] == 0 and g["name"].startswith(TOP_GROUP_PREFIX)]
        doomed, frontier = [], tops
        while frontier:
            doomed += frontier
            frontier = [g["id"] for g in groups if g["parent"] in frontier]
        for gid in reversed(doomed):
            ctx.remove_group(gid)

        # model tree: assemblies and elements of the rtt node tree
        top = ctx.add_group(TOP_GROUP_PREFIX + (system.name or Path(path).stem))
        group_of: dict[str, int] = {"/root": top}

        def group_for(location: str) -> int:
            if location in group_of:
                return group_of[location]
            parent = group_for(location.rsplit("/children/", 1)[0])
            node = system.json_at(location)
            name = node.get("name") or node.get("type", "node")
            group_of[location] = ctx.add_group(name, parent)
            return group_of[location]

        tol = ctx.params["tolerance"]
        locations = compiled.surface_locations
        layouts = rt.layout.surfaces(compiled)
        faces = self._faces(system, compiled, tol)
        coarse = []
        for k, (i, face) in enumerate(faces):
            ctx.check_cancel()
            ctx.progress(0.1 + 0.8 * k / max(len(faces), 1), f"drawing {layouts[i].id} …")
            L = layouts[i]
            element_location = locations[i].rsplit("/surfaces/", 1)[0]
            attributes = {"rtt.surface": L.id, "rtt.location": locations[i], "rtt.kind": L.kind,
                          "rtt.shape": L.shape, "rtt.nurbs_error_mm": float(f"{face.max_error:.3g}")}
            if k == 0:
                attributes.update({"rtt.anchor": True, "rtt.revision": rev, "rtt.file": path})
            ctx.add(face.geometry, L.id, attributes, group=group_for(element_location))
            if face.max_error > tol:
                coarse.append(f"{L.id} ({face.max_error:.1e} mm)")
        for element in rt.layout.elements(compiled):
            location = locations[element.first_surface].rsplit("/surfaces/", 1)[0]
            for n, rim in enumerate(rtt_nurbs.rim_faces(compiled, element)):
                ctx.add(rim, f"{element.name} edge" + (f" {n + 1}" if n else ""),
                        {"rtt.rim": element.name}, group=group_for(location))
        if coarse:
            ctx.log("drawn with more than the tolerance (non-smooth shapes such as the axicon tip): "
                    + ", ".join(coarse))
        if not quiet:
            return f"{system.name or Path(path).name}: {len(layouts)} surfaces"
        return None

    @action("Trace rays", description="Rays along the path as lines; irradiance on the image surface")
    def trace(self, ctx):
        _require_rt()
        system, rev = self._current(ctx)
        compiled = self._compile(system)
        p = ctx.params
        names = list(compiled.path_names)
        wanted = [n.strip() for n in p["path"].split(",") if n.strip()]
        unknown = [n for n in wanted if n not in names]
        if unknown:
            raise UserError(f"Unknown path {', '.join(unknown)}. Paths of this system: {', '.join(names)}")
        paths = wanted or names or [0]  # empty: every path
        fields = None if p["fields"] == 0 else [p["fields"] - 1]
        wls = range(len(compiled.wavelengths_um)) if p["wavelengths"] == "all" else [None]
        self._warn_inactive(ctx)

        run = ctx.result_set(f"Trace {Path(self.loaded_from or 'system').name.split('.')[0]}")
        summary, failed = [], []
        irradiance: dict[int, dict] = {}  # last surface -> accumulated grid of every path ending there
        for k, path in enumerate(paths):
            ctx.check_cancel()
            label = path if isinstance(path, str) else "path"
            try:
                n_ok, n_total, last = self._draw_rays(ctx, system, compiled, run, path, label, k, fields, wls)
                summary.append(f"{label}: {n_ok}/{n_total}")
                if p["irradiance"]:
                    self._irradiance(ctx, system, compiled, path, fields, wls, last, irradiance,
                                     rings=rings_for(p["irradiance_rays"]), pixels=p["grid"])
            except UserError as e:
                if len(paths) == 1:
                    raise
                failed.append(label)
                ctx.log(f"path {label}: {e}")
        fields_on = []
        for acc in irradiance.values():
            values = acc["grid"].reshape(-1)  # values[j * width + i]: i along u (x), j along v (y)
            ctx.field(run, f"Irradiance {acc['surface']}", acc["entity"], values, acc["nx"], acc["ny"],
                      unit="W/mm² (rel.)")
            fields_on.append(f"{acc['surface']} (peak {float(values.max()) if values.size else 0.0:.3g}"
                             f"{', ' + ' + '.join(acc['paths']) if len(acc['paths']) > 1 else ''})")
        message = "rays reaching the end of the path – " + ", ".join(summary)
        if fields_on:
            message += "; irradiance on " + ", ".join(fields_on)
        if failed:
            message += f"; not traced: {', '.join(failed)} (see the plugin log)"
        return message

    def _draw_rays(self, ctx, system, compiled, run, path, label, index, fields, wls):
        """The recorded rays of one path as a line set; returns (reaching the end, total, last surface)."""
        p = ctx.params
        points, starts, values = [], [0], []
        n_ok = n_total = 0
        last_surface = None
        for w in wls:
            rays = self._make_rays(system, compiled, rings_for(p["rays"]), path, fields, w)
            stats, rec = rt.trace.trace(compiled, rays, path=path, record_path=True,
                                        max_recorded_rays=max(rays.size, 1))
            events = np.asarray(rec.event_surfaces)
            if events.size:
                last_surface = int(events[-1])
            pos = np.asarray(rec.position, dtype=float)  # (N, slots, 3); NaN after a loss
            idx = np.asarray(rec.ray_indices)
            wl_um = compiled.wavelengths_um[compiled.reference_wavelength if w is None else w]
            if p["colour_by"] == "wavelength":
                value = np.full(len(idx), wl_um * 1000.0)
            elif p["colour_by"] == "path":
                value = np.full(len(idx), float(index + 1))
            else:
                value = np.asarray(rays.field)[idx].astype(float) + 1
            for k in range(len(idx)):
                points.append(pos[k])
                starts.append(starts[-1] + pos.shape[1])
                values.append(np.full(pos.shape[1], value[k]))
            counts = stats.to_dict()["rays"]
            n_ok += counts[0]
            n_total += sum(counts)
        if points:
            unit = {"wavelength": "nm", "path": "path"}.get(p["colour_by"], "field")
            colormap = {"wavelength": "viridis", "path": "coolwarm"}.get(p["colour_by"], "plasma")
            ranges = (1.0, float(max(len(compiled.path_names), 2))) if p["colour_by"] == "path" else None
            ctx.lines(run, f"Rays {label}", np.concatenate(points), np.asarray(starts), np.concatenate(values),
                      unit=unit, colormap=colormap, value_range=ranges)
        return n_ok, n_total, last_surface

    def _irradiance(self, ctx, system, compiled, path, fields, wls, last_surface, acc: dict, *, rings: int,
                    pixels: int, progress_span: tuple[float, float] = (0.0, 1.0)) -> None:
        """Irradiance of one path on its last surface (Raytatouille #379, rt.analysis.irradiance_map)
        over the parameter domain of the drawn face, added to `acc` (paths ending on the same
        surface add up – incoherently)."""
        if not hasattr(rt.analysis, "irradiance_map"):
            raise UserError("The irradiance needs a newer Raytatouille (rt.analysis.irradiance_map, #379).")
        p = ctx.params
        det = self._detector_index(compiled) if last_surface is None else last_surface
        L = rt.layout.surfaces(compiled)[det]
        entity = next((e for e in self._entities(ctx) if e["attributes"].get("rtt.surface") == L.id), None)
        if entity is None:
            ctx.log(f"irradiance: {L.id} is not drawn")
            return
        knots_u = entity["geometry"]["surface"]["knots_u"]
        knots_v = entity["geometry"]["surface"]["knots_v"]
        if entity["geometry"]["surface"].get("num_v") == 9 and entity["geometry"]["surface"].get("degree_v") == 2:
            ctx.log(f"irradiance on {L.id}: curved surface of revolution – not mapped yet")
            return
        extent = (knots_u[0], knots_u[-1], knots_v[0], knots_v[-1])  # local x, y = face (u, v)
        # pixels along the long side, square pixels
        wx, wy = extent[1] - extent[0], extent[3] - extent[2]
        nx = pixels if wx >= wy else max(1, round(pixels * wx / wy))
        ny = pixels if wy > wx else max(1, round(pixels * wy / wx))
        entry = acc.setdefault(det, {"surface": L.id, "entity": entity["id"], "nx": nx, "ny": ny, "extent": extent,
                                     "grid": np.zeros((ny, nx)), "paths": [], "rays": 0})
        entry["paths"].append(str(path))
        lost = {"outside the grid": 0, "vignetted": 0, "not reached": 0}
        token = rt.CancelToken()

        wls = list(wls)
        a, b = progress_span
        state = {"step": 0}

        def progress(done, total, stage):
            if ctx.cancelled():
                token.cancel()
            part = (state["step"] + min(done / max(total, 1), 1.0)) / len(wls)
            ctx.progress(a + (b - a) * part, f"{path}: {stage} {done:,}/{total:,} rays")

        weights = compiled.wavelength_weights
        for w in wls:
            ctx.check_cancel()
            rays = self._make_rays(system, compiled, rings, path, fields, w, cancel=token, progress=progress)
            rt.trace.trace(compiled, rays, path=path, cancel=token, progress=progress)
            entry["rays"] += rays.size
            try:
                # batch form: any number of rays, if the last event cannot change the weight
                im = rt.analysis.irradiance_map(compiled, rays, path=path, nx=nx, ny=ny, extent=extent)
            except ValueError:
                # e.g. a reflecting or absorbing last surface: the form with recorded paths, whose
                # memory grows with the rays – at most 20 000 here
                few = min(rings, 80)
                if few < rings:
                    ctx.log(f"{L.id} ({path}): the last event changes the weight – computed with recorded "
                            f"paths and fewer rays ({few} pupil rings)")
                rays = self._make_rays(system, compiled, few, path, fields, w)
                _, rec = rt.trace.trace(compiled, rays, path=path, record_path=True,
                                        max_recorded_rays=max(rays.size, 1))
                im = rt.analysis.irradiance_map(compiled, rec, surface=L.id, path=path, nx=nx, ny=ny,
                                                extent=extent, wavelength=w)
            state["step"] += 1
            wl_weight = weights[compiled.reference_wavelength if w is None else w]
            entry["grid"] += np.asarray(im.values, dtype=float) * wl_weight  # (ny, nx): row j along v
            for key, attr in (("outside the grid", "outside_grid"), ("vignetted", "vignetted"),
                              ("not reached", "not_reached")):
                lost[key] += int(getattr(im, attr).count)
        missing = ", ".join(f"{k}: {v}" for k, v in lost.items() if v)
        if missing:
            ctx.log(f"irradiance on {L.id} ({path}): rays not counted – {missing}")

    def _warn_inactive(self, ctx):
        off = [e["name"] for e in self._entities(ctx) if e["attributes"].get("sim.inactive")]
        if off:
            ctx.log("marked inactive in CAD but still traced (Raytatouille #380 is not released yet): "
                    + ", ".join(off))

    @action("Camera image", description="Irradiance on the last surface of each path with many rays, as an image")
    def camera_image(self, ctx):
        _require_rt()
        system, _ = self._current(ctx)
        compiled = self._compile(system)
        p = ctx.params
        names = list(compiled.path_names)
        wanted = [n.strip() for n in p["path"].split(",") if n.strip()]
        unknown = [n for n in wanted if n not in names]
        if unknown:
            raise UserError(f"Unknown path {', '.join(unknown)}. Paths of this system: {', '.join(names)}")
        paths = wanted or names or [0]
        fields = None if p["fields"] == 0 else [p["fields"] - 1]
        wls = range(len(compiled.wavelengths_um)) if p["wavelengths"] == "all" else [None]
        n_fields = compiled.field_count if fields is None else 1
        rings = rings_for(p["camera_rays"])
        self._warn_inactive(ctx)
        run = ctx.result_set(f"Camera image {Path(self.loaded_from or 'system').name.split('.')[0]}")
        acc: dict[int, dict] = {}
        failed = []
        for k, path in enumerate(paths):
            ctx.check_cancel()
            try:
                last = self._last_surface(system, compiled, path)
                self._irradiance(ctx, system, compiled, path, fields, wls, last, acc, rings=rings,
                                 pixels=p["camera_pixels"], progress_span=(k / len(paths), (k + 1) / len(paths)))
            except UserError as e:
                if len(paths) == 1:
                    raise
                failed.append(str(path))
                ctx.log(f"path {path}: {e}")
        if not acc:
            raise UserError("No camera image: none of the paths ends on a drawn plane surface.")
        made = []
        for entry in acc.values():
            grid = entry["grid"]
            values = grid.reshape(-1)
            peak = float(grid.max()) if grid.size else 0.0
            x0, x1, y0, y1 = entry["extent"]
            description = (f"{entry['surface']}: {entry['rays']:,} rays ({', '.join(entry['paths'])}; "
                           f"{n_fields} field(s), {len(list(wls))} wavelength(s)), {entry['nx']}×{entry['ny']} px, "
                           f"pixel {(x1 - x0) / entry['nx'] * 1000:.3g} µm")
            ctx.image(run, f"Camera image {entry['surface']}", values, entry["nx"], entry["ny"],
                      extent=entry["extent"], unit="W/mm² (rel.)", description=description)
            if p["camera_on_surface"]:
                ctx.field(run, f"Irradiance {entry['surface']}", entry["entity"], values, entry["nx"], entry["ny"],
                          unit="W/mm² (rel.)")
            made.append(f"{entry['surface']} (peak {peak:.3g})")
        message = f"camera image on {', '.join(made)} from {sum(e['rays'] for e in acc.values()):,} rays"
        if failed:
            message += f"; not traced: {', '.join(failed)} (see the plugin log)"
        return message

    def _last_surface(self, system, compiled, path) -> int | None:
        """Index of the last surface of a path (one recorded ray)."""
        rays = self._make_rays(system, compiled, 1, path, [0], None)
        _, rec = rt.trace.trace(compiled, rays, path=path, record_path=True, record_rays=[0])
        events = np.asarray(rec.event_surfaces)
        return int(events[-1]) if events.size else None

    # ------------------------------------------------------------------ node workflow (RTT-Fusion)
    @staticmethod
    def _fusion():
        try:
            import importlib

            fg = importlib.import_module("rtt_fusion.graph")
            backend_rtt = importlib.import_module("rtt_fusion.graph.backend_rtt")
            execute = importlib.import_module("rtt_fusion.graph.execute")
            io = importlib.import_module("rtt_fusion.graph.io")
            validate = importlib.import_module("rtt_fusion.graph.validate")  # the module, not the function
            if not hasattr(fg, "plan"):
                raise ImportError("rtt_fusion without planner (R17)")
        except ImportError as e:
            raise UserError("The node workflow needs the Python package rtt_fusion (RTT-Fusion) in the plugin "
                            "environment: pip install -e <RTT-Fusion>") from e
        return fg, backend_rtt, execute, io, validate

    @staticmethod
    def _catalogue(registry) -> list[dict]:
        try:
            topology = importlib.import_module("rtt_fusion.graph.topology")
            node_cls = importlib.import_module("rtt_fusion.graph.model").Node
        except ImportError:
            topology = None
        out = []
        for t in registry.types():
            entry = {"type": t.type_id, "category": t.category.value, "description": t.description,
                     "sockets": [s.to_dict() for s in t.sockets],
                     "params": [p.to_dict() for p in t.params],
                     "through": {o: list(ins) for o, ins in t.through}}
            if topology is not None:  # light ports (RTT-Fusion R24): one dot per port in the editor
                entry["ports"] = [p.to_dict() for p in topology.node_ports(node_cls("x", t.type_id), registry)]
            out.append(entry)
        return out

    @staticmethod
    def _registry(fg):
        """Block types of the workflow: the built-in ones and, with RTT-Fusion R19, the grating
        (a sample from a FourierGrating Jones table). Returns (registry, extra evaluators)."""
        registry = fg.builtin_registry()
        extra = {}
        import importlib

        for module in ("rtt_fusion.graph.grating", "rtt_fusion.graph.particle", "rtt_fusion.graph.meta",
                       "rtt_fusion.graph.display"):  # R19, R20, R21
            try:
                extra.update(importlib.import_module(module).register(registry))
            except ImportError:
                pass
        return registry, extra

    @staticmethod
    def _available(execute) -> frozenset:
        """Representations the workflow can run: R, and H (fields, RTT-Fusion R18b) with fieldtracing."""
        try:
            import fieldtracing  # noqa: F401
        except ImportError:
            return execute.EXECUTABLE
        return getattr(execute, "WITH_FIELDS", execute.EXECUTABLE)

    @classmethod
    def _checked(cls, fg, execute, validate, graph, registry) -> tuple[list[dict], dict]:
        """Diagnostics of the graph check and of the planner (R17, with what can run), and the plan."""
        diagnostics = [d.__dict__ for d in validate.validate(graph, registry)]
        plan = fg.plan(graph, registry, fg.Requirements(available=cls._available(execute)))
        diagnostics += [d.__dict__ for d in plan.diagnostics]
        return diagnostics, plan.to_dict()

    @action("Workflow from system", description="Node workflow of the loaded system from its paths (RTT-Fusion)")
    def workflow_from_system(self, ctx):
        _require_rt()
        fg, _, execute, io, validate = self._fusion()
        system, _ = self._current(ctx)
        if self._has_model(fg):  # RTT-Fusion R24: ports, connections, the system's paths as branches
            registry, _ = self._registry(fg)
            model = fg.model_from_raytatouille(system)
            kept = self._keep_model_edits(fg, io, model, ctx.workflow(), registry)
            wf = self._model_workflow(fg, execute, io, validate, model, registry)
            ctx.set_workflow(wf)
            return (f"workflow: {len(model.nodes)} blocks, {len(model.connections)} connections, "
                    f"{len(model.branches)} branches" + (f"; kept {kept}" if kept else ""))
        graph = fg.graph_from_raytatouille(system)
        registry, _ = self._registry(fg)
        # every node with its effective sockets, so the editor needs no type knowledge
        for node in graph.nodes.values():
            if not node.sockets:
                node.sockets = validate.node_sockets(node, registry)
        kept = self._keep_edits(io, graph, ctx.workflow().get("graph"))
        diagnostics, plan = self._checked(fg, execute, validate, graph, registry)
        ctx.set_workflow({"graph": io.to_dict(graph), "types": self._catalogue(registry),
                          "diagnostics": diagnostics, "plan": plan})
        return f"workflow: {len(graph.nodes)} blocks, {len(graph.links)} links" + (f"; kept {kept}" if kept else "")

    @staticmethod
    def _keep_edits(io, graph, old) -> str:
        """Carries the user's edits of the previous workflow over to a new import of the same system:
        positions, muted blocks, parameters and type of blocks that were replaced (same id), link
        transports, and blocks the import does not make (e.g. meta blocks) with their links."""
        if not old:
            return ""
        try:
            previous = io.from_dict(old)
        except Exception:  # noqa: BLE001 - an unreadable old workflow is simply replaced
            return ""
        counts = {"blocks": 0, "transports": 0, "added": 0}
        for nid, node in previous.nodes.items():
            if nid in graph.nodes:
                new = graph.nodes[nid]
                new.position = node.position
                new.muted = node.muted
                if node.type_id != new.type_id or node.params != new.params:
                    if node.type_id != new.type_id:  # replaced in the editor (e.g. grating)
                        new.type_id, new.sockets, new.through = node.type_id, node.sockets, node.through
                    new.params = dict(node.params)
                    counts["blocks"] += 1
            else:
                graph.add(node)
                counts["added"] += 1
        existing = {(lk.source, lk.target) for lk in graph.links}
        for lk in previous.links:
            match = next((x for x in graph.links if (x.source, x.target) == (lk.source, lk.target)), None)
            if match is not None and lk.transport != match.transport:
                graph.set_transport(match, lk.transport)
                counts["transports"] += 1
            elif match is None and (lk.source, lk.target) not in existing \
                    and lk.source[0] in graph.nodes and lk.target[0] in graph.nodes:
                graph.connect(lk.source, lk.target, lk.transport)
        return ", ".join(f"{v} {k}" for k, v in counts.items() if v)

    @action("Check workflow", description="Check the node workflow (RTT-Fusion) and mark problems")
    def check_workflow(self, ctx):
        fg, _, execute, io, validate = self._fusion()
        wf = ctx.workflow()
        if not wf.get("graph") and not wf.get("model"):
            raise UserError("No node workflow yet – Workflow from system creates one.")
        registry, _ = self._registry(fg)
        if wf.get("model"):
            model = io.model_from_dict(wf["model"], registry)
            wf = {**wf, **self._model_workflow(fg, execute, io, validate, model, registry)}
            graph = fg.unroll(model, registry) if not any(
                d["severity"] == "error" for d in wf["diagnostics"]) else None
            self._model_ids = set(model.nodes)
        else:
            graph = io.from_dict(wf["graph"])
            wf["types"] = self._catalogue(registry)
            wf["diagnostics"], wf["plan"] = self._checked(fg, execute, validate, graph, registry)
            self._model_ids = None
        ctx.set_workflow(wf)
        if graph is not None and not any(d["severity"] == "error" for d in wf["diagnostics"]) and self.loaded_from:
            try:  # which blocks the kept results still cover
                system, _ = self._current(ctx)
                _, extra = self._registry(fg)
                backend_rtt = importlib.import_module("rtt_fusion.graph.backend_rtt")
                ex = self._executor(ctx, graph, registry, {**backend_rtt.EVALUATORS, **extra}, execute,
                                    system, None)
                self._send_status(ctx, ex)
            except Exception as e:  # noqa: BLE001 - only a hint
                ctx.log(f"workflow state: {e}")
        diagnostics = wf["diagnostics"]
        n_err = sum(1 for d in diagnostics if d["severity"] == "error")
        for d in diagnostics:
            ctx.log(f"{d['severity']}: {d['code']} – {d['message']} ({d['location']})")
        planned = sorted(set(v["representation"] for v in wf["plan"].get("links", {}).values()))
        return (f"workflow: {n_err} error(s), {len(diagnostics) - n_err} warning(s); "
                f"planned {''.join(planned) or '–'}, {len(wf['plan']['conversions'])} conversion(s)")

    @action("Run workflow", description="Compute what the display blocks of the node workflow show (RTT-Fusion): "
                                         "camera images, sweep curves, beam probes; links set to H as fields")
    def run_workflow(self, ctx):
        _require_rt()
        fg, backend_rtt, execute, io, validate = self._fusion()
        wf = ctx.workflow()
        if not wf.get("graph") and not wf.get("model"):
            raise UserError("No node workflow yet – Workflow from system creates one.")
        registry, extra = self._registry(fg)
        if wf.get("model"):  # RTT-Fusion R24: exactly the branches of the model, unrolled
            model = io.model_from_dict(wf["model"], registry)
            try:
                graph = fg.unroll(model, registry)
            except fg.ModelError as e:
                for d in e.diagnostics:
                    ctx.log(f"{d.severity}: {d.code} – {d.message} ({d.location})")
                raise UserError(f"The workflow has errors – Check workflow marks them: {e}") from e
            self._model_ids = set(model.nodes)
        else:
            graph = io.from_dict(wf["graph"])
            self._model_ids = None
        evaluators = {**backend_rtt.EVALUATORS, **extra}
        displays = [n for n in graph.nodes.values() if n.type_id.startswith("display.")]
        if not displays:
            raise UserError("Nothing to show: attach a display block (right-click on the canvas → Add block → "
                            "analysis → display.image / display.plot / display.beam) to a camera, a phase "
                            "shifter or a component output, then run again. Only what the displays need is "
                            "computed.")
        system, _ = self._current(ctx)
        compiled = self._compile(system)
        window = float(ctx.params.get("workflow_window", 0.0) or 0.0)
        if window > 0:
            for n in graph.nodes.values():
                if n.type_id == "rtt.detector":
                    n.params["half_size"] = [window, window]
        run = ctx.result_set(f"Workflow {Path(self.loaded_from or 'system').name.split('.')[0]}")
        made = self._run_displays(ctx, run, graph, registry, evaluators, execute, system, compiled, displays)
        ctx.workflow_running("")
        return "workflow: " + (", ".join(made) if made else "nothing computed")

    @staticmethod
    def _hook(ctx, ex, a: float, b: float) -> None:
        """Progress of an executor (RTT-Fusion R21) into a part a … b of the job, the running
        block into the node editor; every report checks for Cancel."""
        last = {"node": None}

        def progress(part: float, text: str) -> None:
            ctx.check_cancel()
            ctx.progress(a + (b - a) * part, text)

        def on_node(node_id: str) -> None:
            node_id = RaytracerPlugin._model_id(node_id)
            if node_id != last["node"]:
                last["node"] = node_id
                ctx.workflow_running(node_id)

        if hasattr(ex.context, "progress"):
            ex.context.progress = progress
            ex.context.on_node = on_node

    def _run_displays(self, ctx, run, graph, registry, evaluators, execute, system, compiled, displays) -> list:
        """Display blocks (RTT-Fusion R21) and everything in front of them, in one executor (shared
        blocks are computed once); their views as results with the key "workflow:<block>", shown in
        the block's window (double-click)."""
        made = []
        ex = self._executor(ctx, graph, registry, evaluators, execute, system, compiled)
        for k, node in enumerate(displays):
            ctx.check_cancel()
            a, b = k / len(displays), (k + 1) / len(displays)
            self._hook(ctx, ex, a, b)
            ctx.progress(a, f"display: {node.name or node.id}")
            try:
                view = ex.evaluate(node.id, "view")
            except execute.ExecutionError as e:
                ctx.log(f"{node.name or node.id}: {e}")
                continue
            key = f"workflow:{node.id}"
            if view.kind == "image" and view.image is not None:
                ny, nx = view.image.values.shape
                if float(view.image.values.max()) <= 0.0:
                    hint = ("set the link into the camera to H (right-click on the link → Transport → H) and "
                            "choose a small image window" if "H" in self._available(execute) else
                            "install fieldtracing in the plugin environment, then set the link into the "
                            "camera to H")
                    ctx.log(f"{node.name or node.id}: dark – the light focuses into less than a pixel; {hint}")
                ctx.image(run, view.title, view.image.values.reshape(-1), nx, ny, extent=view.image.extent,
                          x_label=view.x_label, y_label=view.y_label, unit=view.unit,
                          description=f"{view.title}: {nx}×{ny} px", key=key)
            else:
                for c in view.curves:
                    ctx.curve(run, f"{view.title}: {c.name}", c.x, c.y, x_label=view.x_label,
                              y_label=view.y_label, key=key)
            made.append(f"{node.name or node.id}")
        for line in ex.context.log:
            ctx.log(line)
        cache = getattr(ex, "cache", None)
        for line in getattr(cache, "warnings", [])[-3:]:
            ctx.log(line)
        if cache is not None and hasattr(cache, "warnings"):
            cache.warnings.clear()
        self._send_status(ctx, ex)
        return made

    def _executor(self, ctx, graph, registry, evaluators, execute, system, compiled):
        """An executor with the results of earlier runs (RTT-Fusion R23): only what changed is
        computed again. The environment covers what the results depend on outside the graph."""
        shared = {"system": system, "compiled": compiled, "materials": self.library,
                  "rings": rings_for(ctx.params["workflow_rays"]),
                  "pixels": [ctx.params["workflow_pixels"]] * 2,
                  "gaussian_waist": float(ctx.params.get("workflow_gaussian_waist", 0.0) or 0.0)}
        if compiled is None:
            shared.pop("compiled")  # the backend compiles when it needs to
        kwargs = {"available": self._available(execute)}
        if hasattr(execute, "ResultCache"):
            if getattr(self, "_workflow_cache", None) is None:
                self._workflow_cache = execute.ResultCache(limit_bytes=1 << 30)
            path = self.loaded_from or ""
            try:
                stamp = os.path.getmtime(path) if path else 0.0
            except OSError:
                stamp = 0.0
            kwargs["cache"] = self._workflow_cache
            kwargs["environment"] = {
                "system": path, "modified": stamp,
                "system_json_hash": hash(system.to_json()) if hasattr(system, "to_json") else 0,
                "rings": shared["rings"], "pixels": shared["pixels"], "window": ctx.params.get("workflow_window"),
                "gaussian": shared["gaussian_waist"], "catalogs": list(ctx.params.get("catalogs") or []),
                "available": sorted(kwargs["available"]),
            }
        return execute.Executor(graph, registry, evaluators, shared, **kwargs)

    def _send_status(self, ctx, ex) -> None:
        """Up to date / out of date per block (RTT-Fusion R23) for the node editor; with a model
        (R24) per block of the model: all its copies (id@k) and the blocks of a group."""
        if not hasattr(ex, "status"):
            return
        try:
            # what the display blocks need (RTT-Fusion: status(wanted=…)); a block nobody needs
            # gets no mark, an output nobody needs (a splitter's way to an unshown camera) none
            shown = [(n.id, "view") for n in ex.graph.nodes.values() if n.type_id.startswith("display.")]
            try:
                raw = ex.status(wanted=shown)
            except TypeError:  # RTT-Fusion before the status for what is needed
                raw = ex.status()
            states = {k: bool(v) for k, v in raw.items()}
            ids = getattr(self, "_model_ids", None)
            if ids is not None:
                merged: dict = {}
                for k, v in states.items():
                    m = self._model_id(k)
                    if m in ids:
                        merged[m] = merged.get(m, True) and v
                states = merged
            ctx.workflow_state(states)
        except Exception as e:  # noqa: BLE001 - the state is a display hint only
            ctx.log(f"workflow state: {e}")

    # ------------------------------------------------------------------ the model (RTT-Fusion R24)
    @staticmethod
    def _has_model(fg) -> bool:
        return hasattr(fg, "model_from_raytatouille") and hasattr(fg, "unroll")

    @staticmethod
    def _model_id(unrolled: str) -> str:
        """The block of the model behind a block of the unrolled graph: a copy "x@2" is "x", a block
        "g/x" inside a group is the group "g"."""
        return unrolled.split("@")[0].split("/")[0]

    def _model_workflow(self, fg, execute, io, validate, model, registry) -> dict:
        """The workflow of a model: the model (every block with its sockets and ports, so the
        editor needs no type knowledge), the block library, the checks of the model and of the
        unrolled graph, and the plan – per link of the unrolled graph and per connection of the
        model (direction "forward" a -> b, "back")."""
        topology = importlib.import_module("rtt_fusion.graph.topology")
        for node in model.nodes.values():  # with the inputs every block has (drive, pose)
            node.sockets = tuple(validate.node_sockets(node, registry))
            if not node.ports:
                node.ports = topology.node_ports(node, registry)
        diagnostics = [d.__dict__ for d in fg.validate_model(model, registry)]
        plan: dict = {"links": {}, "conversions": [], "connections": {}}
        if not any(d["severity"] == "error" for d in diagnostics):
            graph = fg.unroll(model, registry)
            checked, plan = self._checked(fg, execute, validate, graph, registry)
            diagnostics += checked
            plan["connections"] = self._connection_plan(graph, model, registry, topology, plan)
        for d in diagnostics:  # copies and blocks inside groups: the block of the model
            d["location"] = re.sub(r"@\d+", "", str(d["location"]))
        data = io.model_to_dict(model)
        for nd in data["nodes"]:  # the editable data of the system (ADR Nachtrag 1)
            fields = self._rtt_fields(nd)
            if fields:
                nd["fields"] = fields
        return {"model": data, "types": self._catalogue(registry),
                "diagnostics": diagnostics, "plan": plan}

    # keys that carry the structure (ids are referenced by the paths; a type changes the shape of
    # the entry): shown nowhere, changed by the import or the library only
    _STRUCTURE_KEYS = frozenset({"id", "type", "children", "schema_version", "units"})
    _LENGTH_KEYS = frozenset({"radius", "inner_radius", "position", "pivot", "half_width_x",
                              "half_width_y", "semi_axis_x", "semi_axis_y", "thickness", "distance",
                              "normalization_radius", "value_mm"})
    _VECTOR_KEYS = frozenset({"position", "pivot", "rotation_deg", "transmission_axis", "normal",
                              "axis", "fast_axis", "optic_axis"})

    @classmethod
    def _rtt_fields(cls, node: dict) -> list:
        """The editable fields of a node's Raytatouille data (ADR Nachtrag 1: the plugin says what
        a node of it holds): one field per value, named by its JSON pointer into the node's
        params, grouped by element; units from Raytatouille's conventions (lengths mm, angles in
        degree where the key ends in _deg, wavelengths µm)."""
        params = node.get("params", {})
        out: list = []

        def pointer(parts) -> str:
            return "/" + "/".join(str(p).replace("~", "~0").replace("/", "~1") for p in parts)

        def unit(key: str) -> str:
            if key.endswith("_deg"):
                return "deg"
            if key in cls._LENGTH_KEYS:
                return "mm"
            return "µm" if key == "um" else ""

        def add(path, kind, label, group, key):
            out.append({"name": pointer(path), "type": kind, "label": label, "group": group,
                        "unit": unit(key)})

        def walk(value, path, label, group, key=""):
            if isinstance(value, bool):
                add(path, "boolean", label, group, key)
            elif isinstance(value, int | float):
                add(path, "number", label, group, key)
            elif isinstance(value, str):
                add(path, "text", label, group, key)
            elif isinstance(value, dict):
                if "value" in value and isinstance(value["value"], int | float) and not isinstance(value["value"], bool):
                    add([*path, "value"], "number", label, group, key)  # a Raytatouille parameter
                    if "variable" in value:
                        add([*path, "variable"], "boolean", f"{label} variable", group, "")
                    return
                if set(value) == {"param"}:
                    add([*path, "param"], "text", f"{label} (row of the parameter table)", group, "")
                    return
                for k, v in value.items():
                    if k in cls._STRUCTURE_KEYS:
                        continue
                    walk(v, [*path, k], f"{label} · {k}" if label else k, group, k)
            elif isinstance(value, list):
                plain = all(isinstance(x, int | float) and not isinstance(x, bool) for x in value)
                if plain and value:
                    kind = "point" if len(value) == 3 and key in cls._VECTOR_KEYS else "numbers"
                    add(path, kind, label, group, key)
                    return
                for i, item in enumerate(value):
                    tag = item.get("id") or item.get("name") if isinstance(item, dict) else None
                    walk(item, [*path, i], f"{label}[{i}]" + (f" ({tag})" if tag else ""), group, key)

        for location, entry in params.get("rtt", {}).items():
            group = str(entry.get("name") or entry.get("type") or location)
            walk(entry, ["rtt", location], "", group)
        for location, entry in params.get("rtt_tree", {}).items():
            if location == "/root" and not {k for k in entry if k not in cls._STRUCTURE_KEYS}:
                continue
            group = str(entry.get("name") or entry.get("type") or location)
            walk(entry, ["rtt_tree", location], "", group)
        if "rtt_system" in params:
            walk(params["rtt_system"], ["rtt_system"], "", "System")
        return out

    @staticmethod
    def _connection_plan(graph, model, registry, topology, plan) -> dict:
        out: dict = {}
        ports = {nid: topology.node_ports(n, registry) for nid, n in model.nodes.items()}
        for lk in graph.links:
            key = f"{lk.source[0]}.{lk.source[1]} -> {lk.target[0]}.{lk.target[1]}"
            if key not in plan.get("links", {}):
                continue
            a, b = RaytracerPlugin._model_id(lk.source[0]), RaytracerPlugin._model_id(lk.target[0])
            pa = next((p.name for p in ports.get(a, ()) if p.out_socket == lk.source[1]), None)
            pb = next((p.name for p in ports.get(b, ()) if p.in_socket == lk.target[1]), None)
            for i, c in enumerate(model.connections):
                if {c.a, c.b} == {(a, pa), (b, pb)}:
                    side = "forward" if c.a == (a, pa) else "back"
                    out.setdefault(str(i), {})[side] = plan["links"][key]["representation"]
        return out

    @staticmethod
    def _keep_model_edits(fg, io, model, old_wf, registry) -> str:
        """The user's edits of the previous workflow (a model, or an old socket graph converted)
        on a new import: positions, muted blocks, parameters, replaced blocks (with their ports),
        added blocks and their links, transports of connections, and branches the import does
        not make."""
        try:
            if old_wf.get("model"):
                previous = io.model_from_dict(old_wf["model"], registry)
            elif old_wf.get("graph"):
                previous = fg.model_from_graph(io.from_dict(old_wf["graph"]), registry)
            else:
                return ""
        except Exception:  # noqa: BLE001 - an unreadable old workflow is simply replaced
            return ""
        counts = {"blocks": 0, "transports": 0, "added": 0, "branches": 0}
        replaced: dict = {}
        for nid, node in previous.nodes.items():
            if nid in model.nodes:
                new = model.nodes[nid]
                new.position, new.muted = node.position, node.muted
                if node.type_id != new.type_id or node.params != new.params:
                    if node.type_id != new.type_id:  # replaced in the editor (e.g. a grating)
                        replaced[nid] = node
                    new.params = dict(node.params)
                    counts["blocks"] += 1
            else:
                model.add(node)
                counts["added"] += 1
                if nid in previous.groups:
                    model.groups[nid] = previous.groups[nid]
        for nid, old_node in replaced.items():  # its ports: matched over the neighbours
            new = model.nodes[nid]
            mapping = {}
            for c in model.connections:
                for end, other in ((c.a, c.b), (c.b, c.a)):
                    if end[0] != nid:
                        continue
                    before = next((o for o in previous.connections for e, f in ((o.a, o.b), (o.b, o.a))
                                   if e[0] == nid and f[0] == other[0]), None)
                    if before is not None:
                        mine = before.a if before.a[0] == nid else before.b
                        mapping[end[1]] = mine[1]
            new.type_id, new.sockets, new.through, new.ports = (old_node.type_id, old_node.sockets,
                                                                old_node.through, old_node.ports)
            model.connections = [
                type(c)((c.a[0], mapping.get(c.a[1], c.a[1])) if c.a[0] == nid else c.a,
                        (c.b[0], mapping.get(c.b[1], c.b[1])) if c.b[0] == nid else c.b,
                        c.transport, c.transport_back)
                for c in model.connections]
            model.branches = [
                type(b)(b.name, tuple(
                    type(s)(s.node, mapping.get(s.port_in, s.port_in), mapping.get(s.port_out, s.port_out),
                            "") if s.node == nid else s for s in b.steps))
                for b in model.branches]
        for old in previous.connections:
            for i, c in enumerate(model.connections):
                if {c.a, c.b} == {old.a, old.b} and (c.transport, c.transport_back) != (old.transport,
                                                                                       old.transport_back):
                    forward = old.transport if c.a == old.a else (old.transport_back or old.transport)
                    back = (old.transport_back or old.transport) if c.a == old.a else old.transport
                    model.connections[i] = type(c)(c.a, c.b, forward, None if back == forward else back)
                    counts["transports"] += 1
        known = {(lk.source, lk.target) for lk in model.links}
        for lk in previous.links:
            if (lk.source, lk.target) not in known and lk.source[0] in model.nodes \
                    and lk.target[0] in model.nodes:
                model.links.append(lk)
        names = {b.name for b in model.branches}
        for b in previous.branches:
            if b.name not in names and all(s.node in model.nodes for s in b.steps):
                model.branches.append(b)
                counts["branches"] += 1
        for target, branch in previous.observe_branch.items():
            model.observe_branch.setdefault(target, branch)
        return ", ".join(f"{v} {k}" for k, v in counts.items() if v)

    @action("Optimise", description="Raytatouille's optimiser; the surfaces update after every iteration")
    def optimise(self, ctx):
        _require_rt()
        system, _ = self._current(ctx)
        tol = ctx.params["tolerance"]
        entity_of = {e["attributes"]["rtt.surface"]: e["id"] for e in self._entities(ctx)
                     if "rtt.surface" in e["attributes"]}
        rims = {e["name"]: e["id"] for e in self._entities(ctx) if "rtt.rim" in e["attributes"]}
        run = ctx.result_set("Optimisation")
        curve = ctx.curve(run, "Merit function", x_label="iteration", y_label="log10 φ")
        token = rt.CancelToken()

        def redraw(sys_k):
            compiled = self._compile(sys_k)
            for i, face in self._faces(sys_k, compiled, tol):
                sid = compiled.surface_ids[i]
                if sid in entity_of:
                    ctx.replace(entity_of[sid], face.geometry)
            for element in rt.layout.elements(compiled):
                for n, rim in enumerate(rtt_nurbs.rim_faces(compiled, element)):
                    name = f"{element.name} edge" + (f" {n + 1}" if n else "")
                    if name in rims:
                        ctx.replace(rims[name], rim)

        def on_iteration(state):
            if ctx.cancelled():
                raise KeyboardInterrupt  # ends the run like a cancellation (Raytatouille #378)
            ctx.append(curve, state.k, math.log10(max(state.phi, 1e-300)))
            ctx.progress(0.0, f"iteration {state.k}: φ = {state.phi:.4g}")
            redraw(rt.edit.apply_patch(system, state.patch))

        def progress(done, total, stage):
            if ctx.cancelled():
                token.cancel()

        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            result = rt.optim.optimize(system, materials=self.library, cancel=token, progress=progress,
                                       on_iteration=on_iteration)
        redraw(result.system)
        rev = self._new_revision(result.system)
        anchor = self._anchor(ctx)
        ctx.set_attribute(anchor["id"], "rtt.revision", rev)  # undone with the run (follows CAD undo)
        phis = np.asarray(result.history.phi)
        phi = float(phis[-1]) if phis.size else float("nan")
        status = str(result.status).split(".")[-1]
        return f"{status} after {result.iterations} iterations, φ = {phi:.4g}"

    @action("Save system", description="Write the current (optimised) system as .rtt.json")
    def save(self, ctx):
        _require_rt()
        system, _ = self._current(ctx)
        target = ctx.params["save_as"]
        if not target:
            source = self.loaded_from or ctx.params["system"]
            base = source[:-len(".rtt.json")] if source.endswith(".rtt.json") else os.path.splitext(source)[0]
            target = base + ".optimised.rtt.json"
        rt.save(system, target)
        return f"saved {target}"
