"""Large arrays travel through memory-mapped temporary files (portable, no copies in JSON)."""

from __future__ import annotations

import array
import os
import tempfile
from typing import Any, Sequence

_DTYPES = {"f4": ("f", "float32"), "u4": ("I", "uint32")}


def write(values: Any, dtype: str) -> dict:
    """Writes a flat array (NumPy array or sequence) as little-endian raw data; returns the
    descriptor CAD reads (and deletes) – {"file", "dtype", "count"}."""
    code, np_name = _DTYPES[dtype]
    try:
        import numpy as np  # optional

        a = np.ascontiguousarray(np.asarray(values, dtype=np_name).ravel())
        if a.dtype.byteorder == ">":
            a = a.byteswap()
        data = a.tobytes()
        count = int(a.size)
    except ImportError:
        a = array.array(code, _flatten(values))
        if os.sys.byteorder != "little":
            a.byteswap()
        data = a.tobytes()
        count = len(a)
    # CAD gives every plugin process a private directory and accepts array files only from there
    fd, path = tempfile.mkstemp(prefix="nfcad-", suffix=".bin", dir=os.environ.get("NFCAD_ARRAY_DIR") or None)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
    return {"file": path, "dtype": dtype, "count": count}


def _flatten(values: Sequence) -> list:
    out = []
    for v in values:
        if isinstance(v, (list, tuple)):
            out.extend(_flatten(v))
        else:
            out.append(v)
    return out
