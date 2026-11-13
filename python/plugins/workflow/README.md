# Workflow (engine of the node workflow)

The engine of Raytatouille Fusion's node workflow ([ADR 0010](../../../docs/adr/0010-multi-plugin-workflow.md),
stage W1): **Check workflow** checks the model, plans the representation per link and marks the
blocks; **Run workflow** computes what the display blocks show (camera images, sweep curves, beam
probes; links set to H as fields), keeps earlier results and turns every display's view into a
result with the key `workflow:<block>`.

Blocks of other plugins (ADR 0010, W2): the engine asks CAD for the other plugins' block catalogues,
adds those types to its library and has such a block evaluated in its plugin's process (inputs and
outputs as RTT-Fusion port data); the workflow records the provider of every such type it uses.

The panel holds the image settings: rays per field and wavelength, pixels, image window (0: each
detector's own size) and a Gaussian illumination. The engine knows no kernel – the blocks' data
come with the workflow (a Raytatouille system from the data of the `rtt.*` blocks, its glass
catalogues from the source's `rtt_catalogs`, written by the Raytracer plugin). Until stage W3 the
blocks of all kernels are evaluated in this plugin's process, so its Python environment needs
`rtt_fusion` and, for Raytatouille blocks, `raytatouille` (the same environment as the Raytracer).

A workflow made before the blocks held their data cannot run here: run *Workflow from system* once
in the Raytracer plugin.
