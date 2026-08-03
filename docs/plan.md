# MarkerPackEditor — plan

Roadmap and current state. Living document: update it in the same change that makes it
stale.

Sibling docs — keep this file free of their content:
[architecture.md](architecture.md) (how it is built and why) ·
[format.md](format.md) (the marker pack format) ·
[references.md](references.md) (where the ground truth lives)

## Goal

A Nexus addon that opens a GW2 marker pack, shows its contents as an editable tree,
draws every marker and trail in the world at its real position, lets you drag them
around with a 3D gizmo, and writes the pack back out as a zip that Blish HUD and
TaimiHUD can load.

Primary use is editing [`gw2-markers`](https://github.com/HasKha/gw2-markers), but
nothing in the addon may be specific to that pack.

## Current state

M0 complete and verified in game: the addon loads, the window shows, the keybind toggles
it, and **Open Pack…** opens the native dialog without stalling the frame. It does not
read the pack yet.

M1 done: the pack layer reads every pack in the markers folder — zip, XML, attributes
with inheritance, and `.trl`.

M2 done and verified in game: **Open Pack…** and **Open Folder…** both load, the tree
shows the category hierarchy with selection, and the property panel edits attributes.

M3 done and verified in game: markers and trails draw in the world from the MumbleLink
camera, with per-node visibility toggles in the tree.

M4 done and verified in game: markers can be clicked in the world to select them, and a
selected POI can be dragged or rotated with ImGuizmo.

M5 built, **not yet verified in game**: the editor can save. A zip is rebuilt whole with
the previous one kept as `.bak`; a folder pack has its `.xml` files rewritten in place.

**The next thing anyone should do is save a pack and load it in Blish HUD.** Everything up
to M4 is confirmed working in game; saving is confirmed only by `mpe_probe`, which proves
the bytes are self-consistent but not that Blish accepts them.

Before touching the pack layer, run:

```
mpe_probe --save-test <scratch-dir> "<...>/markers/haskha_markers.zip" "<...>/git/gw2-markers"
```

It exercises read, model, `.trl`, writer stability and a full save round trip in one go,
without launching the game. Every format claim in `format.md` was measured with it.

### Test packs

In `C:\Users\Giorgio\Documents\Guild Wars 2\addons\blishhud\markers`:

| Pack | Shape | Why |
|---|---|---|
| `haskha_markers.zip` | 80 entries, 10 hand-authored xml at the root, `Data/`, comments throughout | the round-trip target — losing a comment here is a bug |
| `hero.blish.pack.zip` | 1918 entries, 64 xml, 140 trl, 1375 png; `_markerCategories.xml` + `map<id>.xml` | large, popular, and already TmfLib `PackWriter` output — the scale and machine-generated case |

Plus `C:\Users\Giorgio\Documents\git\gw2-markers` as a **folder**, which must produce the
same model as the zip built from it.

Others in that folder are not test targets, but `mpe_probe` accepts them and running it
over all of them is how the format claims in `format.md` were measured.

## Milestones

### M0 — Scaffold ✅
- [x] Repo structure, `.clang-format`, `.gitignore`, `.gitattributes`, LICENSE, README
- [x] CMake with all FetchContent modules
- [x] `entry.cpp` + `marker_editor` module: window, options, quick access, keybind
- [x] `file_dialog` on a worker thread, defaulting to the Blish HUD markers folder
- [x] Configures and builds clean -> `bin/Release/MarkerPackEditor.dll`
- [ ] Verify it loads in game and the Open Pack dialog appears without stalling the frame

Learned while building M0, now recorded in `CLAUDE.md`: imgui is 1.80 and lacks
`BeginDisabled`; `WIN32_LEAN_AND_MEAN` breaks gw2relib; CMake 4 rejects miniz's repo
CMakeLists so we use the release amalgamation.

### M1 — Read a pack
- [x] `mpe_pack` static lib + `mpe_probe` console harness, so format work is testable
      without launching the game
- [x] `pack/zip` — miniz, with TmfLib's path normalization
- [x] `pack/attributes` — the attribute table, ~75 entries with kinds, defaults and
      inheritance
- [x] `pack/model` — Pack / XmlFile / Category / Pathable, explicit vs effective values
- [x] `pack/xml_reader` — pugixml -> model, case-insensitive, keeps unknown attributes,
      records each node's source byte span
- [x] `pack/trl` — read and write, byte-exact
- [x] Load `gw2-markers` end to end with correct counts
- [x] Tested against all 9 packs in the markers folder
- [x] Decided how routes are handled — carried through untouched, see Resolved below
- [ ] Fill in enum value lists from the gw2pathing.com attribute pages (needed by M2's
      property panel, not by the reader)

Measured by `mpe_probe` across all 9 packs:

| | |
|---|---|
| XML files parsed | 937 / 937, zero failures |
| Elements anchored to a source byte offset | 146,146 / 146,146 |
| `.trl` files read | 8,288 / 8,288, zero failures |
| `.trl` byte-exact round trip | 8,288 / 8,288 |
| Largest pack modelled | `tw_ALL_IN_ONE`: 14,430 categories, 55,887 POIs, 1,196 trails |
| Routes found | 0 |

### M2 — Tree + properties
- [x] **Open Pack…** now actually loads the pack into the editor
- [x] Tree top level decided: categories only, see [architecture.md](architecture.md)
- [x] `ui/tree_window` — category hierarchy, pathables under their category, ctrl+click
      multi-select, "Unresolved" node for dangling `type`s
- [x] `ui/property_panel` — form generated from the attribute table; the widget for each
      value comes from its `ValueKind`, no attribute is named in the UI code
- [x] Add / remove attribute, with the picker filtered to what applies to the selection
- [x] Inherited values always shown, greyed out, with the category they come from and an
      "override" button
- [x] Multi-select bulk edit, `<multiple values>` where the selection disagrees
- [x] **Open Folder…** — load an unzipped pack source tree (e.g. the `gw2-markers` git
      checkout) as well as a zip, through a shared `pack::DataSource`
- [ ] Verify in game
- [ ] Shift+click range select
- [ ] Fill in enum value lists so `ValueKind::Enum` gets a dropdown instead of a text box

### M3 — Draw it
- [x] `world/camera` — view/projection from MumbleLink, world-to-screen, FOV parsed out of
      the Mumble identity JSON
- [x] `resources` — icons and `.trl` data pulled from the pack source on demand, budgeted
      to a few decodes per frame so arriving on a map cannot hitch
- [x] `world/renderer` — billboards with `iconSize`, `heightOffset`, `alpha`,
      `color`/`tint`, `minSize`/`maxSize` clamping and distance fade
- [x] Trail ribbons, including `isWall`, with Blish's exact half-width and UV scale
- [x] Highlight the tree selection in the world
- [x] Per-node visibility toggles in the tree — eye / eye-slash icons, cascading down the
      category tree. View state only: never saved, never marks the pack dirty
- [x] Window visibility exposed as a checkbox on the Nexus options page, so closing the
      window is recoverable without a keybind
- [x] Verified in game: positions, camera transform, image loading and sizes all correct
- [x] Marker rotation, including the sign-derived UV mapping that a naive corner mapping
      gets 180 degrees wrong
- [ ] Select in the world to select in the tree (needs M4's picking)
- [ ] `animspeed` — trail textures do not scroll yet

### M4 — Edit in 3D
- [x] Click-to-select in the viewport, frontmost hit wins; ctrl+click extends. Driven by a
      Nexus `WndProc` callback, because Nexus never forwards a world click to ImGui — see
      the note in `CLAUDE.md`
- [x] Hover outline in the world, so picking is visible rather than guessed at
- [x] ImGuizmo translate / rotate on a single selected POI
- [x] Gizmo edits write straight back into the attributes, so the property panel and the
      world stay in step
- [x] Menu bar: File (Open Pack / Open Folder / Reload / Close / Save) and Settings
      (3D highlight, Select in viewport); gizmo mode radio at the top of the window
- [ ] Verify in game — particularly the rotate gizmo's axis mapping
- [ ] Trail points selectable and draggable individually
- [ ] Undo / redo *(deferred by request)*

Scale is deliberately absent: a marker has no scale transform, only `iconSize`, which the
property panel already edits.

### M5 — Write a pack
- [x] `pack/writer` — serializes the document the pack was parsed into. Comments, the
      declaration, attribute order, original spelling, values, the BOM, CRLF and the file
      split all survive; blank lines and intra-tag whitespace do not, so a pack reformats
      once on first save. Still **not** modelled on TmfLib's `PackWriter`, which also
      flattens the file structure and drops comments; see `format.md`
- [x] `pack/trl` write, preserving `(0,0,0)` segment terminators *(done in M1)*
- [x] Zip write, Save and Save As; folder packs update their `.xml` files in place
- [x] Fixed-point test in `mpe_probe`, plus an opt-in `--save-test` that writes a real zip,
      reads it back and compares
- [x] Dirty tracking, confirm-on-close/reload with a Save-first option, previous zip kept
      as `.bak`
- [ ] Load a saved pack in Blish HUD and confirm it still works

Measured by `mpe_probe --save-test` over all 9 packs plus the `gw2-markers` folder:

| | |
|---|---|
| Files stable on rewrite | **947 of 947** — write, reparse, write again, identical |
| Full save → reopen → compare | all 10 sources: every entry preserved, every XML identical, model unchanged |

Stability is the property that matters now that byte equality with the original is
deliberately given up: the first save reformats, and no save after it changes anything it
did not have to.

### M6 — Create and edit structure
The writer became a serializer specifically to make this possible, so it is unblocked.
Adding or removing a marker is now a `pugi::xml_node` insert or erase on the document the
`Pack` owns, plus the matching entry in `Pack::pathables` / `categories`.

- [ ] Add / delete a POI, a trail and a category
- [ ] Place a marker at the player's position (MumbleLink `AvatarPosition`)
- [ ] Create a new pack from scratch — needs a `Pack` built without a `DataSource`, and a
      first save that writes a zip from nothing
- [ ] Undo / redo *(deferred from M4; worth doing before add/delete gets used in anger)*
- [ ] Trail points selectable and draggable individually
- [ ] Settings persisted to `MarkerPackEditor/settings.json`
- [ ] Move pack loading off the render thread if a large pack starts to stutter
- [ ] Validation pass in the UI, reusing what `mpe_probe`'s lint already checks
- [ ] README with screenshots, GitHub releases

Watch out when implementing add/delete:
- `Pathable::category` and every index in `State::pathables_by_category`,
  `orphan_pathables`, `Selection` and the renderer cache are positional. Erasing from
  `Pack::pathables` invalidates all of them — rebuild the index and clear or remap the
  selection, or mark entries dead instead of erasing.
- A new node needs its `SourceNode.file` set, and a category needs a declaration appended,
  or the writer will not know where to put it.
- `Pack` is move-only. That is deliberate; do not "fix" it by adding a copy constructor.

## Open questions

1. **Blank lines on save.** The serializer drops whitespace-only text nodes, so the blank
   lines `gw2-markers` uses to group POIs go away — 13 of them in `w1.xml`. They are
   organizational in the same way comments are, and comments *are* kept. Preserving them
   would mean keeping whitespace text nodes that contain more than one newline and
   dropping the rest, which interacts with how pugixml indents mixed content and needs
   testing. Raised with the user, not yet decided.
2. **`.taco` vs `.zip`.** Both are zips. Confirm TaimiHUD and Blish HUD accept both from
   the markers folder.
3. **`animspeed`** is parsed and stored but the renderer does not scroll trail textures.
4. **Occlusion.** No depth buffer, so markers draw over world geometry regardless of
   `occlude`. Matches how a pack looks in Blish with occlude off; a DX11 backend behind
   the renderer interface is the way out if it ever matters.

### Resolved

- ~~**FOV.**~~ Nexus exposes no parsed identity — there is no `DL_MUMBLE_LINK_IDENTITY`
  and `Nexus.h` does not mention `Identity` at all. `world/camera` parses the `fov` field
  (vertical, radians) out of the raw identity JSON in `Mumble::Data::Identity`, caching by
  the identity string so the JSON is only touched when the game changes it.

- ~~**Tree top level.**~~ Categories only; XML files never appear as nodes. Measured: of
  31,817 categories across the 9 test packs, exactly **one** is declared in more than one
  file, and it is a root wrapper. Files are tracked as metadata, not hierarchy. See
  [architecture.md](architecture.md).
- ~~**Routes.**~~ Deprecated and unimplemented upstream: TmfLib's `UnpackRoutePoi` returns
  null with a "deprecated route type" TODO, Pathing's `StandardRoute` is an empty class,
  and no `<route>` appears in any of the 9 packs tested. We parse and carry them through
  untouched, and do not model them.

- ~~**Round-trip fidelity.**~~ Was the biggest risk; measured with `mpe_probe` over all 9
  packs in the markers folder. pugixml parses **937/937** XML files with zero failures,
  and **146,146/146,146** elements resolve to a source byte offset. It cannot reproduce
  intra-tag whitespace, so the writer **splices edits into the original bytes** rather
  than reserializing the tree. Details in [format.md](format.md).

- ~~**Coordinate units.**~~ From `Pathing/ref/hlsl/marker.hlsl`: positions are metres,
  distance attributes are inches, conversion is `/ 0.0254`. Blish's internal Z-up space
  is its own business and not part of the format. Still worth verifying empirically in M3.
- ~~**ImGuizmo pin.**~~ Tag `1.83` compiles against the RaidcoreGG imgui 1.80 fork. Still
  to confirm it *behaves* correctly there once M4 starts.
