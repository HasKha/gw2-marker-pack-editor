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

M0 done: the addon builds and produces `bin/Release/MarkerPackEditor.dll`, opens a
window, and can pick a pack file through a native dialog. It does not read the pack yet.

Next up is M1.

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
- [ ] `pack/zip` — list and extract entries from a `.zip`/`.taco` with miniz, using
      TmfLib's path normalization (lowercase, `\`→`/`, collapse `//`, trim leading `.`)
- [ ] `pack/attributes` — the attribute table, seeded from `format.md` and filled in
      from the gw2pathing.com attribute pages
- [ ] `pack/model` — Pack / XmlFile / Category / Poi / Trail / Route, with explicit vs
      aggregated attribute resolution
- [ ] `pack/xml_reader` — pugixml -> model, case-insensitive, keeps unknown attributes
- [ ] `pack/trl` — read `.trl`, splitting into segments at `(0,0,0)`
- [ ] Load `gw2-markers` end to end and log a correct count of categories/POIs/trails
- [ ] **Test against third-party packs**, not just `gw2-markers` — TmfLib's lenient
      NanoXml parser implies real packs exist that pugixml will reject

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
2. **Round-trip fidelity.** The biggest open risk. pugixml preserves comments and
   attribute order but not original whitespace/indentation, and it is a *conformant*
   parser where TmfLib is deliberately lenient. Two failure modes to test in M1:
   packs that pugixml refuses to parse at all, and packs that parse but do not
   reserialize byte-identically. Fallback for the second is surgical text edits over the
   original bytes; for the first, lenient parse flags or a sanitizing pass.
3. **FOV.** `Mumble::Identity::FOV` comes from the identity JSON, which Nexus parses;
   confirm whether Nexus exposes a parsed `Identity` via DataLink or whether we parse
   `Mumble::Data::Identity` ourselves.
4. **`.taco` vs `.zip`.** Both are zips. Confirm TaimiHUD and Blish HUD accept both from
   the markers folder.
5. **Routes.** `route` is a real pathable type that TmfLib reads and its writer refuses.
   Decide in M1 whether we model routes properly or just carry them through untouched.

### Resolved

- ~~**Coordinate units.**~~ From `Pathing/ref/hlsl/marker.hlsl`: positions are metres,
  distance attributes are inches, conversion is `/ 0.0254`. Blish's internal Z-up space
  is its own business and not part of the format. Still worth verifying empirically in M3.
- ~~**ImGuizmo pin.**~~ Tag `1.83` compiles against the RaidcoreGG imgui 1.80 fork. Still
  to confirm it *behaves* correctly there once M4 starts.
