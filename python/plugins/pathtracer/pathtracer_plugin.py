"""Path Tracer plugin: CAD objects as light sources, materials on the others, irradiance as
textures on the faces and a camera image – by light tracing, path tracing or bidirectional path
tracing (README.md). The computation is in the pt_* modules (NumPy only, no CAD)."""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np

from nfcad import Param, Plugin, UserError, action

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pt_bsdf  # noqa: E402
import pt_integrator  # noqa: E402
import pt_scene  # noqa: E402
import pt_setup  # noqa: E402

_SURFACES = ["surface", "trimmed_surface", "shell"]

_MATERIAL = {
    "name": Param.text("", label="Name"),
    "kind": Param.choice(pt_bsdf.KINDS, "diffuse", label="Kind"),
    "reflectance": Param.number(0.8, min=0.0, max=1.0, decimals=3, label="Reflectance"),
    "transmittance": Param.number(0.0, min=0.0, max=1.0, decimals=3, label="Transmittance"),
    "roughness": Param.number(0.2, min=0.001, max=1.0, decimals=3, label="Roughness"),
    "ior": Param.number(1.5, min=1.0, max=10.0, decimals=4, label="Index"),
    "absorption": Param.number(0.0, min=0.0, decimals=5, unit="1/mm", label="Absorption"),
}
_SOURCE = {
    "name": Param.text("", label="Name"),
    "flux": Param.number(1.0, min=0.0, decimals=6, unit="W", label="Flux"),
    "half_angle": Param.number(90.0, min=0.01, max=90.0, decimals=2, unit="°", label="Half-angle"),
    "side": Param.choice(["front", "back", "both"], "front", label="Side"),
}


class PathTracerPlugin(Plugin):
    params = {
        "materials": Param.table(_MATERIAL, pt_setup.DEFAULT_MATERIALS, label="Materials", group="Materials",
                                 description="absorber; diffuse (reflectance, transmittance); mirror (reflectance); "
                                             "glossy (reflectance, roughness); glass (index, absorption)"),
        "default_material": Param.text("white", label="Material of the other objects", group="Materials",
                                       description="For objects without a material of their own"),
        "assign_material": Param.text("white", label="Material for the selection", group="Materials",
                                      description="Used by \"Selection: set material\""),
        "sources": Param.table(_SOURCE, pt_setup.DEFAULT_SOURCES, label="Sources", group="Sources",
                               description="Surfaces emit with constant radiance into a cone around the normal "
                                           "(half-angle 90°: Lambertian) and share the flux by area; points emit "
                                           "evenly into all directions"),
        "assign_source": Param.text("lamp", label="Source for the selection", group="Sources",
                                    description="Used by \"Selection: make it a source\""),
        "method": Param.choice(list(pt_setup.METHODS), "light tracing", label="Method", group="Computation",
                               description="light tracing: from the sources; path tracing: from the sensor, with "
                                           "connections to the sources; bidirectional: both, combined"),
        "samples": Param.integer(500_000, min=1_000, max=2_000_000_000, label="Samples", group="Computation",
                                 description="Paths (pairs of a light and an eye path); the noise falls with "
                                             "the square root"),
        "depth": Param.integer(8, min=1, max=100, label="Longest path", unit="segments", group="Computation",
                               description="Segments from the source to the sensor: 1 is direct light only"),
        "tolerance": Param.number(0.01, min=1e-6, max=100.0, decimals=6, unit="mm", label="Mesh tolerance",
                                  group="Computation",
                                  description="Largest distance of the triangles from the surfaces"),
        "seed": Param.integer(1, min=0, label="Random seed", group="Computation"),
        "irradiance_on": Param.entity(kinds=_SURFACES, multiple=True, label="Irradiance on", group="Irradiance",
                                      description="Empty: every surface of the model"),
        "texels": Param.integer(64, min=1, max=1024, label="Texels (longer side)", group="Irradiance"),
        "common_scale": Param.boolean(True, label="One colour scale for all faces", group="Irradiance"),
        "camera_position": Param.point((300.0, -300.0, 200.0), label="Position", group="Camera"),
        "camera_target": Param.point((0.0, 0.0, 0.0), label="Looks at", group="Camera"),
        "camera_up": Param.choice(list(pt_setup.UP), "+Z", label="Up", group="Camera"),
        "camera_fov": Param.number(40.0, min=0.1, max=170.0, decimals=2, unit="°", label="Field of view (horizontal)",
                                   group="Camera"),
        "camera_width": Param.integer(320, min=8, max=4096, unit="px", label="Width", group="Camera"),
        "camera_height": Param.integer(240, min=8, max=4096, unit="px", label="Height", group="Camera"),
        "camera_white": Param.number(99.5, min=50.0, max=100.0, decimals=1, unit="%", label="White at percentile",
                                     group="Camera",
                                     description="The brightest value of the grey scale: this share of the "
                                                 "pixels is not brighter (100: the brightest pixel)"),
        "preview_paths": Param.integer(200, min=1, max=20_000, label="Light paths drawn", group="Preview"),
    }

    def __init__(self):
        self.cache = pt_setup.MeshCache()

    # ------------------------------------------------------------------ assignments
    @staticmethod
    def _selected(ctx, kinds) -> list[dict]:
        found = [e for e in ctx.entities(selection=True) if (e.get("geometry") or {}).get("type") in kinds]
        if not found:
            raise UserError("Select " + (" or ".join(k.replace("_", " ") + "s" for k in kinds)) + " first.")
        return found

    @action("Selection: set material", description="Gives the selected surfaces and bodies the material named "
                                                    "in \"Material for the selection\"")
    def set_material(self, ctx):
        name = ctx.params["assign_material"].strip()
        if name not in [str(m.get("name", "")).strip() for m in ctx.params["materials"]]:
            raise UserError(f"The material {name!r} is not in the table of materials.")
        found = self._selected(ctx, _SURFACES)
        for e in found:
            ctx.set_attribute(e["id"], pt_setup.MATERIAL, name)
        return f"{len(found)} object(s): material {name}"

    @action("Selection: make it a source", description="Makes the selected surfaces, bodies and points emit as the "
                                                        "source named in \"Source for the selection\"")
    def set_source(self, ctx):
        name = ctx.params["assign_source"].strip()
        if name not in [str(s.get("name", "")).strip() for s in ctx.params["sources"]]:
            raise UserError(f"The source {name!r} is not in the table of sources.")
        found = self._selected(ctx, _SURFACES + ["point"])
        for e in found:
            ctx.set_attribute(e["id"], pt_setup.SOURCE, name)
        return f"{len(found)} object(s): source {name}"

    @action("Selection: no material, no source", description="Removes material and source from the selected objects")
    def clear(self, ctx):
        found = self._selected(ctx, _SURFACES + ["point"])
        for e in found:
            ctx.set_attribute(e["id"], pt_setup.MATERIAL, None)
            ctx.set_attribute(e["id"], pt_setup.SOURCE, None)
        return f"{len(found)} object(s) cleared"

    @action("Show materials and sources", description="A table of the objects with their material and source")
    def show(self, ctx):
        rows = pt_setup.assignments(ctx.entities(), ctx.params)
        if not rows:
            raise UserError("The model has no surfaces or bodies.")
        run = ctx.result_set("Path tracer: objects")
        ctx.table(run, "Materials and sources", ["Object", "Kind", "Material", "Source"], rows)
        return f"{len(rows)} objects, {sum(1 for r in rows if r[3])} of them sources"

    # ------------------------------------------------------------------ computation
    def _scene(self, ctx) -> pt_scene.Scene:
        ctx.progress(0.0, "preparing the model")
        try:
            scene = pt_setup.build_scene(ctx.entities(), ctx.params, self.cache)
        except ValueError as e:
            raise UserError(str(e)) from None
        mesh = scene.mesh
        ctx.log(f"{mesh.triangles} triangles on {len(mesh.face_area)} faces; {len(scene.src_flux)} source(s), "
                f"{scene.flux:.6g} W" + (f"; {len(scene.flipped)} face(s) turned outwards" if scene.flipped else ""))
        return scene

    def _render(self, ctx, scene, sensor, what: str) -> tuple[dict, str]:
        p = ctx.params
        method = p["method"]
        start = time.time()

        def progress(f: float) -> None:
            ctx.progress(0.02 + 0.96 * f, f"{what}, {method}: {int(round(f * p['samples'])):,} of {p['samples']:,}")

        out = pt_integrator.render(scene, sensor, pt_setup.METHODS[method], p["samples"], p["depth"], seed=p["seed"],
                                   progress=progress, cancelled=ctx.cancelled)
        seconds = max(time.time() - start, 1e-9)
        ctx.log(f"{what}, {method}: {out['samples']:,} samples, {out['rays']:,} rays in {seconds:.1f} s "
                f"({out['rays'] / seconds:,.0f} rays/s)")
        note = "" if out["samples"] >= p["samples"] else f" (stopped at {out['samples']:,} samples)"
        return out, note

    @action("Irradiance", description="Irradiance as a texture on the faces (W/m²), with the chosen method")
    def irradiance(self, ctx):
        p = ctx.params
        scene = self._scene(ctx)
        try:
            sensor = pt_scene.TexelSensor(scene, pt_setup.sensor_faces(scene, p["irradiance_on"]), p["texels"])
        except ValueError as e:
            raise UserError(str(e)) from None
        out, note = self._render(ctx, scene, sensor, "Irradiance")
        faces = pt_setup.irradiance(sensor, out["values"])
        names = {e["id"]: pt_setup.label(e) for e in ctx.entities()}
        top = max((float(np.nanmax(f["values"])) for f in faces if np.isfinite(f["values"]).any()), default=0.0)
        run = ctx.result_set(f"Irradiance – {p['method']}{note}")
        rows = []
        for f in faces:
            several = sum(1 for g in faces if g["entity"] == f["entity"]) > 1
            name = names.get(f["entity"], str(f["entity"])) + (f" · face {f['face'] + 1}" if several else "")
            finite = f["values"][np.isfinite(f["values"])]
            ctx.field(run, f"E {name}", f["entity"], f["values"], f["w"], f["h"], face=f["face"], unit="W/m²",
                      colormap="inferno", value_range=(0.0, top) if p["common_scale"] and top > 0 else None)
            rows.append([name, f["area"], f["flux"], f["flux"] / f["area"] * 1e6 if f["area"] > 0 else 0.0,
                         float(finite.max()) if finite.size else 0.0])
        ctx.table(run, "Irradiance per face", ["Face", "Area / mm²", "Flux / W", "Mean E / W/m²", "Max E / W/m²"], rows)
        return f"Irradiance on {len(faces)} face(s), {out['samples']:,} samples{note}"

    def _camera(self, ctx) -> pt_scene.Camera:
        p = ctx.params
        try:
            return pt_scene.Camera(p["camera_position"], p["camera_target"], pt_setup.UP[p["camera_up"]],
                                   p["camera_fov"], p["camera_width"], p["camera_height"])
        except ValueError as e:
            raise UserError(str(e)) from None

    @action("Camera image", description="The radiance seen by the camera (W/(m² sr)), with the chosen method")
    def camera_image(self, ctx):
        p = ctx.params
        camera = self._camera(ctx)
        scene = self._scene(ctx)
        out, note = self._render(ctx, scene, camera, "Camera image")
        values = out["values"] * 1e6
        positive = values[values > 0]
        white = float(np.percentile(positive, p["camera_white"])) if positive.size else 1.0
        run = ctx.result_set(f"Camera image – {p['method']}{note}")
        ctx.image(run, f"Radiance – {p['method']}", values, camera.w, camera.h, extent=(0, camera.w, 0, camera.h),
                  x_label="x / pixel", y_label="y / pixel", colormap="grey", unit="W/(m² sr)",
                  value_range=(0.0, white), description=f"{out['samples']:,} samples, paths up to {p['depth']} "
                                                        f"segments{note}")
        return f"Camera image {camera.w} × {camera.h}, {out['samples']:,} samples{note}"

    # ------------------------------------------------------------------ previews
    @action("Show camera", description="Draws the viewing pyramid of the camera")
    def show_camera(self, ctx):
        camera = self._camera(ctx)
        distance = float(np.linalg.norm(np.asarray(ctx.params["camera_target"]) - camera.position))
        c = camera.frustum(distance)
        apex = camera.position
        points = [apex, c[0], apex, c[1], apex, c[2], apex, c[3], c[0], c[1], c[2], c[3], c[0]]
        starts = [0, 2, 4, 6, 8, 13]
        run = ctx.result_set("Camera")
        ctx.lines(run, "Viewing pyramid", np.asarray(points), starts, np.zeros(len(points)), colormap="grey")
        return "Camera drawn"

    @action("Show light paths", description="Draws some paths of the light from the sources – to check where "
                                            "and into which directions they emit")
    def show_paths(self, ctx):
        p = ctx.params
        scene = self._scene(ctx)
        points, starts = pt_integrator.light_paths(scene, p["preview_paths"], p["depth"], p["seed"])
        if len(starts) < 2:
            raise UserError("No light path could be drawn.")
        order = np.zeros(len(points))
        for a, b in zip(starts[:-1], starts[1:]):
            order[a:b] = np.arange(b - a)
        run = ctx.result_set("Light paths")
        ctx.lines(run, "Light paths", points, starts, order, colormap="plasma", unit="bounce")
        return f"{len(starts) - 1} light paths"
