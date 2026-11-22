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
    behaviors/     one file per filter and modifier, mirroring Pathing/Behavior/
    script/        the Lua state, the sandbox, the Pathing API table, the call-style shim
    scene.*        this frame's world quads, billboards and screen items, built from runtime
    render/        the sinks: good_render.* (moments) and overlay.* (Nexus render, draw list)
    map.*          the minimap and full map projection (Pathing's Entity/MapProjection.cs)
    camera.*       MumbleLink -> view/projection
    textures.*     budgeted texture decoding: LoadBudget, TextureCache
    catalog.*      the public pack catalog: list, download, update
    store.*        what outlives a session: category toggles, behavior state, script Storage
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
  `Normalize`; `I:Vector3`, `I:Color` (byte channels, alpha 255 by default), `I:Guid`, `I:Texture`, `I:Marker` and `I:Trail` construct.
- **Numbers.** Pathing builds its state with Int32 integers and single floats
  (`new Lua(LuaIntegerType.Int32, LuaFloatType.Float)`); 5.4 has 64-bit integers and
  doubles. The difference only shows on overflow and precision. Noted, and not imitated unless
  a pack turns out to need it.
- **The sandbox is Pathing's**, `Scripting/PathingGlobal.cs`: the base functions it replicates
  and no others, the `os` subset in `Scripting/Lib/Std/LuaLibraryOS.cs`, and `require` resolved
  through the pack's own files. No `io`, no loading from disk, no `debug`.
- **A script error never escapes.** sol2 with its safeties on, every call into Lua protected.
  An error goes to the script console against its pack, file and line, and that script's
  handlers stop; nothing else does.
- **Each pack's frame time is measured** and shown in the console, as Pathing measures it, so
  a slow script is named rather than felt.
- `Storage` is one key-value store across all packs, as Pathing's `KvStates` is, and persisted.

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

One file per filter and modifier under `behaviors/`, mirroring `Pathing/Behavior/`:

- **Filters**: achievement, festival, map type, mount, profession, race, raid, schedule,
  specialization.
- **Modifiers**: bounce, copy, info, reset guid, show/hide, toggle; plus the `behavior`
  attribute's own values (`StandardPathableBehavior.cs`).

Each is a plain function of the marker, the runtime's state, MumbleLink and the clock, so
each is testable offline. Mount, profession, race and specialization come from MumbleLink's
identity (Nexus shares it as `DL_MUMBLE_LINK_IDENTITY`). Achievement and raid filters need
the GW2 API with `account` and `progression`, which Nexus does not provide — see
[plan.md](plan.md#open-questions). State that outlives a session, such as "hidden until the
daily reset", lives in `store.*`.

Interaction — info, toggle, copy — is triggered by the game's interact key in Pathing. Nexus
can press a game bind and say whether it is bound, but not which key it is bound to, so how
the viewer hears it is open too.

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
- **Behaviors**: each filter and modifier as a function of its inputs, a case per value.
- **Lua**: every corpus script loads and ticks a number of frames on each of its maps against a
  fake MumbleLink, with no errors; the census is empty for what is marked done.
- **Scene**: built through a recording sink, so what would be drawn is asserted without a
  device.

Only the sinks need the game, and each change to one is checked with a single pull decided in
advance.
