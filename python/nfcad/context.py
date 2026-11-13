"""What an action can do in CAD: progress, log, model access, results."""

from __future__ import annotations

import threading
from typing import Any, Sequence

from . import arrays


class Cancelled(Exception):
    """Raised by :meth:`Context.check_cancel` when the user pressed Cancel."""


class UserError(Exception):
    """A problem the user can fix (missing input, wrong file …). CAD shows the message as it is –
    no traceback, the plugin log stays closed. Other exceptions are reported as plugin errors."""


class Context:
    def __init__(self, host, job: int, params: dict, selection: list[int], target: dict | None = None):
        self._host = host
        self.job = job
        self.params = params
        self.selection = selection
        #: the block an action with ``block_types`` was chosen on: {"block": id, "scope": [...]}
        self.target = target or {}
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

    def add(self, geometry: dict, name: str = "", attributes: dict | None = None, group: int = 0) -> int:
        """New entity (in the model tree group ``group``, 0 = top level); returns its id."""
        return self._host.request(
            "model.add",
            {"geometry": geometry, "name": name, "attributes": attributes or {}, "group": int(group)},
        )

    def remove(self, entity: int) -> None:
        """Removes an entity (undone with the run)."""
        self._host.request("model.remove", {"id": int(entity)})

    def groups(self) -> list[dict]:
        """Model tree groups: id, name, parent (0 = top level)."""
        return self._host.request("model.groups", {})

    def add_group(self, name: str, parent: int = 0) -> int:
        return self._host.request("model.add_group", {"name": name, "parent": int(parent)})

    def remove_group(self, group: int) -> None:
        """Removes an empty group (no entities, no sub-groups)."""
        self._host.request("model.remove_group", {"id": int(group)})

    def replace(self, entity: int, geometry: dict) -> None:
        """Replaces the geometry of an entity (live; the whole run is one undo step)."""
        self._host.request("model.replace", {"id": int(entity), "geometry": geometry})

    def set_attribute(self, entity: int, key: str, value: Any) -> None:
        self._host.request("model.set_attribute", {"id": int(entity), "key": key, "value": value})

    # ---- node workflow (RTT-Fusion R16): the graph shown in the node editor
    def workflow(self) -> dict:
        """{"graph": rtt-fusion-graph, "types": [...], "diagnostics": [...]} of the document (may
        be empty); the node editor writes the user's edits into "graph"."""
        return self._host.request("workflow.get", {}) or {}

    def workflow_running(self, node: str) -> None:
        """Marks the block of the node workflow that is computed now ("" : none) in the editor."""
        self._host.send({"type": "notify", "method": "workflow.status", "args": {"node": str(node)}})

    def workflow_state(self, states: dict) -> None:
        """Up to date (True) or out of date (False) per block id – a green tick or a red cross in the
        node editor (results kept across runs)."""
        self._host.send({"type": "notify", "method": "workflow.state",
                         "args": {"states": {str(k): bool(v) for k, v in states.items()}}})

    def block_catalogues(self) -> list:
        """The block types the other plugins provide (ADR 0010): {"provider", "version", "type"}
        per type; for the workflow engine."""
        return list(self._host.request("workflow.blocks", {}) or [])

    def evaluate_block(self, provider: str, request: dict) -> dict:
        """Has the plugin `provider` evaluate one of its blocks (ADR 0010, D3) and returns its
        answer; CAD starts the plugin if needed. For the workflow engine."""
        return dict(self._host.request("workflow.evaluate", {"provider": provider, "request": request}) or {})

    def set_workflow(self, workflow: dict) -> None:
        """Replaces the document's node workflow (shown in the node editor)."""
        self._host.request("workflow.set", {"workflow": workflow})

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

    def image(self, result_set: int, name: str, values, width: int, height: int, *,
              extent: tuple[float, float, float, float] | None = None, x_label: str = "x / mm",
              y_label: str = "y / mm", colormap: str = "inferno", unit: str = "",
              value_range: tuple[float, float] | None = None, description: str = "", key: str = "") -> int:
        """A 2D image in the *Image* dock (e.g. a camera image): values[j * width + i], row 0 at
        y0; extent = (x0, x1, y0, y1) of the grid (cell edges), e.g. in mm."""
        args: dict[str, Any] = {"set": result_set, "name": name, "width": int(width), "height": int(height),
                                "values": arrays.write(values, "f4"), "x_label": x_label, "y_label": y_label,
                                "colormap": colormap, "unit": unit, "description": description}
        if extent is not None:
            args["extent"] = [float(v) for v in extent]
        if value_range is not None:
            args["range"] = [float(value_range[0]), float(value_range[1])]
        if key:
            args["key"] = key
        return self._host.request("results.image", args)

    def curve(self, result_set: int, name: str, x=(), y=(), *, x_label: str = "", y_label: str = "",
              key: str = "") -> int:
        """A curve in the *Plot* dock; `key` ties it to a display window (e.g. "workflow:<block>")."""
        args = {"set": result_set, "name": name, "x": [float(v) for v in x], "y": [float(v) for v in y],
                "x_label": x_label, "y_label": y_label}
        if key:
            args["key"] = key
        return self._host.request("results.curve", args)

    def table(self, result_set: int, name: str, columns, rows, *, key: str = "") -> int:
        """A table in the *Table* dock (e.g. the diffraction orders): column heads and rows; cells
        as text – numbers are written with up to 10 significant digits (format them yourself
        for other precision)."""
        def text(v: Any) -> str:
            return f"{v:.10g}" if isinstance(v, float) else str(v)

        args: dict[str, Any] = {"set": result_set, "name": name, "columns": [str(c) for c in columns],
                                "rows": [[text(v) for v in row] for row in rows]}
        if key:
            args["key"] = key
        return self._host.request("results.table", args)

    def bars(self, result_set: int, name: str, labels, values, *, y_label: str = "", key: str = "") -> int:
        """A bar chart in the *Plot* dock (e.g. the efficiency per order): one bar per label."""
        args: dict[str, Any] = {"set": result_set, "name": name, "labels": [str(v) for v in labels],
                                "values": [float(v) for v in values], "y_label": y_label}
        if key:
            args["key"] = key
        return self._host.request("results.bars", args)

    def append(self, curve: int, x: float, y: float) -> None:
        self._host.send({"type": "notify", "method": "results.append",
                         "args": {"item": int(curve), "x": float(x), "y": float(y)}})
