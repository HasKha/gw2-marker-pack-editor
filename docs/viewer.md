# The viewer

How the viewer is built, and why. The editor and the layout of the other libraries are
[architecture.md](architecture.md); what attributes mean is [format.md](format.md).

A Nexus-native replacement for Blish HUD's Pathing module: it loads every installed pack, draws the current
map's markers and trails into the game's frame through [GoodRender](https://github.com/HasKha/gw2-good-render),
and runs packs' behaviors and Lua scripts. **The compatibility target is Pathing**: a pack that works there
works here, and where Pathing and anything else disagree, Pathing wins. Other viewers' extensions are not a
goal.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Addon shape | one DLL, the editor on demand | one release; the editor draws through the viewer |
| Signature | the original `AddonDef.Signature` | an editor install updates in place through Nexus |
| GoodRender | optional: used when `served()`, a draw list otherwise | the addon works without the service |
| Camera | GoodRender's in a moment; outside one RTAPI's while it is loaded, MumbleLink's otherwise | each is the frame on screen where the next is a frame ahead |
| Frame split | everything decided at Present; a moment only draws | a moment runs inside the game's frame |
| Packs in memory | parsed once, up to four on workers; a map change switches what is live | faster than Pathing's reread per map |
| Category tree | merged across packs by namespace | Pathing's `SharedPackCollection` does it, and packs extending another's rely on it |
| Lua | 5.4 through sol2 | 5.4's integer subtype is what NeoLua imitates |
| Lua state | one for all packs, reset on every map change | Pathing's `ScriptEngine.Reset()` in `LoadMapFromEachPack` |
| Textures | Nexus's | no device code of our own |

## Layout

```
src/viewer/
  viewer.*       the Session, load to unload: the frame's order, the options page's sections,
                 EditorHooks; its members' order is its teardown
  settings.*     viewer.json: menu places, and Pathing's settings by its names
  store.*        category toggles, script Storage
  guid.*         Pathing's Guid: the pack's base64, or an MD5 of other text
  utc.*          the wall clock, and the date format timers and the catalog share
  desktop.*      the clipboard and the player's browser
  frame_log.*    the slow-frame log: a frame in which our own work took long, step by step, in Nexus's log
  packs/         the packs on disk and where they come from:
                 registry (discovery, enable/disable, loading on workers, the merged tree),
                 pack_folder (our folder kept in line with the installed packs: listing, reloading
                 what was written, downloads put in place, deletes), catalog (the public pack
                 catalog), downloads (its downloads), library (the catalog as a session uses it),
                 blish_import (what Blish HUD's Pathing has: packs, category choices, Storage, timers)
  live/          the packs made something to draw: bake (pack::Pack -> resolved attributes, grouped
                 by map id), runtime (the live map: visibility, fades, behavior state, markers
                 scripts made), edited (the editor's pack as a live map of its own, baked per revision)
  behaviors/     Pathing's Behavior/: attributes, update (per frame), entity, states (timers.txt),
                 cron (schedule), festivals
  script/        the Lua state and Pathing's API: engine, object (member tables, call-style shim,
                 census), values, world, globals, os_library, lson, console
  render/        the world drawn: scene (this frame's world quads, built from the live maps),
                 recycled (the last two scenes published, the older reused), ribbon, textures
                 (images read and decoded on the worker, made into Nexus textures a few MB a
                 Present), trail_colour, and the sinks: good_render (moments) and overlay (Nexus
                 render, draw list)
  maps/          map (where the world lands on the minimap and full map), map_scene (what the maps
                 show, drawn into any draw list), maps (the maps across frames: rectangles, fades,
                 zoom, compass, trail caches)
  game/          what the game and Nexus tell us: camera (RTAPI or MumbleLink -> view/projection),
                 rtapi (the RealTime API addon's data block, followed as Nexus loads and unloads
                 it), mumble (MumbleLink as Blish HUD names it, for scripts and behaviors),
                 identity (MumbleLink's identity JSON), game_binds (the game's Interact bind, from
                 Nexus's GameBinds.xml), interact_key (that bind, heard on the window thread)
  gw2_api/       the GW2 API, for achievement and raid filters: account, achievements, api_key
  ui/            main_menu, options, pack_catalog, import_window, script_console, interaction,
                 diagnostics (the options page's Development section), map_menu, fonts (Font Awesome merged into Nexus's font), achievement_tooltip,
                 style (the palette and shared widgets, the editor's too)
```

`viewer/` never includes `editor/`. Where it has to reach the editor (the Edit button, the overlays) it calls
an `EditorHooks` hook, which does nothing while the editor is not running.

## The frame

| Where | Runs |
|---|---|
| worker | parsing and baking packs, handed over in order; the catalog, downloads, festivals, maps; every file read or written after load, through `util::JobQueue` |
| `ERenderType_PreRender` | MumbleLink, map changes, behaviors, Lua `OnTick`, building and publishing the scenes |
| `ERenderType_Render` | our windows, the map's tooltip; the overlay sink for what no moment drew; the editor's overlay |
| GoodRender moments | the latest published scenes, submitted. Nothing else |

**The scene is world space and carries no camera.** It is built at one Present and drawn at the next frame's
moments with the camera `Moment` hands over, so markers do not lag a frame. Min/max size is resolved in the
sink for the same reason. Publishing is an atomic pointer swap.

## Drawing

| What | How | Moment |
|---|---|---|
| markers and trails | `world_quad`, in the scene's order, each with its `HiddenBehind` | `WORLD_FINISHED` |
| the same, from a moment without `world_quad` | over the terrain only, into `draw_list` through `render/overlay.*` | `WORLD_FINISHED` |
| minimap | `draw_list`, from MumbleLink's map fields as the moment reads them | `BEFORE_WINDOWS` |
| full map | the overlay, over everything, as the minimap is without the service | Nexus's render |
| info text, interact prompt, map tooltip and menu | ImGui windows | Nexus's render |

- **Everything goes through `world_quad`**, over the terrain too: a draw list spreads a texture evenly over
  pixels, so a trail running past the camera stretches; the GPU foreshortens it as Blish's does.
- **Billboards are not `world_billboard`.** Blish's faces the ray to its own centre and is clamped on its
  projected corners; building corners ourselves (`itemCorners` in `scene.*`) gives both sinks one shape.
- **The world goes under the game's whole UI**, where Blish HUD draws over it; only the minimap waits for
  `BEFORE_WINDOWS`, over the HUD as the game's map markers are. The full map cannot: the game draws it after
  that moment, and once it is fully up draws no world, so no moment fires at all.
- **The order is Blish's**: every trail, then markers back to front by where each stands before
  `heightOffset` (`buildScene`).
- **Occlusion is the player's Marker occlusion**: None (as Blish HUD), Behind Characters (default), or
  Behind Characters and Terrain; a pack's `occlusion` ([format.md](format.md#occlusion-ours)) wins over it.
  Settled per item when the scene is built (`SceneItem::occlusion`).
- **Fades are colour alpha, per quad**: `alpha`, `fadeNear`/`fadeFar`, combat, max opacity and the 800 ms
  fade-in multiply as in Pathing's `GetOpacity()`. Its shaders' per-pixel work is matched on average or not
  at all: a marker's second distance fade by its average (`shaderFade`); trails fade between `fadeNear` and
  `fadeFar` where Pathing's ignore `fadeNear`, a long step cut into pieces that fade alone (`cutStep`);
  fades near the character and camera not at all, which needs a shader of our own. Distance is measured from
  where a marker is drawn, `heightOffset` included.
- **What is wholly out of view is left out of the scene**, against the view widened for the frame or two
  until it is drawn (`ViewBounds` in `scene.cpp`); what is partly in view is drawn whole. A trail's ribbon is
  built once per live map and passed over a stretch at a time, by its fade and the view.
- **`cull`** keeps one face, decided in `itemCorners`; which is `Cull`'s comment in `bake.h`.
- **`occlude` is not drawn.** Blish erases what is behind it from its transparent overlay; a draw list cannot
  erase, so over the terrain markers behind those walls show through.
- **Without the service**, the scene goes through `render/overlay.*` at Nexus's render, over everything. Each
  moment flags what it drew and the render draws the rest (`good_render_sink::takeDrawn`).
- **Nothing is drawn on a loading screen**, where MumbleLink keeps the previous frame: both scenes are empty
  while Nexus's `IsGameplay` is off.
- **The world is drawn with the full map up**, under it. MumbleLink's `IsMapOpen` comes on at once and goes
  off late, so the scene ignores it; the overlay, which draws over the game's UI and so over the map, draws
  no world while it is set. The minimap goes on under the map too, on the compass as it was
  (`CompassKeeper`): MumbleLink's compass fields and `IsMapOpen` change on different frames, either way.
- **A marker without its icon is not drawn**, as in Pathing, including while its image is being read.
  The editor's overlay draws a dot there for the edited pack.
- **Picking comes from the scene**: the editor projects the edited pack's items through `itemCorners`, so what
  you click is what is drawn.

## Lua

Scripts are written against Pathing's NeoLua API, .NET objects exposed almost as they are.

- **The API is a table**: each member is one row with its getter, setter or function.
- **Both call styles work** (`World:CategoryByType(x)` and `World.CategoryByType(x)`; packs use both): one
  shim drops a leading argument that is the object itself.
- **.NET shows through**: `GameTime`, `Vector3`'s methods, `I:Vector3`, `I:Color` (byte channels, alpha 255),
  `I:Guid`, `I:Texture`, `I:Marker`. `I:Trail` is not built: Pathing's throws.
- **.NET's conversions**: nil where a value type is wanted is its default (Teh's Trails relies on it). Enums
  are Gw2Sharp's names as strings. Guids are interned userdata, and entity handles are cached, so equal
  lookups are one table key.
- **Positions are Blish HUD's**, (x, z, y) of the file's. A marker's `Position` excludes `heightOffset`.
- **Numbers**: Pathing uses Int32 and single floats; 5.4 uses 64-bit and doubles. Only overflow, precision
  and printing (`5` vs `5.0`) differ; not imitated unless a pack needs it.
- **The sandbox is `PathingGlobal.cs`**: no `pcall`, `error`, `load`, `io`, `debug` or `package`; `require`
  through the pack's files. NeoLua's extras are left out until a pack calls one. `os` keeps
  `LuaLibraryOS.cs`'s quirks. `table.ToLson`/`FromLson` are NeoLua's.
- **A script error never escapes**: every call is protected, errors go to the console with pack, file and
  line, and a call past `Host::call_budget` is stopped by a count hook. Each pack's frame time is shown.
- **`Storage`** is one store across packs, keyed `namespace\name`, in `kv.json`, written at most every five
  seconds.
- **The census**: a member a script reads that the table lacks is logged once and returns nil. Over the
  corpus, that log is what is left to implement.

## Behaviors

Pathing's `Behavior/`, as plain functions over the live map in `behaviors/`.

- **Read once, at the bake**, into a `Behaviors` or null; most markers are null. Pathing's parsing quirks are
  kept.
- **Run in Pathing's order** (`StandardMarker_Behaviors.cs`), since a later behavior's prompt replaces an
  earlier one's. A category that is off runs nothing.
- **The live pathable owns its state** (`behaviors::EntityState`). A script setting `InfoValue` or
  `BounceHeight` gives that marker its own copy of its behaviors.
- **A script may create markers mid-update**, which moves the live map: nothing is held by reference across a
  call into Lua.
- **`timers.txt`** is in Pathing's layout but our own file. A timer counts until it runs out, where Pathing's
  waits for its five-second sweep.
- **Festivals** come from the GW2 API's daily group, or Blish HUD's service when the API lists every
  festival, once per load. None is on until the answer.
- **`schedule` is a subset of Cronos**: no `L`, `W` or `#`, which no pack uses.
- **Achievement and raid filters** use an API key the player registers (`api_key.h`); `account::Fetcher`
  asks at load and every five minutes. Without a key, as in Pathing, the achievement filter hides nothing
  and the raid filter hides everything. `account.json` caches progress under a fingerprint of the key;
  `achievements.json` caches achievement details for good. "Clear cached API data" deletes both.

**Interaction** uses the game's Interact bind from `<game>/addons/Nexus/GameBinds.xml` (`game_binds.h`),
where Pathing can only use Blish HUD's own. Interact bound to nothing readable means the prompt is clicked;
F stands in only when the file does not name Interact. The key is passed on to the game, and ignored while
chat or our text fields have the keyboard. A hidden marker shown faded says why, and interacting unhides
it, removing its filters as Pathing does.

## Maps

Pathing's FlatMap and `RenderToMiniMap`, in `map.*` and `map_scene.*`.

- **Three spaces**: world metres; the continent, about two feet a unit, y down; the UI pixel, a screen pixel
  at the Normal interface size. Converted as Nexus's `Scaling` is, with the DPI factor the maps' "DPI scaling"
  option names (`settings::DpiScaling`), Nexus's own by default. The game's DPI Scaling option is its own and
  not read: Nexus's option of that name set otherwise puts markers on a compass of the wrong size, and the
  player picks System or None to match the game instead. Pathing's two corrections for Blish HUD's own
  pixels (MapScale × 0.897, 19–40 px on the compass) come out as UI pixels; sizes Pathing gives in its own
  pixels keep their 0.897.
- **Map rectangles** come from `/v2/maps?ids=all` once a session, cached in `maps.json` in Pathing's fallback
  layout. The Super Adventure Box's maps are skipped, as in Pathing's `ignore.yaml`.
- **The map scene carries no view** either: the moment reads MumbleLink's centre, scale and rotation itself.
- **Trails are laid on the continent once per live map**; a frame picks which show.
- **Clipped by hand**: a GoodRender draw list ignores clip rectangles.
- **The full map zooms as the game's does** while it opens and closes (`FullMapZoom`): MumbleLink only
  describes where it lands. The closing is told by GoodRender's moments coming back, long before `IsMapOpen`
  goes off; without the service the map stays still until then. The figures come from recordings, through
  `scripts/measure_map_zoom.py`: record the map opening and closing over a busy part of a map, and give it a
  frame of the settled map and what to leave out (addon windows, chat, the skill bar). With a pack whose
  markers sit on the game's own icons, `--markers` says how far ours run
  ahead of the game's picture, which is what the zoom's delays make up for. Measure again at a very
  different frame rate before trusting them there.
- **The right click reaches the game too**, as FlatMap does not block it.

## Packs

- **Our folder only**, `addons/<addon>/packs/`. Blish HUD's `markers/` is imported, not drawn from, or every
  marker shows twice while Blish HUD runs.
- **The import** (`blish_import.*`) is offered on the first load and from the options. It copies packs (one
  ours has as new or newer is kept), category choices, Storage and timers. A category choice is kept as
  Pathing made it, since the pack may not be loaded yet to say what its default is.
- **The catalog** is `https://mp-repo.blishhud.com/repo.json`, asked once at load. A `FileName` that is not a
  bare `.zip` or `.taco` name is left out.
- **A package's copy on disk** is the pack whose file name, less extension, is the package's; stale when the
  catalog's `LastUpdate` is later than the file's write time. Pathing matches any path *containing* the
  name, which confuses `TehsTrails-HeroPoints.taco` with Teh's Trails.
- **Downloads** go to `<file>.part` beside the pack, a thread each, with Pathing's user agent, and are opened
  as an archive before anything else. Putting one in place is the frame's job, and waits while the loader
  reads.
- **Keep updated** is on unless turned off (exceptions in `catalog.json`): once a session, stale packs are
  downloaded again.
- **Attributes are not merged across packs**: a marker inherits only from its own pack's declarations, where
  Pathing inherits from every pack's. No corpus pack relies on it; if one does, the bake has to move after
  the merge.

## The menu

Blish HUD's (`ui/main_menu.*`): **All Markers** and **Scripts** as `[ ] name >` rows, children opening on
hover, then Manage Packs, Options and Open Editor. All Markers' own box is stored under the empty id;
a bind, Toggle Markers, turns it on and off. **Smart categories** (default on) hide categories with nothing
on the current map (`runtime::nodesOnMap`) behind an "N Categories Are Hidden" row.

It is shown in Nexus's right-click menu, the menu of our icon in Nexus's bar (the default), or both. Nexus opens an icon's context
menu on right click and presses its keybind ("Open Markus Menu") on left click, so the left click opens a
popup of ours under the icon (`placeMenu`). Both are added and removed from the options page, which is safe
since Nexus locks its quick access only while drawing the bar.

## The editor inside

- **On demand**: Open Editor starts it; closing its main window ends it. Until then nothing of it runs.
- **Driven by the viewer**: `marker_editor::load` registers nothing with Nexus. The viewer's render and
  WndProc call it through `EditorHooks`, because Nexus holds its render mutex across every callback, so a
  render callback of the editor's own could only be registered at load.
- **Undo and redo are bound only while it runs**, so Ctrl+Z and Ctrl+Y are the game's otherwise. Bind handlers
  run on Nexus's thread, so they only raise a flag.
- **The viewer draws the open pack as an installed one**, with the player's options, through
  `EditorHooks::edited_live` and `edited.*`, with the tree's eye toggles and the editor's unsaved images and
  trails. A rebuild keeps fade-in state. Its scripts and behaviors do not run, so the author sees every
  marker that is on. An installed pack being edited shows twice: the saved copy and the edited one.
- **Save replaces a file the viewer has open**: archives are opened sharing deletion (`pack/zip.cpp`), and
  the viewer reloads a pack newer than what it read (`registry::writtenAt`, every two seconds).
- **A catalog pack opened in place is overwritten by its next update** unless Keep updated is turned off for
  it.
