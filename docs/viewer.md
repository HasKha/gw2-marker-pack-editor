# The viewer

How the viewer half of the addon is built, and why. Most of it is still planned; what exists
so far, and the order the rest is built in, is [plan.md](plan.md#the-viewer); the editor half is
[architecture.md](architecture.md); what attributes mean is [format.md](format.md).

## What it is

A Nexus-native replacement for Blish HUD's Pathing module. It loads every installed pack,
draws the current map's markers and trails into the game's own frame through
[GoodRender](https://github.com/HasKha/gw2-good-render) — under the game's windows, and
hidden behind walls and characters — and runs packs' behaviors and Lua scripts the way
Pathing does.

**The compatibility target is Pathing.** A pack that works in Pathing should work here, and
where Pathing and anything else disagree, Pathing wins: the same precedence `format.md`
already uses for what attributes do. Other viewers' own extensions (Taquito's `screenSticky`
family, say) are not a goal.

Pathing cannot simply be pointed at GoodRender. It is a module of Blish HUD, which is a
process of its own drawing on its own D3D device, and GoodRender calls a native function
from inside the game's render thread. Bridging the two would mean rebuilding Pathing's
renderer natively anyway, while keeping Blish HUD and a fork of an 18k-line module.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Addon shape | **one DLL**: the viewer always, the editor behind a setting | one release stream; "Edit" is a function call, not a protocol; the editor draws through the viewer's renderer |
| Signature | the existing `AddonDef.Signature` stays | an editor install updates in place through Nexus; there is never a second addon drawing the same packs |
| Separation | three static libraries, linked one way | see [Layout](#layout); the build enforces what a convention would only ask |
| GoodRender | **optional**: used when `served()`, a draw-list fallback otherwise | the addon still works without the service; the fallback is the quad path the editor's renderer was built on |
| Frame split | everything is decided at Present; a moment callback only draws | the moment runs inside the game's frame, so anything slow there stalls the game |
| Packs in memory | parsed once, a few packs at a time on workers of their own; a map change only switches which part is live | faster than Pathing's reread per map, and 55k markers baked is a few megabytes |
| Category tree | **merged across packs** by namespace | Pathing's `SharedPackCollection` does it, and packs that extend another pack's categories rely on it |
| Lua | Lua 5.4 through sol2 | 5.4's integer subtype is what NeoLua imitates; sol2 is header-only and fetched like the rest |
| Lua state | **one for all packs, reset on every map change** | what Pathing does (`ScriptEngine.Reset()` at the top of `LoadMapFromEachPack`), and scripts are written against it |
| Textures | Nexus's, as the editor does today | already written and in-game tested; revisit if memory with every pack loaded says otherwise |

## Layout

What `src/` becomes; [plan.md](plan.md#the-viewer) says which parts exist.
[architecture.md](architecture.md#module-layout)'s layout goes on describing `editor/`.

```
src/
  entry.cpp        AddonDefinition; loads the viewer, and the editor when the setting is on
  pack/            mpe_pack — unchanged. The format, nothing else
  viewer/          mpe_viewer, links mpe_pack
    viewer.*       lifecycle, the addon's name and folder, its options page, EditorHooks: start, render, WndProc
    settings.*     viewer.json: the menu's places, and Pathing's settings by its names
    json_file.*    lenient reads and writes of small JSON files, the editor's too
    registry.*     installed packs: discovery, enable/disable, loading on workers, the merged tree
    blish_import.* what Blish HUD's Pathing has — packs, category choices, Storage, timers
    bake.*         pack::Pack -> the runtime model: resolved attributes, grouped by map id
    runtime.*      the live map: visibility, fade state, behaviors' state, markers scripts made
    edited.*       a pack model that changes as a live map of its own, baked per revision: the editor's
    behaviors/     Pathing's Behavior/: attributes.* (what a pathable's behavior attributes say,
                   baked once), update.* (filters, focus, interaction, per frame), entity.* (what
                   one live pathable remembers), states.* (what `behavior` hid, timers.txt),
                   cron.* (`schedule`), festivals.* (which festivals are on)
    script/        the Lua state and the Pathing API: engine.* (the state, loading, ticking,
                   script-* calls), object.* (the member tables, the call-style shim, the
                   census), values.cpp, world.cpp and globals.cpp (the API itself),
                   os_library.cpp, lson.*, console.*
    guid.*         Pathing's Guid: from the pack's base64, or an MD5 of any other text
    identity.*     MumbleLink's identity JSON, read once per change
    scene.*        this frame's world quads and billboards, built from the live maps
    map.*          where the world lands on the minimap and the full map: the maps' rectangles
                   from the API and maps.json, the compass's bounds, the projection
    map_scene.*    what the maps show, built from runtime; drawn into any draw list
    render/        the sinks: good_render.* (moments) and overlay.* (Nexus render, draw list)
    background.h   one job on a thread of its own, its answer read from frames
    camera.*       MumbleLink -> view/projection
    mumble.*       MumbleLink as Blish HUD names it, for scripts and behaviors; Gw2Sharp's enums
    http.*         HTTPS GETs on a worker, through WinHTTP: festivals, maps, then the catalog
    textures.*     budgeted texture decoding: LoadBudget, TextureCache
    catalog.*      the public pack catalog: its list, packs on disk, which are stale, Keep updated
    downloads.*    catalog packs downloading into our folder, a thread each
    utc.*          the wall clock, and the date format the timers and the catalog share
    store.*        what outlives a session: category toggles, script Storage
    ui/            main_menu.* (the menu: All Markers, Scripts, Marker Packs, the editor) and
                   icon_data.h (its icon, made by tools/make_icons.py), fonts.* (Nexus's font
                   with Font Awesome merged, font_awesome_solid.* made by tools/convert_fonts.py),
                   the script console,
                   interaction.* (info and prompt),
                   map_menu.* (the maps' tooltip and menu), options.* (Pathing's settings),
                   pack_catalog.* (Marker Packs, the catalog's window), import_window.*
                   (the Blish HUD import)
  editor/          mpe_editor, links mpe_pack and mpe_viewer
```

**The dependency only points one way.** `editor/` includes `viewer/`; `viewer/` never
includes `editor/`, and two things hold it: `mpe_viewer_test` links `mpe_viewer` without
`mpe_editor`, and the ctest case `viewer_includes_no_editor` fails on any `editor/` include
under `src/viewer/`. Where the viewer has to reach the editor — the Edit button, the editor's
overlays — it calls a hook `entry.cpp` connects, null while editing is off. So the viewer
builds and tests without the editor, and an editor change cannot reach a player who never
turned editing on.

The editor's renderer was split along the same line, and is gone. Its quad path —
world-space subdivision and near-plane clipping, the reason a draw-list quad is
perspective-correct at all — is `render/overlay.*`. What to draw and how it fades is the
scene's: the viewer draws the edited pack as it draws every other. What only an editor draws
stays in `editor/world/`: the selection outline, the picking, the open trail's point handles,
a dot where an icon is missing, the gizmo.

## Threading and the frame

| Where | Runs |
|---|---|
| worker | parsing and baking packs, up to four at once and handed over in order; the catalog's list and each download; festivals; maps |
| `ERenderType_PreRender` | MumbleLink, map changes, behaviors, Lua `OnTick`, building the scenes, publishing them |
| `ERenderType_Render` | the viewer's and the editor's windows, the map's tooltip; the overlay sink for a scene no moment drew; the editor's overlay over the world |
| GoodRender moments | the latest published scenes, submitted. Nothing else |

**The scene is world space and carries no camera.** It is built at one Present and drawn at
the next frame's moments, and projecting it there, with the camera `Moment` hands over, is
what keeps that frame of difference from showing: a marker is placed with the camera the
frame is drawn with, not the one MumbleLink had last Present. Min/max size, which needs a
marker's size in pixels, is resolved in the sink for the same reason.

Publishing is one atomic pointer swap of a finished scene. If the moments and Present turn
out to share a thread, the atomic is free; if they do not, nothing else has to change.

## Drawing

What goes where, through GoodRender:

| What | How | Moment |
|---|---|---|
| markers and trails over the terrain, the default | `draw_list`, through `render/overlay.*` with the moment's camera | `BEFORE_WINDOWS` |
| markers and trails behind the terrain | `world_quad`, the corners the overlay would draw | `WORLD_FINISHED` |
| the minimap and the full map | `draw_list`, projected from MumbleLink's map fields as the moment reads them, over the world's | `BEFORE_WINDOWS` |
| info text, the interact prompt, the map's tooltip and menu | ImGui windows | Nexus's render, over everything |

**Billboards do not use `world_billboard`.** Blish's billboard faces the ray to its own centre,
not the camera's plane, and its size is clamped on its projected corners; GoodRender's call
takes a centre and half sizes and faces it its own way. Building the corners ourselves keeps
both sinks drawing the one shape `itemCorners` in `scene.*` gives them.

`BEFORE_WINDOWS` is the point of all of it: over the game's HUD and nameplates, under its
windows and tooltips. **Over the terrain is the default**, as Blish HUD draws every marker:
the world never hides one. Behind the terrain is the player's choice, **Hide markers behind
terrain** in the options, and a pack's per category or marker with `render-layer`, our own
attribute (see [format.md](format.md#render-layer-ours)), which wins over the player's. There,
`world_quad` does the occlusion: each pixel is hidden behind what the game drew in front of it,
which no draw list ever is. The scene carries the choice per item (`SceneItem::behind_terrain`),
settled when it is built.

**Fades are colour alpha, per quad.** `alpha`, `fadeNear`/`fadeFar`, the combat fade and the
max-opacity setting all multiply into the quad's colour on the CPU, as Pathing's
`GetOpacity()` multiplies them before its shader sees them. Two of Pathing's fades are
per-pixel in its shaders (`ref/hlsl/marker.hlsl`, `trail.hlsl`): the fade of markers
between the character and the camera, and of trails around the character. Per quad, they are
an approximation. The exact version needs a shader of our own bound in the moment, which is
the one thing that would take us off `world_quad`; it is a late milestone, and only if the
approximation shows.

**Without the service**, the same scene goes through `render/overlay.*` at
`ERenderType_Render`, projected with `camera.*`, over everything the way the editor draws.
Only what no moment drew: every moment of a frame comes before its Present, so each moment
sets a flag for what it drew — behind the terrain, over it, the map — and the Nexus render
takes them (`good_render_sink::takeDrawn`) and draws the rest. A loading screen, the full map,
or no service at all, and the overlay draws.

**A marker without its icon is not drawn**, as Pathing draws nothing without a texture —
including while the icon waits for its turn in the load budget. The edited pack's are kept in
the scene with no texture, which no sink draws, and the editor's overlay draws a dot there
instead, so that a missing icon can be found; a trail without its texture, its ribbon flat.

**Picking comes from the scene.** The edited pack's items carry their pathable, and the
editor's overlay projects each through `itemCorners`, the call both sinks draw with, under the
Present's camera: the outline and the hit test are the shape drawn. The editor's rule — what
you click is exactly what you see, never a separate ray cast — holds through both sinks.

## Lua

Packs' scripts are written against Pathing's NeoLua API, which is .NET objects exposed to
Lua almost as they are. The job is to present the same names and the same behaviour from C++.

- **The API is a table**, the way attributes are. Each Lua-visible member — `World:CategoryByType`,
  `Mumble.PlayerCharacter.Position`, a marker's `InGameVisibility` — is one row with its
  getter, setter or function. Nothing about a member is written twice.
- **Both call styles work.** NeoLua accepts `World:CategoryByType(x)` and
  `World.CategoryByType(x)` alike, and packs use both: Teh's Trails has both spellings of
  several members. Every method binding drops a leading argument that is its own object. One
  shim, wrapped around every row, never a special case per member.
- **.NET shows through, and is reproduced.** `Event:OnTick` hands an XNA `GameTime`, so
  scripts read `.TotalGameTime.TotalSeconds`; `Vector3` has `Length`, `Dot`, `Cross`,
  `Normalize`; `I:Vector3`, `I:Color` (byte channels, alpha 255 by default), `I:Guid`,
  `I:Texture` and `I:Marker` construct. `I:Trail` is not built: Pathing's throws, and no pack
  calls it.
- **.NET's conversions too.** A nil handed where .NET wants a value type is that type's
  default — 0, a zero vector, transparent black, the empty Guid — as NeoLua converts it, and
  Teh's Trails relies on it. Enums are Gw2Sharp's names as strings (`"Elementalist"`), a
  number where the value has no name. A Guid is an interned userdata, so two lookups of the
  same marker give keys a table sees as one; entity handles are cached by index the same way.
- **Positions are Blish HUD's**, (x, z, y) of the file's and MumbleLink's (x, y, z). A
  marker's `Position` leaves out its `heightOffset`, as Pathing's does.
- **Numbers.** Pathing builds its state with Int32 integers and single floats
  (`new Lua(LuaIntegerType.Int32, LuaFloatType.Float)`); 5.4 has 64-bit integers and
  doubles. The difference only shows on overflow and precision, and in printing: NeoLua
  prints a float 5 as `5`, 5.4 as `5.0`. Noted, and not imitated unless a pack turns out to
  need it.
- **The sandbox is Pathing's**, `Scripting/PathingGlobal.cs`: the base functions it replicates
  and no others — no `pcall`, `error`, `load`, `io`, `debug` or `package` — and `require`
  resolved through the pack's own files. NeoLua's own extras (`rawmembers`, `mpairs`,
  `bit32` and the rest) are left out until a pack calls one. Pathing's `os` is
  `Scripting/Lib/Std/LuaLibraryOS.cs` with its quirks kept: `os.time` reads local time as
  if it were UTC, `wday` counts from 0, and an impossible date returns nil and a message.
  `table.ToLson`/`FromLson` are NeoLua's serialization, which Teh's Trails stores through.
- **A script error never escapes.** sol2 with its safeties on, every call into Lua protected.
  An error goes to the script console against its pack, file and line, and that call stops;
  nothing else does. A call that runs past `Host::call_budget` (two seconds) is stopped the
  same way, from a count hook, so a loop that never ends costs a hitch and a message rather
  than the game.
- **Each pack's frame time is measured** and shown in the console, as Pathing measures it, so
  a slow script is named rather than felt.
- `Storage` is one key-value store across all packs, as Pathing's `KvStates` is, keyed
  `namespace\name` (`000` when a script gives none) and persisted in `kv.json`, written at
  most every five seconds.

### The census

Anything a script indexes on a Pathing object that the table has no row for is logged once,
with pack, file, line and member, and returns nil. Run over the corpus offline, that log *is*
the list of what is left to implement, measured from what packs actually call rather than
guessed from Pathing's source.

The scripts in the installed packs, as a starting point:

| Pack | Lua files | Most used |
|---|---|---|
| `hero.blish.pack.zip` | 939 | `I:Vector3`, `World:CategoryByType`, `Mumble.PlayerCharacter.Position`, `I:Color`, `World:MarkerByGuid`, `Mumble.CurrentMap.Id`, `Storage:*`, `Debug:Watch`, `Event:OnTick` |
| `TehsTrails.taco` | 44 | `I:Color`, `Debug:Print`, `World:CategoryByType`, `Debug:Watch`, `World:TrailByGuid`, `Mumble.UI.MapScale`, `Vector3` methods |
| `LadyElyssa.taco` | 5 | `User.SetClipboard`, `Debug:Print`, `Menu:Add` |

On objects, the most touched members are `InGameVisibility`, `Position`, `Size`, `Tint`,
`Alpha`, `Texture`, `RotationXyz`, `SetTexture`, `SetPos`, `Show`/`Hide`/`Remove`,
`IsVisible`, `GetMarkers`, `CreateMarker`.

## Behaviors

What Pathing's `Behavior/` does, in `behaviors/`, as plain functions over the live map.

- **Read once, at the bake.** A pathable's behavior attributes become one `Behaviors`, through
  its categories as every attribute is, or null — and most markers are null, so a frame skips
  them at the cost of a pointer. A walk up the category chain looking for any behavior key
  comes first: reading 35 attributes one inherited lookup at a time for every one of 56k
  markers was a third of the bake. Pathing's parsing is kept to its quirks: enum lists are not
  trimmed, so `"Thief, Mesmer"` names no Mesmer; `invertbehavior="yes"` is false; a
  `script-*` argument list is split at every comma.
- **Run in Pathing's order** (`StandardMarker_Behaviors.cs`), since a later behavior's prompt
  replaces an earlier one's: the filters, then info, bounce, copy, toggle, reset guid,
  show/hide, the script. A category that is off runs nothing and loses focus, as
  `PathingEntity.Update` does.
- **The live pathable owns its state** (`behaviors::EntityState`, beside the copy of the baked
  pathable): focus, filtered and why, the schedule's window, the bounce's tween. A script that
  sets `InfoValue` or `BounceHeight` gets that marker its own copy of its behaviors; the
  pack's baked ones stay what the file says.
- **A script may create markers mid-update**, which moves every live marker, so nothing in the
  live map is held by reference across a call into Lua.
- **What `behavior` hid is `BehaviorStates`**: until the map changes, until an instance
  changes, until a time. Only the timers outlive the session, in `timers.txt` in Pathing's own
  layout. It is our file, not Pathing's: a player coming from Blish HUD starts with nothing
  hidden. A timer counts until it runs out; Pathing's counts until its five-second sweep.
- **Festivals** come from the GW2 API's daily achievement group, and from Blish HUD's own
  service when the API lists every festival at once, as Blish HUD's `FestivalContext` does —
  once per load, on a worker. Until the answer is in, no festival is on.
- **`schedule` is a subset of Cronos**: numbers, `*`, `?`, ranges, steps, lists and names, day
  of month and day of week both required to match. No pack uses `L`, `W` or `#`, and an
  expression with one is no schedule.
- **Achievement and raid filters** need the GW2 API with `account` and `progression`, which
  Nexus does not provide — see [plan.md](plan.md#open-questions). A pathable carrying one is
  not filtered by it.

**Interaction.** Pathing interacts on Blish HUD's own interact binding, F unless changed,
because it cannot read the game's. The viewer reads the game's instead, as Nexus keeps it:
Interact's two binds in `<game>/addons/Nexus/GameBinds.xml`, read again whenever the file
changes (`game_binds.h` has the format). A key whose game code is not a letter or a digit is
not known; Interact bound only to keys not known, or to the mouse, has no key here and the prompt
is clicked instead, and F stands in only when the file does not name Interact at all. A bind
counts with exactly its own modifiers held. It is
heard in Nexus's WndProc and passed on, so the game still interacts with whatever is in front
of the player, as it does with Blish HUD running, and it is ignored while the game's chat or
one of our text fields has the keyboard. The info text and the prompt ("Copy '…' to clipboard
[F]") are plain ImGui windows where Pathing puts its InfoWindow and gear; clicking the prompt
interacts. A hidden marker, while hidden markers are shown faded, says why it is hidden, and
interacting unhides it — removing its filters, its script with them, as Pathing does.

## Maps

Pathing's FlatMap and each pathable's `RenderToMiniMap`, in `map.*` and `map_scene.*`.

- **Three spaces.** The world in MumbleLink's metres; the continent, where MumbleLink puts the
  map's centre, about two feet to a unit with y down; and the UI pixel, which MumbleLink
  measures the compass in and MapScale is continent units per. A UI pixel is a screen pixel at
  the Normal interface size; Nexus's `Scaling` turns one into the other, interface size and
  DPI together. Pathing works in Blish HUD's own pixels and corrects twice for them — MapScale
  times 0.897, and 19 to 40 pixels added to the compass — and both corrections come out as UI
  pixels. Sizes Pathing gives in its own pixels — `mapDisplaySize`, a trail's width — keep
  their 0.897.
- **A map's rectangles** come from `/v2/maps?ids=all`, once a session on a worker, written to
  `maps.json` in Pathing's own fallback layout; the file stands until the API answers. A map
  neither knows shows nothing, as in Pathing.
- **Like the world scene, the map scene carries no view**: the moment reads MumbleLink's
  centre, scale and rotation itself, the context only — its identity is parsed into a cache
  only the frame's thread may touch. A map being dragged does not trail a frame behind.
- **Trails are laid on the continent once per live map** — 180,000 points on one map with
  the corpus installed. A frame picks which show; the fade by height follows the player, read
  where they are drawn.
- **Clipped by hand.** A GoodRender draw list ignores clip rectangles, so an icon on the
  compass's edge is cut with its uvs, and a line with Liang–Barsky. Pathing draws an icon only
  when its centre is inside, and so do we.
- **The tooltip and the menu** are ImGui, at Nexus's render, over what was published that
  frame. The right click reaches the game too, as FlatMap does not block it.

## Packs

- **Where they come from.** Our own folder, `addons/<addon>/packs/`, alone. Blish HUD's
  `addons/blishhud/markers/` is imported, not drawn from: drawn from, every marker shows twice
  while Blish HUD runs, and our catalog would be updating packs Blish HUD updates too.
- **The import** (`blish_import.*`) is offered on the first load when Blish HUD has anything,
  and from the options at any time. It copies the chosen packs into ours on a worker — a pack
  ours has as new or newer is kept — and brings what Pathing keeps under `markers/data`: the
  categories turned on or off (`states/category_preferences.txt`, or the two files before it),
  Storage (`kv/kv.json`), and the timers of hidden markers (`states/timers.txt`, the format ours
  is already). A category choice is kept as Pathing made it, whatever the pack's default, since
  the pack may not be loaded yet to say; turning it back to the default forgets it.
- **The catalog** is `https://mp-repo.blishhud.com/repo.json`, the list Pathing reads
  (`MarkerPackRepo/MarkerPackRepo.cs`), asked for once at load. Its entries carry name,
  author, size, a last update and a download URL (`MarkerPackPkg.cs`). Its `FileName` is
  someone else's path: anything but a bare `.zip` or `.taco` name is left out.
- **Which pack is which.** A package's copy on disk is the pack whose file name, less its
  extension, is the package's. It is stale when the catalog's
  `LastUpdate` is later than the file was written, as Pathing reads it — not by version, which
  the catalog gives as an opaque number. Pathing matches any file whose path *contains* the
  name, which mistakes `TehsTrails-HeroPoints.taco` for Teh's Trails.
- **Downloads go into our folder only**, as `<file>.part` beside the pack, over WinHTTP on a
  thread each, with Pathing's browser-like user agent. One that arrives is opened as an archive
  before anything else happens. Putting it in place is the frame's job: the old copy is open
  while installed, so it is dropped first, and nothing happens while the loader is reading,
  since the pack it reads may be the one being replaced. Then the folders are listed again and
  only what changed is loaded or dropped.
- **Keep updated** is Pathing's, on unless turned off, the exceptions in `catalog.json`. Once a
  session, as soon as the catalog answers, each of our packs it keeps updated that is stale is
  downloaded again.

## Packs across packs

The category tree is merged across packs; **attributes are not**. A marker inherits only from
its own pack's declarations of its categories, where Pathing inherits from every pack's,
since all of them land in one `PathingCategory`. No pack in the corpus has been seen relying on
it. If one does, the bake has to resolve against the merged tree instead, which means the
bake moves after the merge.

## The menu

Blish HUD's, one set of items wherever it is shown (`ui/main_menu.*`): **All Markers** and
**Scripts**, every entry a row `[ ] name >` whose box or name turns it on and off and whose
children open beside it on hover, as Pathing's context menu does; then **Manage Marker Packs** and
**Open Editor...**. All Markers' own box is kept with the categories' under the empty id, which no
category has, and off hides every pack, as Pathing's root category does; a bind, unset, turns it
on and off too.

**Smart categories**, on by default as in Pathing, leave out of each menu the categories with
nothing on the current map (`runtime::nodesOnMap`), and a separator with nothing under it, behind
a row "N Categories Are Hidden" that shows that menu's whole until the map changes.

It is shown in up to two places, chosen at the top of the options page: Nexus's own menu, and the
menu of an icon of ours in Nexus's bar. Nexus opens an icon's context menu on a right click, at the
mouse, and presses its keybind on a left one, so the left click opens the same items in a popup
of ours. That bind is the one Nexus lists as "Marker pack icon (left click)": Nexus has no other
way to say the icon was clicked. The popup opens under the icon, whose rectangle ImGui keeps as
the button last clicked (`placeMenu` in `viewer.cpp`). Both places are added and removed from the
options page, which is safe: Nexus holds its quick access lock only while it draws the bar, and
the options page is not drawn from there. In Nexus's menu our separator is left out: Nexus draws
one between addons already.

## The editor inside

- **It runs on demand.** The menu's Open Editor starts it at any time, and it runs until its
  main window is closed, which ends the session: the pack and every other window go with it,
  after asking when there are unsaved changes. Until then nothing of it runs: no windows, no
  settings read, no binds. Its windows are not on the addon's options page.
- **The viewer drives it, and that is all.** `marker_editor::load` registers nothing with Nexus:
  the viewer's render and `WndProc` call the editor's through `EditorHooks::render` and
  `wnd_proc`, which do nothing while it does not run. Nexus holds its render mutex across every
  callback, so a render callback of the editor's own could only be registered at load, which is
  why editing was once a setting applied at the next load. Nothing else passes between them.
- **Undo and redo are bound only while it runs**, so Ctrl+Z and Ctrl+Y are the game's otherwise.
  Registering a bind from inside a render callback is safe: Nexus's binds have a lock of their
  own, and Nexus runs their handlers on a thread of its own, so the editor's only raise a flag
  its frame acts on. Deregistering keeps the key the player chose.
- **A pack is opened from the editor's File menu**, in place, wherever it is. A pack in our
  folder that came from the catalog is overwritten by its next update, edits and all: the
  catalog's Keep updated is the author's to turn off for it.
- **The editor draws its pack itself**, over the world at Nexus's render, from its model: a live
  map of its own (`edited.*`), baked again whenever `State::revision` moves, built into a scene
  of its own with the tree's eye toggles and drawn with `render/overlay.*`. Its outlines, handles
  and picking work on that scene. Its images and trails are the editor's reads, which see an
  image added or a trail edited before it is saved. Its scripts and behaviors do not run: the
  author sees every marker.
- **The viewer goes on drawing every installed pack**, the one open in the editor included, as
  it is on disk. Editing an installed pack shows it twice, the saved copy and the edited one.
- **Save replaces a file the viewer has open.** An archive is opened sharing deletion
  (`pack/zip.cpp`), so a save can move it aside while the viewer reads from it. The viewer
  reads a pack again once its file is newer than what it read (`registry::writtenAt`, checked
  every two seconds), and the new copy replaces the old when it arrives. A folder pack counts
  as written when any `.xml` directly in it is.

A gizmo drag is a revision every frame, and each is a bake of the whole pack, as the editor's
own renderer did before. If a 55k-marker pack hitches under a drag, the bake narrows to the
current map; the undo records already name what each edit touched.

## Testing

As in the rest of the repo: against the real corpus, offline, under `ctest`.

- **Bake**: every pack in the corpus bakes; a baked value equals the editor's effective value
  for the same node. Two readings of inheritance would be two chances to disagree, so one is
  checked against the other.
- **Behaviors**: each filter and modifier against a small pack and a hand-set MumbleLink, a
  case per rule (`behavior_test.cpp`); what they are read from — attributes, cron, timers,
  festivals — on its own (`behavior_data_test.cpp`).
- **Lua**: every corpus script loads and ticks three frames on each of its maps against a
  zeroed MumbleLink, and every `script-*` function its pathables name is called as a player
  walking the map would set it off, with no errors but the packs' own; the census is empty for
  Hero's pack and Teh's Trails (`script_corpus_test.cpp`). The API's rules each have a case over a small pack written to a
  temp folder (`script_test.cpp`).
- **Scene**: built through a recording sink, so what would be drawn is asserted without a
  device.
- **Maps**: the projection against Queensdale's rectangles as the API and Pathing's fallback
  file both give them, and a hand-set compass (`map_test.cpp`); what the map shows over a
  kilometre square whose continent coordinates are its metres, drawn into a draw list
  (`map_scene_test.cpp`).

Only the sinks need the game, and each change to one is checked with a single pull decided in
advance.
