"""Plugin base class and the @action decorator."""

from __future__ import annotations

from typing import Callable

API_VERSION = 1


def action(label: str, *, description: str = "", on_workflow_edit: bool = False) -> Callable:
    """Marks a method ``f(self, ctx)`` as an action shown in CAD (menu / tool column).

    ``on_workflow_edit``: CAD also runs it, quietly, shortly after the node workflow was edited
    (e.g. to draw what the edit changed); it should be quick and raise nothing."""

    def mark(f: Callable) -> Callable:
        f._nfcad_action = {"label": label, "description": description}
        if on_workflow_edit:
            f._nfcad_action["on_workflow_edit"] = True
        return f

    return mark


class Plugin:
    """Base class. Subclasses define actions with :func:`action`; ``params`` (a dict of
    :class:`nfcad.Param`, in display order) describes the parameters CAD shows in the plugin
    panel."""

    params: dict = {}

    def params_schema(self) -> list[dict]:
        return [p.to_json(name) for name, p in self.params.items()]

    def convert_params(self, values: dict) -> dict:
        """Values from CAD as Python types; missing ones get their defaults."""
        return {name: p.convert(values.get(name)) for name, p in self.params.items()}

    def actions(self) -> dict[str, dict]:
        out = {}
        for name in dir(type(self)):
            f = getattr(type(self), name, None)
            meta = getattr(f, "_nfcad_action", None)
            if meta is not None:
                out[name] = meta
        return out
