# Markus — plan

What is left to do and in what order. Update it in the same change that finishes, starts or
reorders something on it. It holds no history and nothing about how anything works: what is
built is in the code and the other docs, and the editor's own list is
[editor_plan.md](editor_plan.md).

## Next up

Roughly in order. Each ends in a single pull in game, its questions decided before it.

### 1. The full map through GoodRender

The full map is not a window at `BEFORE_WINDOWS`: markers drawn there land over it. But
GoodRender stops calling its moments while the full map is open, so the overlay sink draws the
full map's markers at Nexus's render instead, over everything, the game's windows included.

- **In [GoodRender](https://github.com/HasKha/gw2-good-render)**: keep calling the moments
  while the full map is open. A change in that repo, and a release of it.
- **Here**: bump `cmake/good_render.cmake` to it. The sink then draws the full map at
  `BEFORE_WINDOWS`, over the map and under the windows, as the minimap is; the overlay goes on
  drawing it whenever no moment did, an older GoodRender included. The world scene is still
  skipped while the map is open.

Pull: with the full map open, do its markers draw over the map and under an open window? With
an older GoodRender, do they still draw, over everything?

### 2. Marker Packs in two tabs

The Marker Packs window becomes two tabs:

- **Installed**: the packs in our folder, wherever they came from — the catalog, the Blish HUD
  import, or dropped in by hand — each with Delete, and Info, Update and Keep updated for
  those the catalog has.
- **Library**: the public catalog, filtered by name, author or category, each with Download.

Pull: does a pack dropped in by hand show under Installed? Downloading from Library, does it
move to Installed when it arrives?

### 3. The repo

- **Screenshots in the README.**
- **CI.** A Windows job that configures and builds would catch a break from a clean clone,
  which is the one thing a local build cannot. The suite skips itself without a corpus, so
  it would run green and prove only that it links — worth having anyway.

## Known rough edges

- **Perspective-correct texturing is capped at 16 subdivisions a side.** That covers every
  marker anyone would author; a quad both enormous and right in the camera's face still
  carries a little of the affine bend near the corner closest to you. Raising the cap is a
  constant in `viewer/camera.h`, paid for in vertices.
- **Quads are clipped against the near plane only, not the sides of the frustum.** A marker
  you are standing inside can project a corner tens of thousands of pixels off screen, and
  those cells are subdivided and emitted like any other before ImGui's scissor throws them
  away. Correct, just wasteful; side planes would be four more of the same clip.
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
