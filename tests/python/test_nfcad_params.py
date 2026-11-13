"""nfcad's structured parameters (ADR 0010, W4a): schema of list / table / json and the values an
action gets (converted per field, nested lists too)."""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

from nfcad import Param  # noqa: E402

lamella = {"x": Param.number(0.0, unit="µm"), "material": Param.text("air")}
layer = {
    "kind": Param.choice(["homogeneous", "lamellar"], "homogeneous"),
    "thickness": Param.number(0.1, unit="µm"),
    "lamellae": Param.table(lamella),
}
layers = Param.list(layer, item_label="kind")
raw = Param.json({"a": 1})

schema = layers.to_json("layers")
assert schema["type"] == "list" and schema["item_label"] == "kind" and schema["default"] == []
assert [f["name"] for f in schema["of"]] == ["kind", "thickness", "lamellae"]
nested = schema["of"][2]
assert nested["type"] == "table" and [c["name"] for c in nested["columns"]] == ["x", "material"]
assert nested["columns"][0]["unit"] == "µm"
json.dumps(schema)  # plain JSON for the hello

# values from CAD: numbers may come as ints, missing fields get the defaults
value = layers.convert([{"kind": "lamellar", "thickness": 1, "lamellae": [{"x": 0, "material": "glass"}, {}]}, {}])
assert value == [
    {"kind": "lamellar", "thickness": 1.0, "lamellae": [{"x": 0.0, "material": "glass"}, {"x": 0.0, "material": "air"}]},
    {"kind": "homogeneous", "thickness": 0.1, "lamellae": []},
]
assert isinstance(value[0]["thickness"], float) and isinstance(value[0]["lamellae"][0]["x"], float)
assert layers.convert(None) == []  # the default

copy = raw.convert({"a": [1, 2]})
copy["a"].append(3)  # a copy: the action may change it
assert raw.convert(None) == {"a": 1}

try:
    Param.table({"where": Param.point()})
except ValueError as e:
    assert "number, integer, text or choice" in str(e)
else:
    raise AssertionError("a point column must be refused")
print("ok")

# a hidden parameter (a plugin's bookkeeping): in the schema with "hidden", not shown, value kept
hidden = Param.json([], hidden=True).to_json("_order")
assert hidden["hidden"] is True and hidden["type"] == "json", hidden
assert "hidden" not in Param.json([]).to_json("x")
