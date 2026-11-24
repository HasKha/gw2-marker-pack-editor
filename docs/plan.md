# Markus — plan

What is left and in what order. No history, nothing about how anything works; the editor's own list is
[editor_plan.md](editor_plan.md).

## Next up

Roughly in order. Each ends in a single pull in game, its questions decided before it.

### 0. The world through GoodRender's `world_quad` alone

Built against GoodRender's contract version 2, which gives `world_quad` a `hidden_behind`. Only the pull is
left.

Pull: does a trail running from the character past the camera stretch smoothly, as Pathing's does? With each
of Marker occlusion's three settings, does a marker behind a wall and one behind a character hide as the
setting says?

### 1. The full map through GoodRender

The full map is not a window at `BEFORE_WINDOWS`: markers drawn there land over it. The world's markers are
drawn under the map ([viewer.md](viewer.md#drawing)); the sink draws the full map's at `BEFORE_WINDOWS`, over
the map and under the windows. Whether the moments fire once the map is fully open, with the camera's rise
done, is not known: if not, the overlay draws the full map's markers at Nexus's render, over everything.

- **Fade the full map's markers** with the map's own fade, about 600 ms. The map's zoom as it opens is not
  known, so the markers sit where the map ends up.

Pull: opening and closing the map, do the world's markers stay put under it as the camera rises? With the
full map open, do its markers draw over the map and under an open window? With an older GoodRender, do they
still draw, over everything? Through a loading screen, entered by a waypoint and by a portal, is nothing
drawn?

### 2. Marker Packs in two tabs

Built: one row for every pack, the same in either tab; Installed shows those on disk, Library the rest. Only
the pull is left.

Pull: does a pack dropped in by hand show under Installed? Downloading from Library, does it stay there, with
Delete, until the tab is switched, then show under Installed? Deleting one under Installed, does it stay there
with Download until then?

### 3. The repo

- **Screenshots in the README.**
- **CI**: a Windows job that configures and builds, to catch a break from a clean clone. Without a corpus the
  suite skips itself, so it proves only that it links.

## Known rough edges

- **Through the draw list, perspective-correct texturing is capped** at `kMaxQuadSubdivisions` cuts an axis;
  only without `world_quad`.
- **Baking is the largest part of loading.** Per marker it walks the category chain once per attribute;
  caching per category is the next step if loading is felt.

## Later

- Pathing's per-pixel fades (markers between the character and camera, trails around the character) and their
  two settings. They need a shader of our own.
- **`occlude`**, which Hero's pack uses for invisible walls in the Aerodrome, the Cosmic Observatory and
  Arborstone. Needs a way of its own, such as hiding what an occluder covers when the scene is built.
- **Pathing's own art**: its info window and gear icon are game textures through an alpha-mask shader; ours
  are ImGui windows. Its above/below icons are arrows.
- **Hiding the full map as the camera moves**, before MumbleLink says it closed (FlatMap's
  `MapTriggerHideFromCamera`): one frame at most.
- **Inheritance across packs**, if a pack relies on it ([viewer.md](viewer.md#packs)).
- Pathing's pack optimizer (TmfLib's `PackWriter`), and a catalog other than the public one (Pathing's
  `PackRepoUrl`).

## Open questions

- **Texture memory with every pack loaded.** Nexus never releases a texture, so a long session accumulates
  every icon it has seen.
