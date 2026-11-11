# Commands and keyboard input (M5)

Commands are started from the tool column, or by typing their name (or alias) – typing while a
view has the focus goes straight to the command line. The prompt in the command line says what
the command expects; the rubber band (orange) previews the result. Every result is one undo step.

| Command | Alias | Input |
|---|---|---|
| Point | `pt` | locations, until Enter / right-click |
| Line | `l` | start, end (or a length) |
| Polyline | `pl` | points; Enter: open polyline, clicking the start point: closed |
| Rectangle | `rec` | corner, opposite corner (edges along the construction-plane axes) |
| Polygon | `pol` | (number of sides,) centre, corner point or circumradius |
| Circle | `c` | centre, radius point or radius |
| Circle (3 points) | `c3` | three points on the circle |
| Arc | `a` | start, end, point on the arc |
| Helix | `h` | (number of turns,) axis start, axis end, radius point or radius |
| Distance | `dist` | two points; the result appears in the status bar |

## Keyboard input

| Typed | Meaning |
|---|---|
| `x,y,z` | absolute world coordinates |
| `x,y` | coordinates in the construction plane of the view (Front: x, z; Left: −y, z; Top / Perspective: x, y) |
| `@dx,dy,dz` / `@dx,dy` | relative to the last point (world / construction plane) |
| `d<a` / `@d<a` | polar in the construction plane (angle in degrees from its u axis), absolute / relative |
| number | what the prompt asks for (radius, sides, turns); otherwise a length from the last point towards the cursor |
| name | starts a command |
| Enter on an empty line, right-click | finishes the command, or repeats the last one |
| Esc | cancels |

Decimal point `.`; coordinates are separated by `,`.

## Modifiers while picking points

- Object snaps as in [snapping.md](snapping.md): F3 on/off, Tab cycles, Alt suppresses.
- **Ortho (F8)**: without a snap, the point is moved onto the construction-plane axis through the
  last point that is closest to the cursor direction. Snapped points are not changed.

## Architecture

`commands/` (no widgets): `Command` state machines and the `parse_input` parser – unit tested
without a window. `app/CommandController` connects them to the viewports (clicked and snapped
points, rubber band), the command line, ortho and the undo stack. Commands build geometry
with the Nurbforge constructors (`make_line`, `make_circle`, `make_arc_3pt`, `make_polygon`,
`make_helix`, …).

Not yet: user-defined construction planes (each view has the plane of its standard view),
options inside commands (e.g. arc by centre), surface and solid commands (M6).
