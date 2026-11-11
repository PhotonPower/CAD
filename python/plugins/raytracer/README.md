# Raytracer plugin

Raytatouille ([PhotonPower/Raytatouille](https://github.com/PhotonPower/Raytatouille)) inside
Raytatouille Fusion – milestone P6 of the [plugin plan](../../../docs/plugins.md).

## Setup

1. Install Raytatouille (with `rt.analysis.irradiance_map`, i.e. #379 – `main` from 2026-11-11 on)
   into the Python environment of the plugins (*Plugins → Settings…*), e.g.
   the repository's `.venv`:
   ```bash
   MACOSX_DEPLOYMENT_TARGET=26.0 .venv/bin/pip install ../Raytatouille   # macOS; elsewhere without the variable
   ```
2. The plugin is not signed yet: switch on the **developer mode** (*Plugins → Settings…*). In
   a development build it is found automatically (`python/plugins`).

## Use (dock *Plugin*, plugin *Raytracer*)

| Action | What happens |
|---|---|
| **Load system** | reads the `.rtt.json` (*System file*), compiles it with the glass catalogues and draws every surface as a NURBS face – lenses with their rims, the aperture stop as a ring, the image surface sized to the spot. The model tree follows the rtt node tree. Loading again replaces the drawing. |
| **Trace rays** | rays along the path (*Path*, *Field*, *Wavelengths*, *Pupil rings*) as a line set in *Results*, coloured by wavelength or field; with *Irradiance on the image* the irradiance on the last surface of the path as a field on its face (*Irradiance grid*, *Pupil rings (irradiance)*), computed by Raytatouille (`rt.analysis.irradiance_map`, #379: incident weight per projected cell area). Rays that are not counted (outside the face, vignetted, not reached) are listed in the plugin log. |
| **Optimise** | Raytatouille's optimiser on the system's variables and merit function; the changed surfaces are redrawn after every accepted iteration, the merit function grows in the *Plot* dock. Cancel (Esc) ends with the last accepted state. |
| **Save system** | writes the current system (*Save as*, default `<system>.optimised.rtt.json`). |

Glass catalogues: *.agf files next to the system file and in its sub-folder `catalogs`, plus
the files in *Glass catalogues* (separated by `;`). Without catalogues only systems with
constant indices compile, such as `examples/singlet_optim.rtt.json`.

**Undo.** Every action is one undo step in CAD. The plugin numbers each state of the optical
system and stores the number in the attribute `rtt.revision` of the first surface; after an
undo of an optimisation the next action continues with the system before it.

**Drawing accuracy.** Surfaces are drawn as bicubic B-splines that interpolate the exact sag
(*Drawing tolerance*, default 0.1 µm; planes exact) and trimmed with exact circles / ellipses.
Rays are always traced on the exact surfaces. Non-smooth shapes (the tip of an axicon) are
drawn with a larger error; the plugin log names them, and the error per surface is in the
attribute `rtt.nurbs_error_mm`.

## Not yet

- Switching elements off for the computation (attribute `sim.inactive`, needs
  [#380](https://github.com/PhotonPower/Raytatouille/issues/380)); the plugin only logs them.
- Editing optical parameters in CAD (radii, thicknesses) and the conversion to
  `raytatouille.nurbs` in the library (D10).
