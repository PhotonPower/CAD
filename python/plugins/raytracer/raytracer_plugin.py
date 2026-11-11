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
import math
import os
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
        "path": Param.text("", label="Path", group="Trace", description="Name of the path; empty: the first"),
        "fields": Param.integer(0, min=0, label="Field", group="Trace", description="0: all fields"),
        "wavelengths": Param.choice(["all", "reference"], "all", label="Wavelengths", group="Trace"),
        "rings": Param.integer(6, min=1, max=60, label="Pupil rings (drawn)", group="Trace",
                               description="Hexapolar sampling of the rays that are drawn"),
        "colour_by": Param.choice(["wavelength", "field"], "wavelength", label="Colour by", group="Trace"),
        "irradiance": Param.boolean(True, label="Irradiance on the image", group="Trace"),
        "irradiance_rings": Param.integer(40, min=2, max=400, label="Pupil rings (irradiance)", group="Trace"),
        "grid": Param.integer(128, min=8, max=1024, label="Irradiance grid", group="Trace"),
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

    def _detector_half(self, compiled) -> tuple[float, float]:
        """Size of an image surface without aperture: the spread of a quick trace, plus margin."""
        idx = self._detector_index(compiled)
        L = rt.layout.surfaces(compiled)[idx]
        try:
            rays = rt.trace.make_rays(compiled, rt.trace.HexapolarPupil(rings=4))
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

    def _faces(self, compiled, tol: float):
        """(surface index, Face) for every surface, the detector with its derived size."""
        layouts = rt.layout.surfaces(compiled)
        radii = [a.aperture[1].get("radius", 0) for a in layouts if a.aperture]
        fallback = max(radii + [10.0])
        det = self._detector_index(compiled)
        faces = []
        for i, L in enumerate(layouts):
            override = None
            if i == det and L.aperture is None:
                override = self.detector_half or self._detector_half(compiled)
                self.detector_half = override
            faces.append((i, rtt_nurbs.surface_face(compiled, i, tol=tol, fallback_half=fallback,
                                                    half_override=override)))
        return faces

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
        faces = self._faces(compiled, tol)
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
        path = p["path"] or 0
        fields = None if p["fields"] == 0 else [p["fields"] - 1]
        wls = range(len(compiled.wavelengths_um)) if p["wavelengths"] == "all" else [None]
        self._warn_inactive(ctx)

        # drawn rays (recorded paths)
        run = ctx.result_set(f"Trace {Path(self.loaded_from or 'system').name.split('.')[0]}")
        points, starts, values = [], [0], []
        n_ok = n_total = 0
        last_surface = None  # the last surface of the path (where the irradiance is computed)
        for w in wls:
            ctx.check_cancel()
            rays = rt.trace.make_rays(compiled, rt.trace.HexapolarPupil(rings=p["rings"]), path=path,
                                      fields=fields, wavelength=w)
            stats, rec = rt.trace.trace(compiled, rays, path=path, record_path=True,
                                        max_recorded_rays=max(rays.size, 1))
            events = np.asarray(rec.event_surfaces)
            if events.size:
                last_surface = int(events[-1])
            pos = np.asarray(rec.position, dtype=float)  # (N, slots, 3); NaN after a loss
            idx = np.asarray(rec.ray_indices)
            wl_um = compiled.wavelengths_um[compiled.reference_wavelength if w is None else w]
            value = (np.full(len(idx), wl_um * 1000.0) if p["colour_by"] == "wavelength"
                     else np.asarray(rays.field)[idx].astype(float) + 1)
            for k in range(len(idx)):
                points.append(pos[k])
                starts.append(starts[-1] + pos.shape[1])
                values.append(np.full(pos.shape[1], value[k]))
            counts = stats.to_dict()["rays"]
            n_ok += counts[0]
            n_total += sum(counts)
        if points:
            ctx.lines(run, "Rays", np.concatenate(points), np.asarray(starts), np.concatenate(values),
                      unit="nm" if p["colour_by"] == "wavelength" else "field",
                      colormap="viridis" if p["colour_by"] == "wavelength" else "plasma")

        message = f"{n_ok} of {n_total} drawn rays reach the end of the path"
        if p["irradiance"]:
            message += "; " + self._irradiance(ctx, compiled, run, path, fields, wls, last_surface)
        return message

    def _irradiance(self, ctx, compiled, run, path, fields, wls, last_surface: int | None) -> str:
        """Irradiance on the last surface of the path (Raytatouille #379, rt.analysis.irradiance_map)
        over the parameter domain of its drawn face, so the field covers the face exactly."""
        if not hasattr(rt.analysis, "irradiance_map"):
            raise RuntimeError("the irradiance needs a newer Raytatouille (rt.analysis.irradiance_map, #379)")
        p = ctx.params
        det = self._detector_index(compiled) if last_surface is None else last_surface
        L = rt.layout.surfaces(compiled)[det]
        entity = next((e for e in self._entities(ctx) if e["attributes"].get("rtt.surface") == L.id), None)
        if entity is None:
            return f"{L.id} is not drawn"
        knots_u = entity["geometry"]["surface"]["knots_u"]
        knots_v = entity["geometry"]["surface"]["knots_v"]
        extent = (knots_u[0], knots_u[-1], knots_v[0], knots_v[-1])  # local x, y = face (u, v)
        n = p["grid"]
        grid = np.zeros((n, n))
        lost = {"outside the grid": 0, "vignetted": 0, "not reached": 0}
        token = rt.CancelToken()

        def progress(done, total, stage):
            if ctx.cancelled():
                token.cancel()
            ctx.progress(min(done / max(total, 1), 1.0), f"irradiance: {stage} {done}/{total}")

        weights = compiled.wavelength_weights
        for w in wls:
            sampling = rt.trace.HexapolarPupil(rings=p["irradiance_rings"])
            rays = rt.trace.make_rays(compiled, sampling, path=path, fields=fields, wavelength=w,
                                      cancel=token, progress=progress)
            rt.trace.trace(compiled, rays, path=path, cancel=token, progress=progress)
            try:
                # batch form: any number of rays, if the last event cannot change the weight
                im = rt.analysis.irradiance_map(compiled, rays, path=path, nx=n, ny=n, extent=extent)
            except ValueError:
                # e.g. a reflecting or absorbing last surface: the form with recorded paths
                rays = rt.trace.make_rays(compiled, sampling, path=path, fields=fields, wavelength=w)
                _, rec = rt.trace.trace(compiled, rays, path=path, record_path=True,
                                        max_recorded_rays=max(rays.size, 1))
                im = rt.analysis.irradiance_map(compiled, rec, surface=L.id, path=path, nx=n, ny=n,
                                                extent=extent, wavelength=w)
            wl_weight = weights[compiled.reference_wavelength if w is None else w]
            grid += np.asarray(im.values, dtype=float) * wl_weight  # (ny, nx): row j along v
            for key, attr in (("outside the grid", "outside_grid"), ("vignetted", "vignetted"),
                              ("not reached", "not_reached")):
                lost[key] += int(getattr(im, attr).count)
        values = grid.reshape(-1)  # values[j * width + i]: i along u (x), j along v (y)
        ctx.field(run, "Irradiance", entity["id"], values, n, n, unit="W/mm² (rel.)")
        missing = ", ".join(f"{k}: {v}" for k, v in lost.items() if v)
        if missing:
            ctx.log(f"irradiance on {L.id}: rays not counted – {missing}")
        peak = float(values.max()) if values.size else 0.0
        return f"irradiance on {L.id} (peak {peak:.3g})"

    def _warn_inactive(self, ctx):
        off = [e["name"] for e in self._entities(ctx) if e["attributes"].get("sim.inactive")]
        if off:
            ctx.log("marked inactive in CAD but still traced (Raytatouille #380 is not released yet): "
                    + ", ".join(off))

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
            for i, face in self._faces(compiled, tol):
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
