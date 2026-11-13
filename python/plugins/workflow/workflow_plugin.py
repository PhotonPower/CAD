"""Workflow engine of Raytatouille Fusion (ADR 0010, stage W1).

Checks, plans and runs the node workflow of the document (RTT-Fusion): the model's branches are
unrolled, only what the display blocks need is computed, results of earlier runs are kept
(R23), the blocks get their state (up to date / out of date) and every display block's view
becomes a result with the key "workflow:<block>". It knows no kernel: the blocks' data come with
the workflow (ADR "Bidirektionale Ports, Posen, Äste und Gruppen", Nachtrag 1) – a Raytatouille
system, for example, from the data of the `rtt.*` blocks and its glass catalogues from the
source's `rtt_catalogs`, written by the Raytracer plugin. Until stage W3 the blocks of all kernels
are evaluated in this process (RTT-Fusion's evaluators).
"""

from __future__ import annotations

import importlib
import math
import tempfile

from nfcad import Param, Plugin, UserError, action


def rings_for(rays: int) -> int:
    """Hexapolar rings r whose 1 + 3 r (r + 1) rays come closest to `rays` (at least 1 ring)."""
    r = (-3 + math.sqrt(max(9 + 12 * (rays - 1), 0))) / 6
    lo, hi = max(1, math.floor(r)), max(1, math.ceil(r))

    def count(k: int) -> int:
        return 1 + 3 * k * (k + 1)

    return lo if abs(count(lo) - rays) <= abs(count(hi) - rays) else hi


def _fusion():
    try:
        workflow = importlib.import_module("rtt_fusion.graph.workflow")
        fg = importlib.import_module("rtt_fusion.graph")
        execute = importlib.import_module("rtt_fusion.graph.execute")
        io = importlib.import_module("rtt_fusion.graph.io")
    except ImportError as e:
        raise UserError("The node workflow needs the Python package rtt_fusion (RTT-Fusion with the "
                        "module graph.workflow) in the plugin environment: pip install -e <RTT-Fusion>") from e
    return workflow, fg, execute, io


def _evaluators(extra: dict) -> dict:
    """RTT-Fusion's evaluators: the Raytatouille backend when raytatouille is installed, and the
    ones of the optional modules."""
    try:
        backend_rtt = importlib.import_module("rtt_fusion.graph.backend_rtt")
        base = dict(backend_rtt.EVALUATORS)
    except ImportError:
        base = {}
    return {**base, **extra}


class WorkflowEngine(Plugin):
    params = {
        "rays": Param.integer(20_000, min=7, max=500_000, label="Rays per field and wavelength",
                              description="Ray sources of the workflow (coherent image; every ray path is "
                                          "recorded, so memory grows with rays × events)"),
        "pixels": Param.integer(512, min=16, max=4096, label="Pixels (long side)"),
        "window": Param.number(0.0, min=0.0, label="Image window (half width, mm)",
                               description="0: each detector's own size. For a focus computed as a field "
                                           "(link into the camera set to H) a small window, e.g. 0.05 mm"),
        "gaussian_waist": Param.number(0.0, min=0.0, max=10.0, label="Gaussian illumination (w / pupil radius)",
                                       description="0: uniform (a hard-edged beam, whose Fresnel diffraction a "
                                                   "field link shows); e.g. 0.5 for a laser beam"),
    }

    def __init__(self):
        self._portdata = tempfile.TemporaryDirectory(prefix="nfcad-portdata-")  # remote blocks (D4)
        self._cache = None
        self._notes: list[str] = []
        self._model_ids: set[str] | None = None

    def _blocks(self, ctx, registry) -> tuple[dict, dict]:
        """The other plugins' block types in `registry` (ADR 0010, D2/D3): their evaluators (a
        request each) and the providers by type {"plugin", "version"}."""
        try:
            remote = importlib.import_module("rtt_fusion.graph.remote")
        except ImportError:
            return {}, {}
        try:  # no routing (an older CAD, a test host): no other plugins' blocks
            catalogues = ctx.block_catalogues() if hasattr(ctx, "block_catalogues") else []
        except RuntimeError as e:
            ctx.log(f"blocks of other plugins: {e}")
            catalogues = []
        evaluators = remote.register_remote(registry, catalogues, ctx.evaluate_block, self._portdata.name)
        providers = {str(e["type"]["type"]): {"plugin": str(e["provider"]), "version": str(e.get("version", ""))}
                     for e in catalogues}
        return evaluators, providers

    @staticmethod
    def _document(workflow, model, registry, wf, providers):
        try:  # with the provider records (ADR 0010, D7)
            return workflow.model_document(model, registry, None, workflow.kept_fields(wf), providers,
                                           wf.get("providers") or {})
        except TypeError:  # RTT-Fusion before W2c
            return workflow.model_document(model, registry, None, workflow.kept_fields(wf))

    # ------------------------------------------------------------------ check
    @action("Check workflow", description="Check the node workflow and mark problems; the plan per link")
    def check_workflow(self, ctx):
        workflow, fg, execute, io = _fusion()
        wf = ctx.workflow()
        if not wf.get("graph") and not wf.get("model"):
            raise UserError("No node workflow yet – a plugin makes one (Raytracer: Workflow from system, "
                            "New Raytatouille system).")
        registry, extra = workflow.registry()
        remote, providers = self._blocks(ctx, registry)
        extra = {**extra, **remote}
        graph = None
        if wf.get("model"):
            model = io.model_from_dict(wf["model"], registry)
            wf = {**wf, **self._document(workflow, model, registry, wf, providers)}
            if not any(d["severity"] == "error" for d in wf["diagnostics"]):
                graph = fg.unroll(model, registry)
            self._model_ids = set(model.nodes)
        else:  # an old socket graph (R16)
            graph = io.from_dict(wf["graph"])
            wf["types"] = workflow.catalogue(registry)
            wf["diagnostics"], wf["plan"] = workflow.checked(graph, registry)
            self._model_ids = None
        ctx.set_workflow(wf)
        if graph is not None and not any(d["severity"] == "error" for d in wf["diagnostics"]):
            try:  # which blocks the kept results still cover
                self._send_status(ctx, self._executor(ctx, graph, registry, _evaluators(extra), execute, wf))
            except Exception as e:  # noqa: BLE001 - only a hint
                ctx.log(f"workflow state: {e}")
        diagnostics = wf["diagnostics"]
        n_err = sum(1 for d in diagnostics if d["severity"] == "error")
        for d in diagnostics:
            ctx.log(f"{d['severity']}: {d['code']} – {d['message']} ({d['location']})")
        planned = sorted(set(v["representation"] for v in wf["plan"].get("links", {}).values()))
        return (f"workflow: {n_err} error(s), {len(diagnostics) - n_err} warning(s); "
                f"planned {''.join(planned) or '–'}, {len(wf['plan']['conversions'])} conversion(s)")

    # ------------------------------------------------------------------ run
    @action("Run workflow", description="Compute what the display blocks of the node workflow show: camera "
                                         "images, sweep curves, beam probes; links set to H as fields")
    def run_workflow(self, ctx):
        workflow, fg, execute, io = _fusion()
        wf = ctx.workflow()
        if not wf.get("graph") and not wf.get("model"):
            raise UserError("No node workflow yet – a plugin makes one (Raytracer: Workflow from system, "
                            "New Raytatouille system).")
        registry, extra = workflow.registry()
        remote, _ = self._blocks(ctx, registry)
        extra = {**extra, **remote}
        if wf.get("model"):  # RTT-Fusion R24: exactly the branches of the model, unrolled
            model = io.model_from_dict(wf["model"], registry)
            try:
                graph = fg.unroll(model, registry)
            except fg.ModelError as e:
                for d in e.diagnostics:
                    ctx.log(f"{d.severity}: {d.code} – {d.message} ({d.location})")
                raise UserError(f"The workflow has errors – Check workflow marks them: {e}") from e
            self._model_ids = set(model.nodes)
        else:
            graph = io.from_dict(wf["graph"])
            self._model_ids = None
        self._require_data(graph)
        displays = [n for n in graph.nodes.values() if n.type_id.startswith("display.")]
        if not displays:
            raise UserError("Nothing to show: attach a display block (right-click on the canvas → Add block → "
                            "analysis → display.image / display.plot / display.beam) to a camera, a phase "
                            "shifter or a component output, then run again. Only what the displays need is "
                            "computed.")
        window = float(ctx.params.get("window", 0.0) or 0.0)
        if window > 0:
            for n in graph.nodes.values():
                if n.type_id in ("rtt.detector", "rtt.part.detector"):  # also a detector of the library
                    n.params["half_size"] = [window, window]
        source = next((n for n in graph.nodes.values() if n.type_id == "rtt.source"), None)
        name = str(((source.params.get("rtt_system") or {}).get("name") if source is not None else "") or "")
        run = ctx.result_set(f"Workflow {name or 'run'}")
        self._notes = []
        made = self._run_displays(ctx, run, graph, registry, _evaluators(extra), execute, wf, displays)
        ctx.workflow_running("")
        message = "workflow: " + (", ".join(made) if made else "nothing computed")
        if self._notes:  # shown in the status line, not only in the log
            message += " – warning: " + "; ".join(self._notes)
        return message

    @staticmethod
    def _require_data(graph) -> None:
        """Raytatouille blocks compute from the system their data make (S2); a workflow from before
        the data were kept cannot run without the system file, which the engine does not know."""
        rtt = [n for n in graph.nodes.values()
               if n.type_id.startswith("rtt.") and n.type_id != "rtt.source" and not n.type_id.startswith("rtt.part.")]
        has_system = any(n.type_id == "rtt.source" and "rtt_system" in n.params for n in graph.nodes.values())
        if rtt and (not has_system or any("rtt" not in n.params for n in rtt)):
            raise UserError("This workflow holds no Raytatouille system data (made before they were kept): run "
                            "Workflow from system once in the Raytracer plugin, then Run workflow again.")

    @staticmethod
    def _materials(wf: dict):
        """Raytatouille's material library with the glass catalogues the Raytracer wrote into the
        source (`rtt_catalogs`); None without raytatouille."""
        try:
            rt = importlib.import_module("raytatouille")
        except ImportError:
            return None
        lib = rt.MaterialLibrary()
        for node in (wf.get("model") or wf.get("graph") or {}).get("nodes", []):
            if node.get("type") == "rtt.source":
                for path in node.get("params", {}).get("rtt_catalogs", []):
                    lib.add_catalog(str(path))
        return lib

    @staticmethod
    def _catalogues(wf: dict) -> list:
        for node in (wf.get("model") or wf.get("graph") or {}).get("nodes", []):
            if node.get("type") == "rtt.source":
                return list(node.get("params", {}).get("rtt_catalogs", []))
        return []

    def _hook(self, ctx, ex, a: float, b: float, workflow) -> None:
        """Progress of an executor (R21) into a part a … b of the job, the running block into the
        node editor; every report checks for Cancel."""
        last = {"node": None}

        def progress(part: float, text: str) -> None:
            ctx.check_cancel()
            ctx.progress(a + (b - a) * part, text)

        def on_node(node_id: str) -> None:
            node_id = workflow.model_id(node_id)
            if node_id != last["node"]:
                last["node"] = node_id
                ctx.workflow_running(node_id)

        if hasattr(ex.context, "progress"):
            ex.context.progress = progress
            ex.context.on_node = on_node

    def _run_displays(self, ctx, run, graph, registry, evaluators, execute, wf, displays) -> list:
        """Display blocks (R21) and everything in front of them, in one executor (shared blocks are
        computed once); their views as results with the key "workflow:<block>", shown in the
        block's window (double-click)."""
        workflow, _, _, _ = _fusion()
        made = []
        ex = self._executor(ctx, graph, registry, evaluators, execute, wf)
        for k, node in enumerate(displays):
            ctx.check_cancel()
            a, b = k / len(displays), (k + 1) / len(displays)
            self._hook(ctx, ex, a, b, workflow)
            ctx.progress(a, f"display: {node.name or node.id}")
            try:
                view = ex.evaluate(node.id, "view")
            except execute.ExecutionError as e:
                ctx.log(f"{node.name or node.id}: {e}")
                continue
            key = f"workflow:{node.id}"
            if view.kind == "image" and view.image is not None:
                ny, nx = view.image.values.shape
                note = str(getattr(view.image, "note", "") or "")  # RTT-Fusion W65: too few pixels
                # here the window is the engine's "Image window" (or more "Pixels")
                note = note.replace("set the detector's half size to", "set Image window (Workflow panel) to")
                note = note.replace("use more pixels", "raise Pixels (Workflow panel)")
                if note:
                    ctx.log(f"warning: {node.name or node.id}: {note}")
                    self._notes.append(f"{node.name or node.id}: {note}")
                elif float(view.image.values.max()) <= 0.0:
                    hint = ("set the link into the camera to H (right-click on the link → Transport → H) and "
                            "choose a small image window" if "H" in workflow.available() else
                            "install fieldtracing in the plugin environment, then set the link into the "
                            "camera to H")
                    ctx.log(f"{node.name or node.id}: dark – the light focuses into less than a pixel; {hint}")
                ctx.image(run, view.title, view.image.values.reshape(-1), nx, ny, extent=view.image.extent,
                          x_label=view.x_label, y_label=view.y_label, unit=view.unit,
                          description=f"{view.title}: {nx}×{ny} px", key=key)
            else:
                for c in view.curves:
                    ctx.curve(run, f"{view.title}: {c.name}", c.x, c.y, x_label=view.x_label,
                              y_label=view.y_label, key=key)
            made.append(f"{node.name or node.id}")
        for line in ex.context.log:
            ctx.log(line)
        cache = getattr(ex, "cache", None)
        for line in getattr(cache, "warnings", [])[-3:]:
            ctx.log(line)
        if cache is not None and hasattr(cache, "warnings"):
            cache.warnings.clear()
        self._send_status(ctx, ex)
        return made

    def _executor(self, ctx, graph, registry, evaluators, execute, wf):
        """An executor with the results of earlier runs (R23): only what changed is computed again.
        The environment covers what the results depend on outside the graph."""
        workflow, _, _, _ = _fusion()
        shared = {"rings": rings_for(int(ctx.params["rays"])),
                  "pixels": [int(ctx.params["pixels"])] * 2,
                  "gaussian_waist": float(ctx.params.get("gaussian_waist", 0.0) or 0.0)}
        materials = self._materials(wf)
        if materials is not None:
            shared["materials"] = materials
        available = workflow.available()
        if self._cache is None:
            self._cache = execute.ResultCache(limit_bytes=1 << 30)
        environment = {
            "rings": shared["rings"], "pixels": shared["pixels"], "window": ctx.params.get("window"),
            "gaussian": shared["gaussian_waist"], "catalogs": self._catalogues(wf), "available": sorted(available),
        }
        return execute.Executor(graph, registry, evaluators, shared, available=available, cache=self._cache,
                                environment=environment)

    def _send_status(self, ctx, ex) -> None:
        """Up to date / out of date per block (R23) for the node editor, per block of the model."""
        workflow, _, _, _ = _fusion()
        try:
            shown = [(n.id, "view") for n in ex.graph.nodes.values() if n.type_id.startswith("display.")]
            raw = {k: bool(v) for k, v in ex.status(wanted=shown).items()}
            ctx.workflow_state(workflow.block_states(raw, self._model_ids))
        except Exception as e:  # noqa: BLE001 - the state is a display hint only
            ctx.log(f"workflow state: {e}")
