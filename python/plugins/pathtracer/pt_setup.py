"""From the CAD model and the panel to a scene, and from the estimates to results – without CAD:
entities are the dicts of `ctx.entities()`, params the values of the panel."""

from __future__ import annotations

import hashlib
import json

import numpy as np

import pt_bsdf
import pt_mesh
import pt_scene

MATERIAL = "pt.material"        # attribute of an object: name of a material of the panel
SOURCE = "pt.source"            # attribute of an object: name of a source of the panel
INACTIVE = "sim.inactive"       # attribute: the object takes no part (docs/plugins.md, D11)

METHODS = {"light tracing": "light", "path tracing": "path", "bidirectional": "bidir"}
UP = {"+Z": (0, 0, 1), "-Z": (0, 0, -1), "+Y": (0, 1, 0), "-Y": (0, -1, 0), "+X": (1, 0, 0), "-X": (-1, 0, 0)}

DEFAULT_MATERIALS = [
    {"name": "white", "kind": "diffuse", "reflectance": 0.8, "transmittance": 0.0, "roughness": 0.2, "ior": 1.5,
     "absorption": 0.0},
    {"name": "black", "kind": "absorber", "reflectance": 0.0, "transmittance": 0.0, "roughness": 0.2, "ior": 1.5,
     "absorption": 0.0},
    {"name": "mirror", "kind": "mirror", "reflectance": 0.95, "transmittance": 0.0, "roughness": 0.2, "ior": 1.5,
     "absorption": 0.0},
    {"name": "metal", "kind": "glossy", "reflectance": 0.85, "transmittance": 0.0, "roughness": 0.2, "ior": 1.5,
     "absorption": 0.0},
    {"name": "glass", "kind": "glass", "reflectance": 0.0, "transmittance": 0.0, "roughness": 0.2, "ior": 1.5,
     "absorption": 0.0},
    {"name": "diffuser", "kind": "diffuse", "reflectance": 0.3, "transmittance": 0.6, "roughness": 0.2, "ior": 1.5,
     "absorption": 0.0},
]
DEFAULT_SOURCES = [{"name": "lamp", "flux": 1.0, "half_angle": 90.0, "side": "front"}]


def label(entity: dict) -> str:
    return str(entity.get("name") or f"object {entity.get('id')}")


def _kind(entity: dict) -> str:
    return str((entity.get("geometry") or {}).get("type", ""))


def _attr(entity: dict, key: str) -> str:
    value = (entity.get("attributes") or {}).get(key)
    return "" if value is None else str(value).strip()


def active(entities: list[dict]) -> list[dict]:
    return [e for e in entities if not (e.get("attributes") or {}).get(INACTIVE)]


def surfaces(entities: list[dict]) -> list[dict]:
    """The objects that are traced: active surfaces, trimmed surfaces and shells."""
    return [e for e in active(entities) if _kind(e) in pt_mesh.SURFACE_KINDS]


def assignments(entities: list[dict], params: dict) -> list[list]:
    """Rows object, kind, material, source for the active objects that take part."""
    rows = []
    for e in active(entities):
        kind = _kind(e)
        if kind in pt_mesh.SURFACE_KINDS:
            rows.append([label(e), kind, _attr(e, MATERIAL) or f"({params['default_material']})", _attr(e, SOURCE)])
        elif kind == "point" and _attr(e, SOURCE):
            rows.append([label(e), kind, "", _attr(e, SOURCE)])
    return rows


class MeshCache:
    """Keeps the mesh of the last model: tessellation is repeated only when the geometry or the
    tolerance changed."""

    def __init__(self):
        self.key = None
        self.mesh = None

    def get(self, entities: list[dict], tol: float) -> pt_mesh.Mesh:
        digest = hashlib.sha1()
        digest.update(repr(float(tol)).encode())
        for e in entities:
            digest.update(json.dumps([e.get("id"), e.get("geometry")], sort_keys=True).encode())
        key = digest.hexdigest()
        if key != self.key:
            self.mesh = pt_mesh.build(entities, tol)
            self.key = key
        return self.mesh


def build_scene(entities: list[dict], params: dict, cache: MeshCache | None = None) -> pt_scene.Scene:
    """The scene of the model: materials and sources of the panel, assigned by the attributes
    pt.material and pt.source. ValueError with a message for the user when something is missing."""
    materials = pt_bsdf.Materials.from_rows(params["materials"])
    default = str(params.get("default_material", "")).strip()
    if default not in materials.names:
        raise ValueError(f"the default material {default!r} is not in the table of materials")
    surf = surfaces(entities)
    if not surf:
        raise ValueError("the model has no surfaces or bodies to trace")
    entity_material = []
    for e in surf:
        name = _attr(e, MATERIAL) or default
        if name not in materials.names:
            raise ValueError(f"{label(e)}: its material {name!r} is not in the table of materials")
        entity_material.append(materials.names.index(name))
    names = [str(row.get("name", "")).strip() for row in params["sources"]]
    if len(set(names)) != len(names) or "" in names:
        raise ValueError("sources need different, non-empty names")
    for e in active(entities):
        if _attr(e, SOURCE) and _attr(e, SOURCE) not in names:
            raise ValueError(f"{label(e)}: its source {_attr(e, SOURCE)!r} is not in the table of sources")
    specs = []
    for row, name in zip(params["sources"], names):
        on = tuple(k for k, e in enumerate(surf) if _attr(e, SOURCE) == name)
        points = tuple(tuple(e["geometry"]["point"]) for e in active(entities)
                       if _kind(e) == "point" and _attr(e, SOURCE) == name)
        specs.append(pt_scene.SourceSpec(name, float(row.get("flux", 0.0)), float(row.get("half_angle", 90.0)),
                                         str(row.get("side", "front")), on, points))
    if not any(s.entities or s.points for s in specs):
        raise ValueError("no object is a source: select a surface, a body or a point and choose "
                         "\"Selection: make it a source\"")
    tol = float(params.get("tolerance", 0.01))
    mesh = cache.get(surf, tol) if cache is not None else pt_mesh.build(surf, tol)
    closed = tuple(k for k, e in enumerate(surf) if _kind(e) == "shell")
    return pt_scene.Scene(mesh, materials, np.asarray(entity_material), specs, closed)


def sensor_faces(scene: pt_scene.Scene, ids) -> list[int]:
    """Faces of the mesh that belong to the objects `ids` (empty: all faces)."""
    mesh = scene.mesh
    if not ids:
        return list(range(len(mesh.face_area)))
    wanted = {int(i) for i in ids}
    faces = [f for f in range(len(mesh.face_area)) if mesh.entity_ids[mesh.face_entity[f]] in wanted]
    if not faces:
        raise ValueError("the objects chosen for the irradiance are not surfaces of the model (or inactive)")
    return faces


def irradiance(sensor: pt_scene.TexelSensor, flux: np.ndarray) -> list[dict]:
    """Per sensor face: entity (id), face (number within the entity), w, h, values (irradiance in
    W/m², row by row, NaN where the face has no or hardly any area in a texel), flux / W and
    area / mm²."""
    mesh = sensor.scene.mesh
    out = []
    for k, f in enumerate(sensor.faces):
        a, b = sensor.first[k], sensor.first[k + 1]
        area = sensor.texel_area[a:b]
        full = np.median(area[area > 0]) if (area > 0).any() else 0.0
        with np.errstate(divide="ignore", invalid="ignore"):
            values = np.where(area > 0.1 * full, flux[a:b] / area * 1e6, np.nan)
        out.append({"entity": mesh.entity_ids[mesh.face_entity[f]], "face": int(mesh.face_index[f]),
                    "w": int(sensor.w[k]), "h": int(sensor.h[k]), "values": values,
                    "flux": float(flux[a:b].sum()), "area": float(mesh.face_area[f])})
    return out
