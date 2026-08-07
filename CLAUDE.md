# MarkerPackEditor — project instructions

A Nexus (GW2) addon DLL for creating and editing marker packs.

Read [docs/plan.md](docs/plan.md) before starting work. The docs have one job each —
keep them that way rather than letting any one of them grow into a catch-all:

| Doc | Holds | Does not hold |
|---|---|---|
| [docs/plan.md](docs/plan.md) | goal, current state, milestones, open questions | how anything works |
| [docs/architecture.md](docs/architecture.md) | decisions and why, dependencies, module layout, rendering approach, threading | the roadmap, format facts |
| [docs/format.md](docs/format.md) | the marker pack format, sourced from the reference implementations | our design |
| [docs/references.md](docs/references.md) | clone paths and links to the reference implementations | the facts themselves |

Consult `format.md` before writing any parsing, serialization or rendering code.

## Format ground truth

When implementations disagree about the pack format, precedence is **TmfLib** (parsing)
→ **Pathing** (what attributes do) → gw2pathing.com (attribute reference) → **TaimiHUD**
(partial; only where the others are silent). Paths and the files worth reading are in
[docs/references.md](docs/references.md).

One exception: **do not model our writer on TmfLib's `PackWriter`.** It is a
distribution re-packer that flattens the file structure and discards comments. We are an
editor and must round-trip hand-authored packs. See `docs/format.md`.

## Memory policy — read this first

**Do not write anything to Claude's system/user memory directory.** No files under
`~/.claude/.../memory/`, no `MEMORY.md` entries, nothing outside this repo.

Everything worth remembering lives in the repo instead:

- **`CLAUDE.md`** (this file) — durable conventions, architecture, and rules for
  working in this project.
- **`docs/`** — everything else, split by the table above. `docs/plan.md` is the living
  roadmap and must be updated as work progresses: tick off completed items, move
  resolved questions, add newly discovered constraints. New docs get added when an area
  grows enough to need one — not by widening an existing doc's remit.

If you learn something durable, put it in one of those two places in the same change
that taught it to you.

## Conventions

Formatting is enforced by `.clang-format` (Google base, 4 spaces, no tabs, 140
columns). Run clang-format on anything you touch.

Style:

| Thing | Style | Example |
|---|---|---|
| Files | `snake_case` | `pack_loader.cpp` |
| Namespaces | `snake_case` | `namespace marker_editor` |
| Functions | `camelCase` | `loadPack()`, `drawOptions()` |
| Types / structs / enums | `PascalCase` | `struct MarkerCategory` |
| Variables / members | `snake_case` | `icon_size` |
| Constants | `SCREAMING_SNAKE` | `SETTINGS_FILE` |

Other rules:

- **All dependencies come in via FetchContent**, one `cmake/<dep>.cmake` module each,
  pinned to a tag or commit. Never vendor a dependency into `src/`.
- **No copy-paste.** If two places need the same logic, factor it out. This applies
  especially to the marker attribute table — attributes are defined once, in data, and
  parsing / serialization / the property form are all generated from that one table.
- Each module is a `snake_case` namespace with a matching `.h`/`.cpp` pair. The public
  header exposes only what other modules need.
- `src/entry.cpp` stays minimal: it fills in the `AddonDefinition`, resolves the
  DataLink pointers, and hands them to `marker_editor::load()`. No feature logic there.

## Architecture

Full picture in [docs/architecture.md](docs/architecture.md). The invariants that must
hold in any change:

- **Never block the render thread.** Anything modal or slow (native file dialogs today,
  pack loading later) runs on a worker and is polled from `render()`.
- **One pack open at a time.** The loaded pack is the single source of truth in memory;
  the zip on disk is only touched on open and on save.
- **The parsed XML document is the storage.** `Pack` owns a `pugi::xml_document` per file
  for its whole lifetime; the model indexes into it, saving serializes it, and adding or
  removing a marker is a node insert or erase. `Pack` is move-only as a result.
- **World rendering is ImGui draw lists**, not a custom DX11 pipeline.

## Build environment gotchas

- **Nexus ships imgui 1.80** (RaidcoreGG fork). It predates `BeginDisabled`/`EndDisabled`,
  `ImGuiTableFlags`-era tables, and much else from later versions. Check `imgui.h` in
  `build/_deps/imgui-src/` before reaching for a modern API. This is fixed by Nexus — we
  cannot upgrade it, so every other dependency has to live with it.
- **ImGuizmo is built gizmo-only, on purpose.** Its CMakeLists compiles all six widgets in
  the repo as one target and four of them require imgui 1.90+. `cmake/imguizmo.cmake`
  declares a target over `src/ImGuizmo.cpp` alone and defines `ImDrawFlags_Closed=1` for
  it — the one thing the gizmo needs from imgui 1.82+, and numerically identical to the
  `bool closed` argument 1.80 expects. ImGuizmo itself carries `IMGUI_VERSION_NUM` guards
  for old imgui, so the gizmo is genuinely supported there.
- **An ImGui draw list has no w.** `AddImageQuad` is two triangles with affine UVs, so a
  projected quad whose corners sit at different depths gets its texture bent along the
  diagonal instead of foreshortened. Anything textured and projected has to be subdivided in
  world space — see `drawTexturedQuad` in `world/renderer.cpp` and `docs/architecture.md`.
- **`NexusLinkData::Width/Height` is not the viewport.** It is the DXGI swap chain's size,
  written only from Nexus's `ResizeBuffers` hook, and it goes stale when the game builds a
  new swap chain. The draw list is rasterized against `ImGui::GetIO().DisplaySize`; project
  against anything else and the world comes out stretched along one axis.
- **A click on the world never reaches ImGui.** `Nexus/src/UI/UiInput.cpp` only sets
  `io.MouseDown` when `io.WantCaptureMouse` is *already* true, so `ImGui::IsMouseClicked`
  can never fire for a click outside an addon window. Anything that reacts to a world
  click must register a `WndProc` callback (`api->WndProc.Register`) and read
  `WM_LBUTTONDOWN` itself. Returning 0 from that callback consumes the message; return
  non-zero to let the game have it.
- **Nexus never releases a texture.** `Textures.GetOrCreateFromMemory` has no counterpart,
  so textures live for the session. `resources.cpp` puts a generation counter in each
  identifier so two packs cannot collide; the cost is that reopening packs accumulates
  them. Fine for an editor, worth revisiting if it ever isn't.
- **Icon fonts cannot reach the atlas.** Nexus's `CFontManager` rebuilds every font with
  its *own* glyph ranges (Latin + Latin-Ext, plus CJK/Cyrillic for `FONT_DEFAULT`), so
  FontAwesome's `0xF000-0xF8FF` glyphs are dropped whatever we pass. It also calls
  `AddFontFromMemoryTTF`, i.e. raw TTF, while `HasKha/imgui-fonts` ships *compressed*
  data. Draw icons with `ImDrawList` primitives instead — see `eyeButton` in
  `ui/tree_window.cpp`.
- **CMake 4 rejects `cmake_minimum_required` below 3.5.** miniz's repo CMakeLists trips
  this, which is why `cmake/miniz.cmake` fetches the release amalgamation and declares
  the target by hand instead. Expect the same for other old dependencies.
- **Nexus's update check is picky about the release.** It reads the repo's GitHub releases,
  keeps the newest `tag_name` that parses as a version *and* carries an asset whose name
  ends in `.dll`, and offers it only when that is greater than the version compiled into
  `AddonDefinition`. Its parser **throws on a two-component tag** (`v1.0`) and the throw is
  swallowed, so such a release is skipped in silence. Tag `vMAJOR.MINOR.BUILD`, matching
  `VERSION_*` in `CMakeLists.txt`, and attach the DLL itself.
- **`EAddonFlags_IsVolatile` is for addons that hook the game.** We are not one. Nexus
  refuses to load a volatile addon once the game build has advanced more than 350 past the
  one it last recorded, and the user has to re-enable it by hand — a self-inflicted outage
  after every large patch. See `Nexus/src/Host/Addons/Addon.cpp`, `IsVolatileDisabled`.
- `clang-format` is not on PATH; it ships with Visual Studio at
  `<VS install>\VC\Tools\Llvm\x64\bin\clang-format.exe`.

## Don't

- Don't add a marker attribute in more than one place — add it to the attribute table.
- Don't silently drop unknown XML attributes or comments on save; packs are hand-authored
  and losing either is a bug. Whitespace is the one thing we *do* give up — a pack
  reformats once on its first save, deliberately. See `docs/architecture.md`.
- Don't verify pack-layer work by launching the game, and don't verify it by hand either.
  `ctest --test-dir build -C Release` runs the whole read → model → write → save round trip
  over every real pack in under two seconds. Use it first, and **add to it**: a fix without
  a case that fails before it is a fix that comes back. `mpe_probe` is still the right tool
  for *looking* at a pack — it prints and lints where `mpe_test` asserts.
- Don't assume an unzipped folder is loadable by Blish HUD — it isn't, reliably. Zipped
  is the shipping format.
