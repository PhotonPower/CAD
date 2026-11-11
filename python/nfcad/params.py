"""Parameter descriptions; CAD shows them as a form in the plugin panel (docs/plugin-api.md).

    class Raytracer(Plugin):
        params = {
            "rays": Param.integer(10_000, min=1, label="Number of rays"),
            "wavelengths": Param.numbers([486.1, 587.6, 656.3], unit="nm", group="Source"),
            "detector": Param.entity(kinds=["surface", "trimmed_surface", "shell"]),
        }

In an action, ``ctx.params["rays"]`` is the value the user set (or the default).
"""

from __future__ import annotations

from typing import Any, Sequence


class Param:
    def __init__(self, type_: str, default: Any, **fields: Any):
        self.type = type_
        self.default = default
        self.fields = {k: v for k, v in fields.items() if v is not None}

    def to_json(self, name: str) -> dict:
        d = {"name": name, "type": self.type, "default": self.default}
        d.update(self.fields)
        d.setdefault("label", name.replace("_", " ").capitalize())
        return d

    def convert(self, value: Any) -> Any:
        """The value CAD sent, as the Python type of this parameter."""
        if value is None:
            value = self.default  # defaults get the same Python types as values from CAD
        if self.type == "number":
            return float(value)
        if self.type == "integer":
            return int(value)
        if self.type == "boolean":
            return bool(value)
        if self.type == "numbers":
            return [float(v) for v in value]
        if self.type == "files":
            if isinstance(value, str):  # a ';'-separated text (older documents)
                value = value.split(";")
            return [str(v).strip() for v in value if str(v).strip()]
        if self.type == "point":
            return tuple(float(v) for v in value)
        if self.type == "entity":
            ids = [int(v) for v in value]
            return ids if self.fields.get("multiple") else (ids[0] if ids else None)
        return value

    # ---- builders
    @staticmethod
    def number(default: float = 0.0, *, min: float | None = None, max: float | None = None,
               step: float | None = None, decimals: int | None = None, unit: str = "",
               label: str | None = None, group: str | None = None, description: str | None = None) -> "Param":
        return Param("number", float(default), min=min, max=max, step=step, decimals=decimals, unit=unit,
                     label=label, group=group, description=description)

    @staticmethod
    def integer(default: int = 0, *, min: int | None = None, max: int | None = None, unit: str = "",
                label: str | None = None, group: str | None = None, description: str | None = None) -> "Param":
        return Param("integer", int(default), min=min, max=max, unit=unit, label=label, group=group,
                     description=description)

    @staticmethod
    def boolean(default: bool = False, *, label: str | None = None, group: str | None = None,
                description: str | None = None) -> "Param":
        return Param("boolean", bool(default), label=label, group=group, description=description)

    @staticmethod
    def choice(options: Sequence[str], default: str | None = None, *, label: str | None = None,
               group: str | None = None, description: str | None = None) -> "Param":
        options = [str(o) for o in options]
        return Param("choice", default if default is not None else options[0], options=options, label=label,
                     group=group, description=description)

    @staticmethod
    def text(default: str = "", *, label: str | None = None, group: str | None = None,
             description: str | None = None) -> "Param":
        return Param("text", str(default), label=label, group=group, description=description)

    @staticmethod
    def file(default: str = "", *, filter: str = "", save: bool = False, label: str | None = None,
             group: str | None = None, description: str | None = None) -> "Param":
        return Param("file", str(default), filter=filter, save=save, label=label, group=group,
                     description=description)

    @staticmethod
    def files(default: Sequence[str] = (), *, filter: str = "", label: str | None = None,
              group: str | None = None, description: str | None = None) -> "Param":
        """Several files (e.g. glass catalogues): a list with Add… / Remove in the panel."""
        return Param("files", [str(f) for f in default], filter=filter, label=label, group=group,
                     description=description)

    @staticmethod
    def numbers(default: Sequence[float] = (), *, unit: str = "", label: str | None = None,
                group: str | None = None, description: str | None = None) -> "Param":
        return Param("numbers", [float(v) for v in default], unit=unit, label=label, group=group,
                     description=description)

    @staticmethod
    def point(default: Sequence[float] = (0.0, 0.0, 0.0), *, label: str | None = None,
              group: str | None = None, description: str | None = None) -> "Param":
        return Param("point", [float(v) for v in default], label=label, group=group, description=description)

    @staticmethod
    def entity(*, kinds: Sequence[str] | None = None, multiple: bool = False, label: str | None = None,
               group: str | None = None, description: str | None = None) -> "Param":
        """Objects of the model, taken from the selection in CAD. kinds: point, curve, surface,
        trimmed_surface, shell (empty: any)."""
        return Param("entity", [], kinds=list(kinds) if kinds else None, multiple=multiple, label=label,
                     group=group, description=description)
