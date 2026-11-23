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
| GoodRender | **optional**: used when `served()`, a draw-list fallback otherwise | the addon still works without the service; the fallback is the editor's existing renderer |
| Frame split | everything is decided at Present; a moment callback only draws | the moment runs inside the game's frame, so anything slow there stalls the game |
| Packs in memory | parsed once, on a worker; a map change only switches which part is live | faster than Pathing's reread per map, and 55k markers baked is a few megabytes |
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
    viewer.*       lifecycle, the addon's name and folder, its options page, EditorHooks
    settings.*     viewer.json: "Enable pack editing"
    json_file.*    lenient reads and writes of small JSON files, the editor's too
    registry.*     installed packs: discovery, enable/disable, loading on a worker, the merged tree
    bake.*         pack::Pack -> the runtime model: resolved attributes, grouped by map id
    runtime.*      the live map: visibility, fade state, behaviors' state, markers scripts made
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
    scene.*        this frame's world quads, billboards and screen items, built from runtime
    render/        the sinks: good_render.* (moments) and overlay.* (Nexus render, draw list)
    map.*          the minimap and full map projection (Pathing's Entity/MapProjection.cs)
    camera.*       MumbleLink -> view/projection
    mumble.*       MumbleLink as Blish HUD names it, for scripts and behaviors; Gw2Sharp's enums
    http.*         HTTPS GETs on a worker, through WinHTTP: festivals, then the catalog
    textures.*     budgeted texture decoding: LoadBudget, TextureCache
    catalog.*      the public pack catalog: list, download, update
    store.*        what outlives a session: category toggles, script Storage
    ui/            the category menu, pack manager, settings, script console
  editor/          mpe_editor, links mpe_pack and mpe_viewer
```

**The dependency only points one way.** `editor/` includes `viewer/`; `viewer/` never
includes `editor/`, and two things hold it: `mpe_viewer_test` links `mpe_viewer` without
`mpe_editor`, and the ctest case `viewer_includes_no_editor` fails on any `editor/` include
under `src/viewer/`. Where the viewer has to reach the editor — the Edit button, the editor's
overlays — it calls a hook `entry.cpp` connects, null while editing is off. So the viewer
builds and tests without the editor, and an editor change cannot reach a player who never
turned editing on.

The editor's `editor/world/renderer.*` is split along the same line. Its quad path —
world-space subdivision and near-plane clipping, the reason a draw-list quad is
perspective-correct at all — is `render/overlay.*`. What only an editor draws stays in
`editor/`: the selection outline, the open trail's point handles, the gizmo. What to draw and
how it fades is still the editor renderer's until the scene exists.

## Threading and the frame

| Where | Runs |
|---|---|
| worker | parsing and baking packs; catalog downloads |
| `ERenderType_PreRender` | MumbleLink, map changes, behaviors, Lua `OnTick`, building the scene, publishing it |
| `ERenderType_Render` | the viewer's and the editor's windows; the overlay sink when there is no service |
| GoodRender moments | the latest published scene, submitted. Nothing else |

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
| a marker, billboarded | `world_quad`, corners from `Camera::billboardQuad` at the size min/max size allow under the moment's camera | `WORLD_FINISHED` |
| a marker with `rotate` | `world_quad`, corners from the same transform the editor's gizmo uses | `WORLD_FINISHED` |
| a trail | `world_quad`, one per quad of `viewer/ribbon.*`'s strip; `animspeed` scrolls the uvs | `WORLD_FINISHED` |
| info text, the interact icon, tooltips, the compass | `draw_list` | `BEFORE_WINDOWS` |
| the minimap and the full map | `draw_list`, projected from MumbleLink's map fields | `BEFORE_WINDOWS` |

**Billboards do not use `world_billboard`.** Blish's billboard faces the ray to its own centre,
not the camera's plane, and its size is clamped on its projected corners; GoodRender's call
takes a centre and half sizes and faces it its own way. Building the corners ourselves keeps
both sinks drawing the one shape `itemCorners` in `scene.*` gives them.

`BEFORE_WINDOWS` is the point of all of it: over the game's HUD and nameplates, under its
windows and tooltips. `world_quad` does the occlusion: each pixel is hidden behind what the
game drew in front of it, which no draw list ever is.

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
Only on a frame no moment drew: every moment of a frame comes before its Present, so the
moment sets a flag and the Nexus render takes it (`good_render_sink::takeDrawn`). A loading
screen, the full map, or no service at all, and the overlay draws.

**A marker without its icon is not drawn**, as Pathing draws nothing without a texture —
including while the icon waits for its turn in the load budget. The editor draws a dot
instead, so that a missing icon can be found.

**Picking comes from the sink.** The sink projects each marker once, for min/max size, and
keeps those screen rectangles for the frame; hit tests read them. The editor's rule — what
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
because it cannot read the game's; the viewer does the same, with the key a setting. It is
heard in Nexus's WndProc and passed on, so the game still interacts with whatever is in front
of the player, as it does with Blish HUD running, and it is ignored while the game's chat or
one of our text fields has the keyboard. The info text and the prompt ("Copy '…' to clipboard
[F]") are plain ImGui windows where Pathing puts its InfoWindow and gear; clicking the prompt
interacts. A hidden marker, while hidden markers are shown faded, says why it is hidden, and
interacting unhides it — removing its filters, its script with them, as Pathing does.

## Packs

- **Where they come from.** Our own folder, `addons/<addon>/packs/`, and, read-only, Blish
  HUD's `addons/blishhud/markers/` when it exists: a player moving over from Blish HUD keeps
  their packs without copying anything. Each source can be turned off, since a player still
  running Blish HUD would otherwise see every marker twice.
- **The catalog** is `https://mp-repo.blishhud.com/repo.json`, the list Pathing reads
  (`MarkerPackRepo/MarkerPackRepo.cs`). Its entries carry name, author, version, size and a
  download URL (`MarkerPackPkg.cs`). Downloads go over WinHTTP on a worker into our own
  folder; an update is a newer catalog version than the one installed.
- **A pack from the catalog is never edited in place**: the next update would overwrite the
  edits. Edit offers a personal copy instead.

## Packs across packs

The category tree is merged across packs; **attributes are not**. A marker inherits only from
its own pack's declarations of its categories, where Pathing inherits from every pack's,
since all of them land in one `PathingCategory`. No pack in the corpus has been seen relying on
it. If one does, the bake has to resolve against the merged tree instead, which means the
bake moves after the merge.

## The editor inside

- **A setting, "Enable pack editing", off by default.** It starts on for an install that
  already has the editor's `settings.json`, so an existing editor user does not lose it in the
  update. It lives in the viewer's own `viewer.json`, beside the editor's file rather than in
  it: the editor rewrites its file whole, and "the file exists" is what the rule reads.
- **Off means inert.** `marker_editor::load()` never runs: no windows, keybinds, Quick Access
  entry or `WndProc` handling are registered.
- **It applies at the next load.** Nexus holds its render mutex across every callback, so the
  options page cannot register the editor's callbacks from inside one, and unloading it there
  would drop unsaved edits. The page says so when the box and the session disagree.
- **Edit, on a pack in the pack manager**, closes that pack's source in the registry — the save
  later renames over the zip, which Windows refuses while it is open — and hands its path to
  the editor. From then on the viewer draws that pack from the editor's live model, baked again
  whenever `State::revision` moves: **what the author sees is what a player sees**, fades,
  behaviors and all. The editor's outline, handles and gizmo draw on top through the hook.
- **Scripts of the pack being edited are paused** by default. A script that moves markers
  would fight the gizmo; a toggle in the editor turns them back on.
- **Save** writes the zip as today, and the pack goes back to the registry as a normal one.
- **Before the Edit button**, the editor reports the pack it has open through
  `viewer::setEditedPack` every frame. The viewer drops that pack and closes its file — the
  save's rename needs it closed — and reads it again once the editor lets go, if it was one of
  the installed packs. Meanwhile the editor draws it with its own renderer.

A gizmo drag is a revision every frame, and baking a 55k-marker pack every frame would hitch.
The live bake starts with the current map only; the undo records already name what each edit
touched, if that turns out not to be enough.

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

Only the sinks need the game, and each change to one is checked with a single pull decided in
advance.
