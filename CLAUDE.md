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
- **World rendering is ImGui draw lists**, not a custom DX11 pipeline.

## Build environment gotchas

- **Nexus ships imgui 1.80** (RaidcoreGG fork). It predates `BeginDisabled`/`EndDisabled`,
  `ImGuiTableFlags`-era tables, and much else from later versions. Check `imgui.h` in
  `build/_deps/imgui-src/` before reaching for a modern API.
- **Do not define `WIN32_LEAN_AND_MEAN`.** It drops winsock from `<Windows.h>`, and
  gw2relib's `Game/VdfContext.h` uses `IN_ADDR`. `NOMINMAX` is fine and is set globally.
- **CMake 4 rejects `cmake_minimum_required` below 3.5.** miniz's repo CMakeLists trips
  this, which is why `cmake/miniz.cmake` fetches the release amalgamation and declares
  the target by hand instead. Expect the same for other old dependencies.
- `clang-format` is not on PATH; it lives at
  `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`.

## Don't

- Don't add a marker attribute in more than one place — add it to the attribute table.
- Don't silently drop unknown XML attributes or comments on save; packs are hand-authored
  and a lossy round-trip is a bug.
- Don't assume an unzipped folder is loadable by Blish HUD — it isn't, reliably. Zipped
  is the shipping format.
