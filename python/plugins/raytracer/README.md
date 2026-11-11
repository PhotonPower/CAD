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
| **Trace rays** | rays along the paths (*Paths*: names separated by commas, empty = every path of the system; *Field*, *Wavelengths*, *Pupil rings*), one line set per path in *Results* (show / hide each), coloured by wavelength, field or path; with *Irradiance on the image* the irradiance on the last surface of each path as a field on its face – paths ending on the same surface add up (incoherently, no interference) (*Irradiance grid*, *Pupil rings (irradiance)*), computed by Raytatouille (`rt.analysis.irradiance_map`, #379: incident weight per projected cell area). Rays that are not counted (outside the face, vignetted, not reached) are listed in the plugin log. |
| **Optimise** | Raytatouille's optimiser on the system's variables and merit function; the changed surfaces are redrawn after every accepted iteration, the merit function grows in the *Plot* dock. Cancel (Esc) ends with the last accepted state. |
| **Save system** | writes the current system (*Save as*, default `<system>.optimised.rtt.json`). |

Glass catalogues: the files added under *Glass catalogues* (*Add…*, several at once), plus all
*.agf files next to the system file and in its sub-folder `catalogs`. Without catalogues only systems with
constant indices compile, such as `examples/singlet_optim.rtt.json`.

**Undo.** Every action is one undo step in CAD. The plugin numbers each state of the optical
system and stores the number in the attribute `rtt.revision` of the first surface; after an
undo of an optimisation the next action continues with the system before it.

**Drawing accuracy.** Rotationally symmetric surfaces (conic, aspheres, axicon, Q-type) with a
circular aperture are drawn as surfaces of revolution: the profile is interpolated along its
arc length, so steep, nearly hemispherical lenses are drawn as exactly as flat ones. Other
shapes, rectangular / elliptical apertures and image surfaces are bicubic B-splines over the
local x-y plane, trimmed with exact curves. The *Drawing tolerance* (default 0.1 µm) is
checked; planes are exact. Rays are always traced on the exact surfaces. The error per
surface is in the attribute `rtt.nurbs_error_mm`; surfaces above the tolerance are named in
the plugin log.

**Folded and tilted paths** (beam splitters, interferometers such as a Linnik): Raytatouille's
ray aiming needs a rotationally symmetric path. For other paths the plugin starts the rays
itself – on the same hexapolar grid over the stop, along the field angles (object at infinity)
or from the object points (finite object). That is exact when the aperture stop is the first
surface of the path (as with an illumination stop); otherwise the plugin says that it cannot
aim into the path.

## Not yet

- Switching elements off for the computation (attribute `sim.inactive`, needs
  [#380](https://github.com/PhotonPower/Raytatouille/issues/380)); the plugin only logs them.
- Editing optical parameters in CAD (radii, thicknesses) and the conversion to
  `raytatouille.nurbs` in the library (D10).
