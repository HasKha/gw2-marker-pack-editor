# MarkerPackEditor — plan

Roadmap and current state. Living document: update it in the same change that makes it
stale. It holds *what is left and in what order* — not how anything works.

Sibling docs — keep this file free of their content:
[architecture.md](architecture.md) (how the editor is built and why) ·
[viewer.md](viewer.md) (how the viewer is built and why) ·
[format.md](format.md) (the marker pack format) ·
[references.md](references.md) (where the ground truth lives)

## Goal

Two halves of one Nexus addon:

- **The viewer** (planned — see [The viewer](#the-viewer)): loads every installed pack and
  draws it into the game's frame through GoodRender, under the game's windows, with packs'
  behaviors and Lua scripts working as they do in Blish HUD's Pathing. What most players run.
- **The editor** (released): opens a GW2 marker pack, shows its contents as an editable
  tree, draws every marker and trail in the world at its real position, lets you drag them
  around with a 3D gizmo, and writes the pack back out as a zip that Blish HUD and TaimiHUD
  can load. Off unless a player turns it on.

Primary use of the editor is editing [`gw2-markers`](https://github.com/HasKha/gw2-markers),
but nothing in the addon may be specific to that pack.

## Current state

Released as **1.0**. The full loop works and is confirmed end to end: **open a pack, edit
it, save it back, and the edited markers show correctly in Blish HUD.**

| Area | State |
|---|---|
| Read | zip and folder sources, XML, attribute inheritance, `.trl`. 937/937 files across 9 test packs |
| UI | five independent windows — Tree, Details, Toolbar, Trail editor, Resources — with a View menu and a Nexus options checkbox each |
| Resources | the pack's images as a folder tree, with reference counts, a preview, and add / remove pending until a save |
| Trail editor | map id, the point list, drag fields, point handles in the world, and a Trail menu: reload, export, add circle. A trail whose `.trl` the pack does not have says so and names the file |
| Tree + properties | category tree with visibility toggles; attribute form generated from the attribute table, per-element attribute sets traced to Pathing, inheritance shown for categories as well as pathables |
| World | the open pack drawn by the viewer from the live model, as a player sees it; over it, click-to-pick for markers and trail points, and a dot where an icon is missing; nothing drawn while the map is open. Selection is shown by an outline on a marker and by the point handles on a trail — a selected ribbon is left untinted on purpose, so what the pack actually looks like stays readable |
| Selection | two independent selections — nodes and trail points — sharing one click rule. ctrl+click in the tree and the world, shift+click ranges in the tree and the point list, world picks reveal the node in the tree. Cleared by a click that hits nothing — blank space in the tree, empty world in the viewport — where a miss lets go of the points first and keeps the trail |
| 3D edit | ImGuizmo translate/rotate on the selection — markers and trail points alike — anchored on the first node selected; a group rotation orbits the rest about it |
| Rename | a trail is named by its `traildata` path, and editing that in the details window moves the `.trl` with it, unless the new path already names data — that is a re-point, and nothing is written over |
| Create | Add POI, Trail and Category from the toolbar, placed by the selection. A POI copies the selected marker's attributes; a trail comes with an empty `.trl`. Both get the current `MapID`, unless an ancestor category already supplies it. Add Category prompts for a dotted id (defaulted to the selection's own, so editing its last segment makes a sibling and appending one makes a child), a required display name, an optional description, and whether to copy the selection's attributes |
| Delete | a marker, or a category and its whole subtree, from the details window. Tombstoned, never erased |
| Undo / redo | Ctrl+Z / Ctrl+Y and an Edit menu, over every edit there is: attributes, add, delete, images, trails |
| Settings | the Settings toggles, gizmo mode, which windows are open and where the browsers start, kept in `settings.json` |
| Create pack | `New Pack...` builds an empty pack in memory; Save As writes the first zip |
| Write | serializes the owned document; comments, order, spelling, BOM, CRLF and the file split survive. 947/947 files stable on rewrite |
| Tests | `mpe_test`, 44 cases over the real pack corpus, the selection rules and the trail editing layer; `mpe_viewer_test`, 167 over the camera, the quad path, textures, settings, the bake, the registry, the ribbon, the scene, the GoodRender sink, the Lua scripts, the behaviors, the maps, the catalog, the edited pack and personal copies, the scripts over every scripted corpus pack, plus one hidden case that reaches the real catalog; and a check that nothing in `src/viewer/` includes the editor. All run by `ctest` |

A pack can be edited end to end, grown new branches, and started from nothing. The test
row's claims are measured against the corpus in [testing.md](testing.md).

---

## Next up

Roughly in order.

### 1. Whatever is still thin

- Reorder a trail's points. Adding and deleting them already work.
- **A rename field in the trail editor.** Renaming a trail means editing `traildata` in the
  details window, while the trail editor is where that path is on screen. A field there
  calling `pathable_edit::attributeRenamed` is the obvious next step.
- More shape tools beside the circle: a straight line between two points is the obvious
  next one. `gw2-markers/scripts/shared.py` remains the spec.
- **Enum value lists** from the gw2pathing.com attribute pages, so `ValueKind::Enum` gets a
  dropdown instead of a text box.
- **Validation pass in the UI**, reusing what `mpe_probe`'s lint already checks. Worth more
  now that new content can be authored in the editor rather than only edited. Two of the
  lint's checks already have a counterpart in the property panel — an unknown attribute and
  one that does nothing where it sits are both flagged there — so the pattern is set.
- **`fadecenter`**, Pathing's legacy alias for `canFade`, is not in the attribute table and
  so reads as an unknown attribute. Adding it means also adding a `deprecated` flag to keep
  it out of the "add attribute" picker. No pack in the corpus uses it; see
  [format.md](format.md).
- **Move pack loading off the render thread** if a large pack starts to stutter. 56k
  markers has not been slow enough to need it.
- **Tests above the pack layer.** `mpe_test` links `mpe_editor`, so anything there is
  reachable: `selection_test.cpp` covers the click rules and `trail_test.cpp` covers
  creating trails, following the selection and renaming traildata. Undo itself and the
  editor's indexes are still covered by hand and could go the same way. The windows cannot.
- **Screenshots in the README.** The rest of the release prep is done.
- **CI.** A Windows job that configures and builds would catch a break from a clean clone,
  which is the one thing a local build cannot. The suite skips itself without a corpus, so
  it would run green and prove only that it links — worth having anyway.

Anything that adds to the pack keeps [architecture.md's rules](architecture.md#rules):
move-only nodes, an `undo::Step` around every edit, and new categories through
`State::categoryAdded`.

### Known rough edges

- **Trail ribbons were rebuilt to Pathing's strip** (`viewer/ribbon.*`): joined at the bends,
  with the texture's left and right and its direction along the path worked out from
  `StandardTrail.World.cs` rather than taken from the old per-segment quads, which had flat
  trails mirrored and every trail's texture running backwards. Worked through by hand and
  pinned by `ribbon_test.cpp`; the next pull in game should look at an arrowed trail beside
  Blish HUD to confirm it.

Everything built so far has now been driven in game: the tree and the property form, the
world view, selection and the gizmo, save, add and delete, the resources window, the trail
editor, and undo/redo. What is left is not doubt about whether it works, but these:

- A group rotation stamps `rotate` onto markers that had none, which turns a billboard
  into a fixed-orientation quad. That is what orbiting means, but it is worth a look in
  game before deciding it is fine.
- A trail is still moved a point at a time; there is no "move the whole trail" handle. The
  gizmo skips trail pathables themselves.
- **A trail ribbon is not pickable in the viewport** — only markers and the open trail's
  point handles are, so a trail is still selected from the tree. Clicking a ribbon counts as
  a miss, which lets go of the open trail's points and leaves the trail itself selected (see
  `deselectInViewport`), so the painful case is covered; clicking some *other* trail's ribbon
  still deselects. A segment-wise distance test against the projected polyline would make
  ribbons pickable and close it properly.
- Window positions are whatever ImGui's `imgui.ini` remembers. Visibility is ours and is
  persisted; position is not, deliberately.
- Only the tree has an `update()` so far. If a second and third window grow one, drive the
  calls from the `editor/ui/windows.*` table instead of by hand — see [A window that is not
  drawing](architecture.md#a-window-that-is-not-drawing).
- `saveToDirectory`'s asset half — writing added images into a folder pack and deleting
  removed ones from disk — is the one write path the tests do not exercise, because the
  folder they point at is a real git checkout and a test that deletes from it would be
  vandalism. Both halves are confirmed by hand instead. Copying the checkout to a temp dir
  first would fix this and is the obvious next test.
- The resources window lists images only. `.trl` files are resources too and are already
  counted as references; showing them is a one-line change to `kImageExtensions` if it
  turns out to be wanted.
- **Add POI stores the raw character position.** A copied `heightoffset` then lifts the
  marker off that spot when it draws, which is either what you want or a surprise.
- **A new root category lands in the pack's first XML file.** It has no parent declaration
  to nest inside, so something has to choose, and nothing better suggests itself.
- **Perspective-correct texturing is capped at 16 subdivisions a side.** That covers every
  marker anyone would author; a quad both enormous and right in the camera's face still
  carries a little of the affine bend near the corner closest to you. Raising the cap is a
  constant in `viewer/camera.h`, paid for in vertices.
- **Quads are clipped against the near plane only, not the sides of the frustum.** A marker
  you are standing inside can project a corner tens of thousands of pixels off screen, and
  those cells are subdivided and emitted like any other before ImGui's scissor throws them
  away. Correct, just wasteful; side planes would be four more of the same clip.

## The viewer

Each milestone ends in something that works and can ship; none leaves the editor broken in
between. How each part works is [viewer.md](viewer.md).

### 0. Split, with no change in behaviour — done offline

`mpe_viewer` exists in `src/viewer/`: the camera, `LoadBudget` and `TextureCache`, the
draw-list quad path as `render/overlay.*`, and the lifecycle that owns the addon's options
page. `entry.cpp` loads the editor only when "Enable pack editing" is on, which it is for an
install that already has `settings.json`. Every case that passed before still passes,
`mpe_viewer_test` links without `mpe_editor`, and `viewer_includes_no_editor` holds the
include rule.

**Left: one pull in game**, to answer one question: with an existing `settings.json`, does the
options page show "Enable pack editing" ticked above the editor's window checkboxes, and does
the editor open, draw and save a pack as 1.6 did? Then untick, reload the addon, and check
the editor's windows, keybinds and Quick Access entry are all gone.

### 1. Packs drawn, no scripts — done offline

Built: the registry (our `packs` folder and Blish HUD's, discovered as Pathing does, loaded on
a worker, categories merged); the bake, the live map and the scene; the GoodRender sink beside
the overlay fallback; billboards, rotated markers, min/max size, fades as per-quad alpha;
trails as Pathing's strip with `animspeed`; the Markers window with toggles kept in
`categories.json`. The editor hands the pack it has open over and gets it back through
`viewer::setEditedPack`. The bake is checked over the whole corpus, and the scene through a
recording moment.

**Left: one pull in game**, deciding these in advance:
- With GoodRender loaded: do Hero's pack and `gw2-markers` draw under an open window and
  hidden behind a wall, matching Blish HUD at the same spot — size, tint, fade, and an arrowed
  trail pointing the same way?
- Without it: do they draw over everything, the editor's way?
- With editing on: open a pack from the Blish HUD folder, save it, close it — does the save
  succeed, and does the pack come back in the viewer?

Not in it, and not needed for the pull: inheritance across packs — see
[viewer.md](viewer.md#packs-across-packs). Pathing's view distance, marker scale, max opacity
and combat fade settings came with milestone 4.

### 2. Lua — done offline

Built: Lua 5.4 through sol2, one state for every pack, reset with every live map; Pathing's
sandbox and `os`; the call-style shim and the census; the API — `World`, `Mumble`, `I`,
marker, trail and category members, `Event:OnTick`, `Storage` (in `kv.json`), `Debug`, `Menu`,
`User.SetClipboard`, `Pack:Require` and `CreateMarker`, NeoLua's LSON; a per-call time budget;
the script console (messages, watches, frame time per pack, the census) and the scripts' menu
under the Markers window. "Run pack scripts" in the options turns it all off. Every corpus
script loads and ticks on each of its maps without an error, and the census is empty for
Hero's pack and Teh's Trails.

**Left: one pull in game**, deciding these in advance:
- With Hero's pack and Teh's Trails installed: does the console stay free of errors across
  two map changes, and does each pack's frame time stay under a millisecond?
- Does a script-driven marker in Hero's pack — one `World:MarkerByGuid` moves or retints —
  look as it does in Blish HUD at the same spot?
- Does LadyElyssa's menu entry copy to the clipboard, with its message shown?

Not in it: Pathing's clipboard consent setting, which comes with the interaction settings in
milestone 3; until then every copy is allowed, and announced.

### 3. Behaviors and interaction — done offline

Built: every filter (festival, map type, mount, profession, race, schedule, specialization, the
`behavior` attribute, `script-filter`) and modifier (info, bounce, copy, toggle, show/hide,
reset guid, auto-trigger, `script-*`), focus by trigger range and interaction, under
`viewer/behaviors/`. What `behavior` hid is kept in `timers.txt`. The interact key is heard
through Nexus's WndProc and passed on to the game, F unless the player picks another letter.
Pathing's pack settings are in the options: markers hiding themselves, animating, the prompt,
clipboard consent (which `User.SetClipboard` honours), and hidden markers shown faded to
unhide them. Festivals come from the GW2 API, with Blish HUD's fallback. Scripts reach
behaviors through `GetBehavior`, `Focused` and `ResetLength`, and the corpus test now calls
every `script-*` function packs name, the way a player walking each map would.

**Left: one pull in game**, deciding these in advance:
- LadyElyssa's copy markers: walking into one copies its waypoint and says so; pressing F at
  one copies again. Does the game still interact with what is in front of the player?
- A `behavior="1"` marker in `tw_ALL_IN_ONE`: F hides it, a map change brings it back.
- Teh's Trails on a skyscale route: the info text shows in range and goes out of range, and a
  bouncing marker bounces.
- Hero's pack on a raid map: markers hidden by profession or specialization match the
  character, as in Blish HUD.

**First beta here**, once that pull passes: local packs, scripts and behaviors all working.

Not in it: achievement and raid filters (milestone 7).

### 4. Maps and screen — done offline

Built: the minimap and the full map, as Pathing's FlatMap draws them — markers at their
`mapDisplaySize`, zoomed or not, above/below arrows, trails as lines faded by height, the
compass turning with the camera, the fade after the map opens. Every map's rectangles come
from the GW2 API once a session, kept in `maps.json` for the next. The map draws at GoodRender's
`BEFORE_WINDOWS`, over the HUD and under the windows, or over everything without it. Over a
marker on either map: its tooltip, the category path with Shift, and a right-click menu that
hides its category. Pathing's map settings, its pack settings for the world (view distance,
marker scale, max opacity, combat fade, trail speed, world markers off) and its info display
mode are in the options, the options in two sections.

**Left: one pull in game**, deciding these in advance:
- The minimap at Normal and at Larger interface size, in both corners, with compass rotation
  on and off: does each marker sit on the game's own icon for the same spot — a waypoint
  marker on the waypoint — and does nothing spill outside the compass?
- With DPI scaling on in the game's options: does the compass's box still fit the compass?
  That is the one thing Nexus's `Scaling` is trusted to carry.
- The full map: are markers drawn over the map, or under it — see
  [Open questions](#open-questions)? Panning it, do they stay put without trailing a frame?
  Zoomed all the way out on a map with every pack installed, does the frame time hold?
- The tooltip over a marker on the full map, the category path with Shift, and right-click →
  hide.

Not in it:
- **Pathing's own art**: its info window and gear icon are the game's textures, drawn with
  an alpha-mask shader; ours are plain ImGui windows. Its above/below icons are drawn as
  arrows.
- **A trail without a tint is white on the map**, where Pathing samples its texture for the
  dominant colour. That needs the image decoded on our side, which Nexus does not hand back.
- **Hiding the full map as the camera moves**, before MumbleLink says it closed
  (FlatMap's `MapTriggerHideFromCamera`): one frame's difference at most.
- Pathing's per-pixel fades — markers between character and camera, trails around the
  character — and their two settings: milestone 7.

### 5. The catalog — done offline

Built: the **Pack catalog** window, Pathing's pack repository — the public catalog's packs,
filtered by name, author or category, each with Download, Update or "Up to date", Keep
updated, Info and, for our own copy, Delete. Downloads go into our `packs` folder on a worker
each, with progress, are checked to be archives, and replace the old copy once nothing is
reading it; only what changed on disk is loaded or dropped, not every pack. A catalog pack in
our folder stood in for Blish HUD's copy of it, until milestone 7 stopped drawing from Blish HUD's folder. Packs kept updated are updated once a
session, as soon as the catalog answers. WinHTTP was checked once against the real catalog,
which reads, and its smallest pack downloads and opens.

**Left: one pull in game**, deciding these in advance:
- Download a pack not installed: does the window show its progress, and does it draw without
  a reload of the others?
- Update a pack: does the new copy replace the old, with no marker shown twice?
- Unload the addon while a large pack downloads: does it unload within a second or two? A
  download stops between reads, so only a stalled read waits for its timeout.

**First public release here**, once that pull and the earlier ones pass: the maintainer's call.

Not in it: Pathing's pack optimizer (TmfLib's PackWriter, which rewrites a downloaded pack),
and a catalog other than the public one (Pathing's `PackRepoUrl` setting, which its UI does
not offer).

### 6. Edit from the viewer — done offline

Built: whatever the editor has open, however it was opened, the viewer draws from the
editor's model, baked again per revision, as a live map of its own beside the installed
packs': GoodRender, fades, filters and all, on the minimap and the full map as well. Its scripts do not run. The editor's renderer is
gone; what only an editor draws — outlines, picking, point handles, a dot for a missing icon —
draws over the viewer's scene through `EditorHooks::draw_overlay`, picking by the shapes the
scene drew.

A Packs window with Edit, and personal copies a catalog update could not overwrite, were
built too, and dropped after the first look in game — see milestone 7.

**Left: one pull in game**, deciding these in advance:
- Drag a marker of Hero's pack with the gizmo, GoodRender loaded: does it follow without a
  hitch, and hide behind a wall as it moves?
- Click a marker, then a trail point: is what is outlined what was clicked, with GoodRender
  and without it?
- A marker whose icon is missing: is there a dot where it stands?
- Save, then close: does the pack go back to drawing from disk, unchanged?
- Drag a marker with the gizmo while the minimap shows it: does its icon follow there too?

Not in it: the edited pack's info text and interact prompt, and a toggle to run its scripts
while editing.

### 7. After the first look in game

The first pull found the world drawing no installed pack — their textures were never given
Nexus's API, fixed — and asked for the shape below. Each step is a commit of its own.

1. **Fewer windows** — done. The Packs window, Edit from the viewer and personal copies go: the
   editor's own File menu is how a pack is opened, in place. The Markers window goes too: the
   menu below replaces it. "Pack catalog" becomes **Marker Packs**.
2. **One menu, Blish HUD's** — done offline. At the top of the options page: show it in Nexus's menu (what is
   done now), and add an icon of our own to Nexus's bar, whose left and right click both open
   it. The same items everywhere: **All Markers** and **Scripts** as Blish HUD draws them,
   `[ ] name >` selectables that open their children to the side on hover; **Manage Marker
   Packs**, which opens Marker Packs; **Edit Marker Pack**, which opens the editor.
3. **Blish HUD's packs imported, not read** — done offline. Its folder is no longer drawn from. A wizard on
   the first load, and a button in options, copy its packs into ours, with Pathing's category
   choices, script storage and hidden-marker timers.
4. **Interact is Nexus's.** The key comes from Nexus's game binds (`GameBinds.xml`, its
   Interact action), not a setting of our own. GW2's key codes are its own: letters and digits
   are their ASCII codes, as Windows' are; anything else falls back to F.
5. **The editor on demand.** No "Enable pack editing", nothing of the editor registered at
   load: the viewer's callbacks drive it, so Edit Marker Pack starts it at any time. It was
   applied at load only because the editor registered a render callback of its own, which
   cannot be done from inside one (Nexus holds its render mutex across every callback), and
   unloading it would have dropped unsaved edits.

### 8. Later

- Achievement and raid filters, once the API key question is settled.
- Pathing's per-pixel fades, with a shader of our own, if the per-quad version shows.

## Open questions

- **The addon's name**, and whether the repo is renamed to match. The signature stays either
  way; whether Nexus's GitHub update check follows a renamed repo needs checking first.
- **Where a GW2 API key comes from** for the achievement and raid filters. Nexus has no
  provider of its own.
- **Whether `world_quad` samples with wrap.** A trail texture repeats along its length; with
  clamping, every repeat has to be its own quad. The ribbon keeps each quad's v near zero
  either way, and one look at a long trail segment in the pull answers it.
- **Whether the full map counts as a window** at `BEFORE_WINDOWS`, i.e. whether markers drawn
  there land over it or under it. One pull answers it. Under it, the full map moves to
  `UI_FINISHED` and the minimap stays.
- **Texture memory with every pack loaded.** Nexus never releases a texture, which was fine
  for one pack; with all of them, a long session accumulates every icon it has seen.

The editor has none open. Its last one — tombstone or erase — was settled by building it:
tombstones, for the reasons in [architecture.md](architecture.md).
