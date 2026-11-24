# Markus — plan

What is left to do and in what order. Update it in the same change that finishes, starts or
reorders something on it. It holds no history and nothing about how anything works: what is
built is in the code and the other docs, and the editor's own list is
[editor_plan.md](editor_plan.md).

## Next up

Roughly in order. Each ends in a single pull in game, its questions decided before it.

### 0. The world through GoodRender's `world_quad` alone

GoodRender's contract version 2 gives `world_quad` and `world_billboard` a `hidden_behind`, and
the sink sends the whole world through `world_quad`, so a trail over the terrain is drawn by the
GPU rather than a flat draw list.

- **In [GoodRender](https://github.com/HasKha/gw2-good-render)**: the header, and the service in
  gw2-community-challenge-mode, which hands version 1 subscribers version 1's calls. Unpushed.
- **Here**: built against the local header, `GOOD_RENDER_LOCAL` on in
  `cmake/good_render.cmake`. Once the header is pushed, pin it there and turn the switch off.
  **Marker occlusion** in the options picks among the three.

Pull: does a trail running from the character past the camera stretch smoothly, as Pathing's
does? With each of Marker occlusion's three settings, does a marker behind a wall and one
behind a character hide as the setting says? Does Community Challenge Mode, built against
version 1, still draw its shapes?

### 1. The full map through GoodRender

The full map is not a window at `BEFORE_WINDOWS`: markers drawn there land over it. GoodRender
keeps its moments and its camera with the map up, and the world's markers are drawn under the
map ([viewer.md](viewer.md#drawing)); the sink draws the full map's at `BEFORE_WINDOWS`, over
the map and under the windows, as the minimap is. Whether the moments fire once the map is
fully open, with the camera's rise done, is not known: the frame may then have no world for
GoodRender to recognise, and the overlay draws the full map's markers instead, at Nexus's
render, over everything, the game's windows included. Nothing is drawn on a loading screen.

- **Fading the full map's markers in and out** with the map's own fade, about 600 ms, rather
  than all at once. The map's zoom out from the character as it opens is not known, so the
  markers sit where the map ends up, not where it is mid-zoom.

Pull: opening and closing the map, do the world's markers stay put under it as the camera
rises? With the full map open, do its markers draw over the map and under an open window? With
an older GoodRender, do they still draw, over everything? Through a loading screen, entered by a
waypoint on the map and by a portal, is nothing drawn?

### 2. Marker Packs in two tabs

Built: one row for every pack, the same in either tab; Installed shows those on disk, Library
the rest, under one filter. Only the pull is left.

Pull: does a pack dropped in by hand show under Installed? Downloading from Library, does it
stay there, with Delete, until the tab is switched, then show under Installed? Deleting one
under Installed, does it stay there with Download until then?

### 3. The repo

- **Screenshots in the README.**
- **CI.** A Windows job that configures and builds would catch a break from a clean clone,
  which is the one thing a local build cannot. The suite skips itself without a corpus, so
  it would run green and prove only that it links — worth having anyway.

## Known rough edges

- **Through the draw list, perspective-correct texturing is capped at 64 cuts an axis**
  (`kMaxQuadSubdivisions`). That is only without GoodRender's `world_quad`, and covers a trail
  step running from far ahead to beside the camera; a quad both enormous and right in the
  camera's face can still carry a little of the affine bend.
- **Baking is the largest part of loading.** Per marker it walks the category chain once per
  attribute; caching that per category is the next step if loading is still felt.

## Later

- Pathing's per-pixel fades — markers between the character and the camera, trails around the
  character — and their two settings. They need a shader of our own.
- **`occlude`**, which Hero's pack uses for invisible walls in the Aerodrome, the Cosmic
  Observatory and Arborstone. Blish erases what it drew behind them; a draw list over the game
  cannot erase, so over the terrain the markers behind those walls show through. Needs a way
  of its own — hiding what an occluder covers when the scene is built, say — before anything
  is written.
- **Pathing's own art**: its info window and gear icon are the game's textures, drawn with
  an alpha-mask shader; ours are plain ImGui windows. Its above/below icons are drawn as
  arrows.
- **Hiding the full map as the camera moves**, before MumbleLink says it closed (FlatMap's
  `MapTriggerHideFromCamera`): one frame's difference at most.
- **Inheritance across packs**, should a pack turn out to rely on it — see
  [viewer.md](viewer.md#packs-across-packs).
- Pathing's pack optimizer (TmfLib's PackWriter, which rewrites a downloaded pack), and a
  catalog other than the public one (Pathing's `PackRepoUrl` setting, which its UI does not
  offer).

## Open questions

- **Texture memory with every pack loaded.** Nexus never releases a texture, so a long session
  accumulates every icon it has seen.
