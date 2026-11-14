# Automotive Lighting plugin

The reflector of a **clear-lens headlamp**: for a filament lamp (H1, H3, H4, H7, H8, H9, H11, HB3,
HB4 or your own data) the mirror is facetted so that the low beam gets its **cut-off line** from the
mirror alone, and the facets are joined to one surface that can be moulded. Needs only NumPy in
the plugin environment.

Coordinates: **+X** is the driving direction (the light leaves along +X), **+Y** left, **+Z** up; the
centre of the filament lies at *Filament centre*. On the measuring screen H is positive to the right
and V upwards.

## Panel

- *Lamp*: the filament as a cylinder (length, diameter) and its luminous flux; nominal flux of
  UN/ECE R37, typical coil dimensions – check them against the lamp's sheet, or enter them under
  *Custom*. H4: the low-beam coil above its shield (light only into the upper 195°).
- *Cut-off*: right- or left-hand traffic, the height of the horizontal part (−0.57° = 1 %), the
  rise (15°) and the position of the kink.
- *Frame*: the outline of the mirror seen along X – closed curves drawn in CAD (*Frame (curves)*,
  joined end to end) or a rounded rectangle – and the radius of the lamp hole.
- *Reflector*: the focal length of the base paraboloid, the strip width and the **zones**.
- *Connecting faces*: on / off, the draft angle of the walls between the facets and the fillet
  radius at their foot and head.
- *Analysis*: reflectance, the lattice of mirror points, the number of filament images.

**Zones.** A zone is a sector about the lamp axis (azimuth 0° = left, 90° = up, 180° = right, seen
in the driving direction; each row reaches to the next row's start). Its facets use one branch of
the cut-off – *horizontal* or *rising* – and place their filament images along it: the image of
the filament centre at *aim* (H), running from the strip's right border to its left one (the
spread of each strip). *below cut-off* keeps the images of a zone that far under the line.
*Default zones* fills the table for the chosen traffic side: the flank whose images lie along the
rising branch carries it, the other flank forms the horizontal cut-off and the hot spot, top and
bottom spread the light.

## Actions

- **Generate reflector**: the mirror as trimmed NURBS faces – every facet (bicubic B-spline,
  trimmed with its cell) and every connecting face – and the filament as a cylinder, in the
  model-tree group *Reflector &lt;lamp&gt;*; a new run replaces the last reflector. The faces carry
  the attribute `al.part` (`facet`, `connection`, `filament`). The log gives the area, the depth,
  how close the filament images lie to the cut-off, the highest step, the smallest draft angle
  and the share of the mirror the connecting faces take.
- **Filament images**: the outlines of the filament images of points all over the facets on the
  screen, with the cut-off line (*Image* window).
- **Light distribution**: the luminous intensity (cd; 625 cd = 1 lx at 25 m) as an image, the test
  points and zones of UN/ECE R112 (class B, passing beam) as a table, a vertical cut through the
  cut-off at 2.5° and a horizontal cut at V = −0.86° as curves; flux (also what the connecting
  faces send into the beam – stray light), the sharpness G of the cut-off and the highest
  intensity above it in the log.
- **Show strips**: the start curve and some characteristic strips of every facet as lines.
- **Default zones**: the zone table for the chosen traffic side.

## The method

A mirror point P with unit normal n images the filament point Q into the direction
d = R_n (P − Q)/|P − Q| (R_n: the reflection). The **filament image** of P – all these directions –
lies below a branch of the cut-off, the plane m · d = 0 through the lamp, and touches it exactly
when the cut-off plane through P, *mirrored at the facet*, touches the filament:

    F(P, n) = m' · (C − P) − σ(m') = 0,        m' = R_n m

(C: the filament centre, σ: the support function of the filament cylinder). For a surface this is
a first-order partial differential equation. Its **characteristic strips** – curves on the mirror
together with the normals along them – solve the ordinary differential equations

    dP/ds = −2 [(g · n) m_t + (m · n) g_t],        dn/ds = m_t,

with g = Q* − P towards the filament point that touches and m_t, g_t the tangential parts of m and
g. Along such a curve the mirror may tilt about the curve's tangent without lifting the image off
the cut-off; the image only slides along the line. The curve lies in a plane normal to the
direction of the cut-off at the image – a vertical section for the horizontal branch aimed
straight ahead, a tilted one for the rising branch, and a curved strip on the mirror in general.

A **facet** is the family of characteristic strips through a start curve across the strip. On the
start curve the normal is fully prescribed – the image touches the cut-off and its centre lies at
the aim – which gives an ordinary differential equation for that section; its middle lies on the
base paraboloid. So on the whole facet every filament image touches the cut-off, to the accuracy of
the B-spline (reported, typically below 0.05°); the aim holds on the start curve and drifts with
the distance from it. The start curve lies at the facet's deepest section, so that strips whose
aims diverge (right border aimed right) fan out towards the rim; aims that converge too strongly
make the strips cross, which is reported.

The **strip borders** follow the characteristics of a facet aimed at the middle of the zone's
aims: sections of the base paraboloid with parallel planes, which are straight lines in the front
view for a zone aimed straight ahead and circles otherwise.

Where the long edge of the filament image is parallel to the cut-off, the end of the coil that
touches changes; the surface is smooth there but its curvature jumps (the strips are integrated
with the change located inside the step).

## Connecting faces

Neighbouring facets meet with a step that changes along their border (each facet starts on the
base paraboloid, then follows its own differential equation). So that the mirror is one surface
that can be injection-moulded – demoulding along X – the facets are joined by **walls with a draft
angle**, **rounded** at their foot and head. Across a border (ξ, in the front view) the mirror is
the raw profile G – one facet for ξ < 0, the other for ξ > 0 – turned into

    F1 = median(L, w, U)      the wall w through the middle of the step, with the slope
                              cot(draft), kept between the envelopes L ≤ G ≤ U of that slope;
    F2 = open(close(F1))      a ball of the fillet radius rolled over both sides.

Where the step vanishes the wall does too, and only the crease between the facets is rounded.
The profiles of all stations along a border make a **band**, a bicubic B-spline that runs out
into the facets on both sides; the facets are trimmed back to it. There are two levels: the bands
between the strips of a zone, then the bands along the rays between two zones – their two sides
are the zones as built so far, strips and bands – so the junctions are covered too. All bands
between zones have one width.

What to expect: the walls keep the draft angle (a little less only where two walls meet at a
junction – the smallest angle is logged); bands and facets join within about 0.01 mm; the bands
take 10 – 15 % of the mirror with the default settings and send their light elsewhere – the
light distribution shows it as stray light above the cut-off. Lower steps mean narrower walls:
narrower strips, a smaller spread per strip, or neighbouring zones with similar aims.

## Limits of this version

- Photometry: the coil as an evenly shining cylinder, one reflection at the facets and the
  connecting faces, nothing shades (a wall does not shadow the facet behind it), no light leaving
  the lamp directly, no bulb glass, no cover lens. The cut-off is sharper than a real lamp's.
- The lamp hole must be large enough for the bands between the zones, which meet at the lamp
  axis; the strips must be wider than their connecting faces (both are reported).
- The frame should be convex or nearly so; the lamp axis is +X; the faces are separate trimmed
  surfaces (not sewn to a shell).
