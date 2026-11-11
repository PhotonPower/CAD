"""nfcad – Python side of the Raytatouille Fusion plugin interface (docs/plugins.md, docs/plugin-api.md).

A plugin is a directory with ``plugin.json`` and a Python module that defines a subclass of
:class:`Plugin`. Raytatouille Fusion starts it in its own process (``python -m nfcad.host``) and calls
its actions; the actions talk to CAD through the :class:`Context` they receive.
"""

from .plugin import API_VERSION, Plugin, action
from .context import Cancelled, Context
from .params import Param

__all__ = ["API_VERSION", "Plugin", "action", "Context", "Cancelled", "Param"]
