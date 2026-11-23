# Code style

How code in this repo is written. Shared with VfxDenoiser and gw2-community-challenge-mode on purpose: the
projects share a maintainer and should read alike. The rules on how the addon is structured are in
[architecture.md](architecture.md#rules).

## Formatting and names

VfxDenoiser's `.clang-format` verbatim: Google base, 4 spaces, no tabs, 140 columns. Run it on anything you
touch. It is not on PATH; it ships with Visual Studio at
`<VS install>\VC\Tools\Llvm\x64\bin\clang-format.exe`.

| Thing | Style | Example |
|---|---|---|
| Files, folders, namespaces | `snake_case` | `trail_edit.cpp`, `src/pack/`, `namespace marker_editor` |
| Functions | `camelCase` | `loadPack()`, `drawOptions()` |
| Types / structs / enums | `PascalCase` | `struct MarkerCategory` |
| Variables / members | `snake_case` | `icon_size` |
| Constants | `SCREAMING_SNAKE` | `SETTINGS_FILE` |

**`""` for this repo's headers, `<>` for everything else** — the standard library, Windows, and every
dependency, imgui and Nexus included. Ours is written from `src/`, never relative to the file:
`"pack/model.h"`, `"viewer/camera.h"`.

**Name a function for what it does**: the operation and its subject, `countByType()`, not `judgeTypes()`. No
`judge`, `evaluate`, `process`, `handle`, `manage` — they name a category, not an action. A name that will
not fit usually means the function does two things. Name and comment say what it does, never when it is
called; that belongs to the caller.

**Comments describe the code, never the change.** A comment must read as true had the code always been
this way; why it changed goes in the commit. *now*, *instead*, *otherwise*, *no longer*, *moved* mean it
is narrating.

**A fact lives beside what it is about.** A field's meaning, how a number was found, what was seen in game
or in a pack, why the obvious way does not work: a comment on the thing itself, however long it takes —
as `cmake/imguizmo.cmake` explains why ImGuizmo is built from one file, and `viewer::fromMumble` why it never
takes Nexus's width and height. `docs/` holds processes and how-tos, and points here rather than repeating.
**A comment on a number says where it came from and what bounds it, never the figures that follow from
it**, so tuning the number never leaves the comment wrong.

## Functions read top to bottom

A function reads as one block, so the flow is followed without jumping round the file: each step a short
block under a comment saying what it does.

- **No function called exactly once.** Write its body where it is called. Keep one only when its name
  says more than its lines, as a rule or a lookup does, or when a callback or an `#ifdef` needs one.
- **Duplication is still factored out.** Above all the attribute table: an attribute is defined once, in
  data, and everything about it is generated from that row.
- **A number used in one place is named there**: a `constexpr` local in that block, under the comment
  that says what it means, edited in place — never a bare literal, never a header constant. What one
  file's functions share goes at the top of the .cpp; what another file needs goes in the header, as
  the subdivision cap is in `viewer/camera.h`.
- **Positive conditions**: `if (x) {…} else {…}`, not `if (!x) {…} else {…}`; `if (const T* p = find())`
  rather than a null check that returns.
- **No early returns where a branch does the job**, and the same for `continue` in a loop. Keep one only
  where the alternative reads worse.

## UI

- **imgui is 1.80**, Nexus's, and it cannot be upgraded. It predates `BeginDisabled`, modern tables and
  much else, so check `build/_deps/imgui-src/imgui.h` before reaching for an API. Every other dependency
  has to live with it, which is why ImGuizmo is built the way it is.
- **No text drawn in the game smaller than 20 px.** 16 px is barely readable in game; 20 is the floor,
  for the least important line, and anything the player must read is larger. This is for what is drawn
  into the world or over it, not for our windows, which use Nexus's font at Nexus's size.
- **UI text is `ImGui::Text`.** `TextDisabled` only for what is really off — a control that cannot be
  used, a pack that is not loaded — never to make a heading or a hint quieter. `TextWrapped` only for
  long or variable text, such as an error.
- **An icon in a window is Font Awesome**, as text: `viewer::ui::fonts::label(ICON_FA_TRASH, "Delete")`,
  inside a `fonts::push()`. An icon added to `src/` draws only once `tools/convert_fonts.py` is run
  again, which subsets the font to the names it finds; its docstring says how. Where the font may not
  be built and the icon is the whole control, draw it with `ImDrawList` too, as `eyeButton` in
  `editor/ui/tree_window.cpp` does.
