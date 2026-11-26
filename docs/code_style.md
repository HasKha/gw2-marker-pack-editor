# Code style

How code in this repo is written. How the addon is structured is [architecture.md](architecture.md#rules).

## Formatting and names

`.clang-format` at the root: Google base, 4 spaces, no tabs, 140 columns. Run it on anything you touch. It
is not on PATH; it ships with Visual Studio at `<VS install>\VC\Tools\Llvm\x64\bin\clang-format.exe`.

| Thing | Style | Example |
|---|---|---|
| Files, folders, namespaces | `snake_case` | `trail_edit.cpp`, `src/pack/`, `namespace marker_editor` |
| Functions | `camelCase` | `loadPack()`, `drawOptions()` |
| Types / structs / enums | `PascalCase` | `struct MarkerCategory` |
| Variables / members | `snake_case` | `icon_size` |
| Constants | `SCREAMING_SNAKE` | `SETTINGS_FILE` |

**`""` for this repo's headers, `<>` for everything else**, dependencies included. Ours are written from
`src/`, never relative to the file: `"pack/model.h"`.

**Name a function for what it does**: `countByType()`, not `judgeTypes()`. No `judge`, `evaluate`,
`process`, `handle`, `manage`. A name that will not fit usually means the function does two things. Name and
comment say what it does, never when it is called; that belongs to the caller.

## Comments

- **Short, and only what the code cannot say**: why, what bounds a number, what was measured in a pack or
  seen in game, why the obvious way does not work. Never restate the code.
- **Describe the code, never the change.** A comment must read as true had the code always been this way;
  history goes in the commit. *now*, *instead*, *no longer*, *used to* mean it is narrating.
- **A fact lives beside what it is about**, as a comment on the thing itself. `docs/` holds processes and
  how-tos and points at the code rather than repeating it.
- **A comment on a number says where it came from and what bounds it**, never the figures that follow from
  it, so tuning the number never leaves the comment wrong.

## Functions read top to bottom

Each step is a short block under a comment saying what it does.

- **No function called exactly once.** Write its body where it is called, unless its name says more than
  its lines, or a callback or an `#ifdef` needs one.
- **Duplication is still factored out.** Above all the attribute table: an attribute is defined once, in
  data, and everything about it is generated from that row.
- **A number used in one place is named there**: a `constexpr` local under the comment that says what it
  means, never a bare literal or a header constant. What one file shares goes at the top of the .cpp; what
  another file needs goes in the header.
- **Positive conditions**: `if (x) {…} else {…}`, not `if (!x)`; `if (const T* p = find())` rather than a
  null check that returns.
- **No early returns or `continue` where a branch does the job**, unless the alternative reads worse.

## C++

- **C++20 on MSVC, at /W4 with warnings as errors.** A parameter a callback has to take but does not use
  goes unnamed.
- **No mutable state at file scope.** It belongs to the object whose lifetime it shares
  ([architecture.md](architecture.md#who-holds-what), which lists the few exceptions). What crosses
  threads is an atomic, with a comment naming the threads. A list kept only so a build does not allocate
  goes with what is built, as `Scene::Workspace` does.
- **Return a value, not an out-parameter.** A result that may be missing is `std::optional`. An
  out-parameter is for a buffer the caller reuses, or an error string beside a `bool`.
- **`[[nodiscard]]` on a function whose answer says whether it worked**, and on a parser. A failure that
  really does not matter is dropped with `static_cast<void>` under a comment saying why.
- **Text is `std::string`.** An imgui field takes one through `imgui_stdlib.h`, and text is put together
  with `std::format`; `std::to_chars` into a stack buffer where the shortest round trip is the point. A fixed
  buffer only where Win32 asks for one.
- **A COM object is held in `Microsoft::WRL::ComPtr`**, never released by hand.
- **Members are torn down in reverse declaration order**, so what other members' destructors still use is
  declared first.

## UI

- **imgui is 1.80**, Nexus's, and cannot be upgraded. It predates `BeginDisabled`, modern tables and much
  else, so check `build/_deps/imgui-src/imgui.h` before reaching for an API.
- **No text drawn in the game smaller than 20 px**, the floor for the least important line. This is for what
  is drawn into or over the world, not our windows, which use Nexus's font.
- **UI text is `ImGui::Text`.** `TextDisabled` only for what is really off (a control that cannot be used, a
  pack not loaded), never to quieten a heading; quieter text is `style::muted()`. `TextWrapped` only for long
  or variable text.
- **Colours and shared widgets come from `viewer/ui/style.h`**, the editor's windows too: the palette,
  `beginDisabled` for greying out a block, buttons of one width for a group, one field width for a form, a
  link. A colour new to the UI is added there, never as a literal where it is drawn.
- **Title Case** for window titles, section headers, tabs, buttons and menu entries ("Import from Blish HUD...",
  "Manage Packs"); **sentence case** for checkbox, slider and dropdown labels, dropdown choices, tooltips and
  messages ("Show markers on maps"). A label ends in "..." when its action asks for more before it acts.
- **An icon in a window is Font Awesome**, as text: `viewer::ui::fonts::label(ICON_FA_TRASH, "Delete")`,
  inside a `fonts::push()`. An icon new to `src/` draws only once `scripts/convert_fonts.py` is run again; its
  docstring says how. Where the icon is the whole control and the font may not be built, draw it with
  `ImDrawList`, as `eyeButton` in `editor/ui/tree_window.cpp` does.
