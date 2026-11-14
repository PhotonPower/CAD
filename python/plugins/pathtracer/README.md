# Path Tracer plugin

Light transport on the CAD model in Raytatouille Fusion: some objects are **light sources**, the
others get **materials**; the plugin computes the **irradiance as a texture** on the faces and –
with a camera – an **image**. Three methods estimate the same quantities and can be compared:
light tracing, path tracing and bidirectional path tracing. Needs only `numpy` in the plugin
environment.

Lengths are mm (the document's unit), power is W. One channel: radiometric, no colour and no
wavelengths yet.

## Steps

1. **Materials** (table in the panel). Select surfaces or bodies, write a material's name into
   *Material for the selection* and choose *Selection: set material*. Objects without one get the
   *Material of the other objects*.

   | Kind | Uses | Meaning |
   |---|---|---|
   | `absorber` | – | takes everything (a detector that does not reflect) |
   | `diffuse` | reflectance, transmittance | Lambertian reflection; with a transmittance also diffuse transmission (a diffuser sheet) |
   | `mirror` | reflectance | ideal mirror |
   | `glossy` | reflectance, roughness | a lobe around the mirror direction (normalised Phong, exponent 2/roughness² − 2) |
   | `glass` | index, absorption | smooth dielectric: Fresnel reflection and refraction, absorption inside (1/mm); the body must be closed and stand in air |

2. **Sources** (table in the panel: flux in W, half-angle, side). Select surfaces, bodies or
   points, write a source's name into *Source for the selection* and choose *Selection: make it a
   source*.
   - Surfaces emit with constant radiance into a cone around their normal – half-angle 90° is a
     Lambertian emitter, a small angle a beam. All surfaces of one source share its flux by area.
     *Side*: the front is the side of the surface normal; for closed bodies the outside.
   - Points emit evenly into all directions; the points of one source share its flux.
   - A source also has its material (it reflects light like any other object).

   *Show materials and sources* lists the objects; *Show light paths* draws some paths from the
   sources (colour: number of the bounce) – a check of where and into which direction they emit.

3. **Irradiance.** *Irradiance on* (use the selection; empty: every surface), *Texels (longer
   side)*, then *Irradiance*: every face gets a texture with the irradiance in W/m² – the light
   arriving from both sides –, and the *Table* window lists area, flux, mean and maximum per face.
   Texels in which a trimmed face has hardly any area stay empty.

4. **Camera.** Position, *Looks at* (both with *Pick*), up direction, horizontal field of view and
   pixels; *Show camera* draws the viewing pyramid. *Camera image* shows the radiance in
   W/(m² sr) in the *Image* window (a pinhole camera; *White at percentile* sets the grey scale).
   Point sources themselves are not visible, the light they cast is.

*Cancel* stops a computation after the current batch and shows what it has so far.
`sim.inactive` objects take no part.

## Methods

A light path runs from a source over any number of surfaces to the sensor (a texel of a face, or
the camera). *Longest path* limits its segments (1: direct light only).

| Method | How | Good for | Weak at |
|---|---|---|---|
| **light tracing** | particles start at the sources and are followed; they leave their flux in the texels they land in, and every point they reach is connected to the camera | irradiance maps, light through lenses and off mirrors (caustics) | camera images of mirrors and glass (they stay black: a pinhole is never hit by chance) |
| **path tracing** | paths start at the sensor (points on the faces, or the camera's pixels); every point is connected to a point on a source, and paths that hit a source count too | camera images, large diffuse sources | small or narrow sources behind mirrors or glass; point sources seen through them are not found at all |
| **bidirectional** | both kinds of paths, every point of one connected to every point of the other; each way of making a path is weighted by how well it samples that path (multiple importance sampling) | scenes where neither of the others is good everywhere | costs most per sample |

The three agree within their noise wherever a method can produce every path (tests:
`tests/python/test_pathtracer.py`, with closed-form cases – point source over a plane, a closed
diffuse box, a glass plate).

## How it works

- **Geometry.** Every face is tessellated in its surface parameters to the *Mesh tolerance*;
  trimming is a mask over (u, v) (512² cells per face; fewer with very many faces), tested at
  every hit. Normals are interpolated over the triangles. Closed bodies are oriented by counting
  crossings, so that their front is the outside.
- **Tracing.** A bounding volume hierarchy and a traversal that advances all rays of a batch
  together (NumPy). Expect roughly 10⁵ … 10⁶ rays per second.
- **Modules** (no CAD in them, usable from scripts): `pt_nurbs` (NURBS evaluation), `pt_mesh`
  (tessellation, masks, areas), `pt_bvh` (intersection), `pt_bsdf` (materials), `pt_scene`
  (sources, sensors), `pt_integrator` (the estimator), `pt_setup` (model and panel → scene).

## Not yet

Colour and wavelengths (dispersion), measured materials and rough glass, glass touching glass,
detectors that light passes through unchanged, sources with a measured distribution, a lens
camera with depth of field, the exact NURBS surface at a hit (instead of the triangle), a
compiled intersection kernel, keeping results with the document.
