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

M1 essentially done: the pack layer reads every pack in the markers folder — zip, XML,
attributes with inheritance, and `.trl`. Nothing is wired into the addon UI yet; the
reader is exercised by `mpe_probe`, not by the game.

### Test packs

In `C:\Users\Giorgio\Documents\Guild Wars 2\addons\blishhud\markers`:

| Pack | Shape | Why |
|---|---|---|
| `haskha_markers.zip` | 80 entries, 10 hand-authored xml at the root, `Data/`, comments throughout | the round-trip target — losing a comment here is a bug |
| `hero.blish.pack.zip` | 1918 entries, 64 xml, 140 trl, 1375 png; `_markerCategories.xml` + `map<id>.xml` | large, popular, and already TmfLib `PackWriter` output — the scale and machine-generated case |

Others in that folder are not test targets.

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
- [ ] `ui/tree_window` — hierarchy with selection
- [ ] Decide and implement the tree's top level (see Open questions)
- [ ] `ui/property_panel` — form generated from the attribute table
- [ ] Add / remove attribute
- [ ] Show inherited values greyed out with an "override" affordance
- [ ] Multi-select and bulk edit

### M3 — Draw it
- [ ] `world/camera` — matrices from MumbleLink, validated against a known marker
- [ ] `pack/textures` — pack images into Nexus textures
- [ ] `world/renderer` — billboards with `iconSize`, `alpha`, `color`, fade
- [ ] Trail ribbons, including `isWall`
- [ ] Highlight the tree selection in the world and vice versa

### M4 — Edit in 3D
- [ ] Click-to-select in the viewport (ray vs. billboard, nearest hit)
- [ ] ImGuizmo translate / rotate / scale on the selection
- [ ] Gizmo edits write back into the model and the tree/property panel
- [ ] Trail points selectable and draggable individually
- [ ] Undo / redo

### M5 — Write a pack
- [ ] `pack/xml_writer` — preserves comments, attribute order, original casing and the
      original file split. Explicitly **not** modelled on TmfLib's `PackWriter`, which
      is a re-packer; see `format.md`
- [ ] `pack/trl` write, preserving `(0,0,0)` segment terminators
- [ ] `pack/zip` write; save and save-as
- [ ] Round-trip test: load `gw2-markers`, save untouched, diff should be empty
- [ ] Load the saved pack in Blish HUD and confirm it still works
- [ ] Dirty tracking, confirm-on-close, backup of the previous zip

### M6 — Polish
- [ ] Settings persisted to `MarkerPackEditor/settings.json`
- [ ] Create a new pack from scratch; add category / POI / trail
- [ ] Place a marker at the player's position
- [ ] Validation pass: missing icon files, dangling `type`, duplicate GUIDs
- [ ] README with screenshots, GitHub releases

## Open questions

1. **Tree top level.** Options: (a) categories only, files hidden; (b) XML files as
   roots with their categories beneath; (c) a "Files" section listing xml/png/trl
   alongside a "Categories" section. Categories can be split across files, which argues
   against (b) as the only view. Leaning towards (a) as the default with a toggle for a
   file-oriented view — decide during M2.
2. **FOV.** `Mumble::Identity::FOV` comes from the identity JSON, which Nexus parses;
   confirm whether Nexus exposes a parsed `Identity` via DataLink or whether we parse
   `Mumble::Data::Identity` ourselves.
3. **`.taco` vs `.zip`.** Both are zips. Confirm TaimiHUD and Blish HUD accept both from
   the markers folder.
*(nothing blocking)*

### Resolved

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
