# Editor — what is left

None of it is scheduled; the editor works end to end. The addon's roadmap is [plan.md](plan.md); how the
editor is built is [architecture.md](architecture.md), whose rules anything new keeps.

## To build

- Reorder a trail's points.
- A rename field in the trail editor, calling `pathable_edit::attributeRenamed`; today a trail is renamed
  through `traildata` in Details.
- More shape tools beside the circle, a straight line first. `gw2-markers/scripts/shared.py` is the spec.
- Enum value lists from the gw2pathing.com attribute pages, so `ValueKind::Enum` gets a dropdown.
- A validation pass in the UI from `markus_probe`'s lint. The property panel already flags unknown attributes
  and ones that do nothing where they sit.
- `fadecenter`, Pathing's alias for `canFade`, with a `deprecated` flag to keep it out of the "add attribute"
  picker.
- The edited pack's info text and interact prompt, and a toggle to run its scripts while editing.
- Picking a trail ribbon in the viewport: a distance test against the projected polyline. Today clicking a
  ribbon counts as a miss (`deselectInViewport`), so clicking another trail's ribbon deselects.
- Moving a whole trail with the gizmo; it skips trails and moves points one at a time.
- Showing `.trl` files in the resources window, if wanted: `kImageExtensions`.

## If it becomes a problem

- **Pack loading is on the render thread.** 56k markers has not stuttered.
- **A gizmo drag bakes the whole pack every frame.** If it hitches, narrow the bake to the current map; the
  undo records already name what each edit touched.
- **A second window needing `update()`**: drive the calls from the `editor/ui/windows.*` table rather than by
  hand. Not a base class: the draw signatures differ.

## Untested

- Undo and the editor's indexes.
- `saveToDirectory`'s asset half: the folder source is a real checkout. Copying it to a temp folder first
  would make it testable.

## To check in game

- A group rotation stamps `rotate` onto billboards, making them fixed quads.
- Add POI stores the raw character position, so an inherited `heightOffset` lifts it off that spot.
