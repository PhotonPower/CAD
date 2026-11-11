"""What an action can do in CAD: progress, log, model access, results."""

from __future__ import annotations

import threading
from typing import Any, Sequence

from . import arrays


class Cancelled(Exception):
    """Raised by :meth:`Context.check_cancel` when the user pressed Cancel."""


class Context:
    def __init__(self, host, job: int, params: dict, selection: list[int]):
        self._host = host
        self.job = job
        self.params = params
        self.selection = selection
        self._cancel = threading.Event()

    # ---- run control
    def progress(self, value: float, text: str = "") -> None:
        """Progress in [0, 1] with an optional text (status bar)."""
        self._host.send({"type": "progress", "job": self.job, "value": float(value), "text": text})

    def log(self, text: str) -> None:
        self._host.send({"type": "log", "job": self.job, "text": str(text)})

    def cancelled(self) -> bool:
        return self._cancel.is_set()

    def check_cancel(self) -> None:
        if self._cancel.is_set():
            raise Cancelled()

    # ---- model (geometry as in the .nfcad format, docs/file-format.md)
    def entities(self, ids: Sequence[int] | None = None, selection: bool = False) -> list[dict]:
        """Entities with id, name, group, attributes and geometry (dict)."""
        args: dict[str, Any] = {"selection": selection}
        if ids is not None:
            args["ids"] = [int(i) for i in ids]
        return self._host.request("model.entities", args)

    def add(self, geometry: dict, name: str = "", attributes: dict | None = None) -> int:
        return self._host.request(
            "model.add", {"geometry": geometry, "name": name, "attributes": attributes or {}}
        )

    def replace(self, entity: int, geometry: dict) -> None:
        """Replaces the geometry of an entity (live; the whole run is one undo step)."""
        self._host.request("model.replace", {"id": int(entity), "geometry": geometry})

    def set_attribute(self, entity: int, key: str, value: Any) -> None:
        self._host.request("model.set_attribute", {"id": int(entity), "key": key, "value": value})

    # ---- results (docs/plugins.md, section 3a)
    def result_set(self, name: str) -> int:
        return self._host.request("results.create_set", {"name": name})

    def lines(self, result_set: int, name: str, points, starts=None, values=None, *,
              colormap: str = "viridis", unit: str = "", on_top: bool = True,
              value_range: tuple[float, float] | None = None) -> int:
        """Polylines: points (n, 3) or flat; starts = first point of each polyline (+ total)."""
        args: dict[str, Any] = {"set": result_set, "name": name, "points": arrays.write(points, "f4"),
                                "colormap": colormap, "unit": unit, "on_top": on_top}
        if starts is not None:
            args["starts"] = arrays.write(starts, "u4")
        if values is not None:
            args["values"] = arrays.write(values, "f4")
        if value_range is not None:
            args["range"] = [float(value_range[0]), float(value_range[1])]
        return self._host.request("results.lines", args)

    def field(self, result_set: int, name: str, entity: int, values, width: int, height: int, *,
              face: int = 0, colormap: str = "inferno", unit: str = "",
              value_range: tuple[float, float] | None = None) -> int:
        """Scalar grid over the face's normalised (u, v): values[j * width + i] at (u_i, v_j)."""
        args: dict[str, Any] = {"set": result_set, "name": name, "entity": int(entity), "face": face,
                                "width": int(width), "height": int(height),
                                "values": arrays.write(values, "f4"), "colormap": colormap, "unit": unit}
        if value_range is not None:
            args["range"] = [float(value_range[0]), float(value_range[1])]
        return self._host.request("results.field", args)

    def curve(self, result_set: int, name: str, x=(), y=(), *, x_label: str = "", y_label: str = "") -> int:
        return self._host.request("results.curve", {"set": result_set, "name": name,
                                                    "x": [float(v) for v in x], "y": [float(v) for v in y],
                                                    "x_label": x_label, "y_label": y_label})

    def append(self, curve: int, x: float, y: float) -> None:
        self._host.send({"type": "notify", "method": "results.append",
                         "args": {"item": int(curve), "x": float(x), "y": float(y)}})
