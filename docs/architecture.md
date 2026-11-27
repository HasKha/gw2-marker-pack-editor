# Architecture

How the editor is built and why, and the layout every library shares. The viewer is [viewer.md](viewer.md);
the format is [format.md](format.md); what is left on the editor is [editor_plan.md](editor_plan.md).

The addon is one DLL: a viewer every player runs, and the editor, started on demand from its menu. The
editor uses `viewer/` for the camera, textures and drawing; nothing in `viewer/` reaches back.

## Rules

- **Nothing may be specific to one pack.** The editor's main use is
  [`gw2-markers`](https://github.com/HasKha/gw2-markers), and it has to work as well on any other.
- **`markus_pack` knows nothing of Nexus, imgui or the game**, which is what lets the format be tested
  offline.
- **An attribute is defined once, in the attribute table** (`pack/attributes.*`). Parsing, serialization,
  the property form and the resources window's reference counts are generated from it; an attribute named
  anywhere else is a second definition.
- **Never block the render thread.** Anything modal or slow, such as the native file dialogs, runs on a
  worker and is polled from `render()`. **No file is read or written on it after load**, not even a stat: a disk
  usually answers at once and now and then takes a tenth of a second, which is a stutter. Hand it to a
  `util::JobQueue` and take the answer on a later frame.
- **One pack open at a time**, the single source of truth in memory; its file is touched only on open and
  save.
- **The parsed XML document is the storage.** `Pack` owns a `pugi::xml_document` per file; the model
  indexes into it, saving serializes it, adding or removing a node is an insert or erase. So `Pack`,
  `Category` and `Pathable` are move-only: never "fix" that with a copy constructor.
- **Nothing a pack author wrote is dropped on save**, no unknown attribute, no comment. Whitespace inside
  tags is the one loss: a pack reformats once, on its first save ([format.md](format.md#what-pugixml-does-not-preserve)).
- **Every edit is an `undo::Step`** with the matching capture inside.
- **Indices into `Pack::categories` and `Pack::pathables` never move**: delete tombstones, add appends. A
  new category grows the per-category indexes through `State::categoryAdded` alone.
- **The viewer draws the editor's pack**, from the live map the editor hands it through
  `EditorHooks::edited_live`; the editor draws only its overlays over it.
- **`src/addon/entry.cpp` stays minimal**: the `AddonDefinition`, the DataLink pointers, the editor's hooks,
  loading the viewer. No feature logic, no globals beyond `AddonDef` and `hSelf`. **Never
  `EAddonFlags_IsVolatile`**: Nexus disables a volatile addon once the game build moves more than 350 past
  the one it recorded (`IsVolatileDisabled` in Nexus's `Host/Addons/Addon.cpp`).
- **Every dependency comes in through FetchContent**, one `cmake/<dep>.cmake` each, pinned, whose comments
  say why it is fetched as it is. Nothing is vendored into `src/`; the font and the game's icons compiled
  in are generated from `assets/`, each folder's `NOTICES.md` saying where they came from.

## Build targets

| Target | Kind | Contents |
|---|---|---|
| `markus_util` | static lib | `src/util/` — paths as UTF-8 text, small JSON files kept between sessions |
| `markus_http` | static lib | `src/http/` — WinHTTP requests on a worker that cancel; `Background`, a one-job thread |
| `markus_pack` | static lib | `src/pack/` — the format. pugixml, miniz, glm |
| `markus_viewer` | static lib | `src/viewer/` — imgui, Nexus, Mumble, GoodRender, RTAPI, sol2 |
| `markus_editor` | static lib | `src/editor/` — ImGuizmo, NFD |
| `Markus` | DLL | `src/addon/entry.cpp` |
| `markus_corpus` | static lib | `test/corpus/` — where the real packs are, shared so the tests cannot disagree about it |
| `markus_probe` | exe | `test/probe/` — prints and lints a pack |
| `markus_{http,pack,viewer,editor}_test` | exe | `test/<library>/` — Catch2, linking that library and nothing above it |

One folder per target, defined by its own `CMakeLists.txt`; `markus_target()` in
`cmake/markus.cmake` takes every source under the folder and builds it at /W4 with warnings as errors;
dependency headers are left out.

**The libraries are layered**: `util` < `http`, `pack` < `viewer` < `editor` < `addon`. Every library has
all of `src/` as its include root, so `markus_layering()` adds a ctest case per folder, `<folder>_layering`,
that fails on an `#include` of a folder above it. A test binary linking one library is the same rule at
link time. Anything in the editor that touches Nexus goes through an `AddonAPI*` that stays null in tests.

## Layout

The viewer's folder is [viewer.md](viewer.md#layout).

```
src/
  addon/entry.cpp      Nexus entry
  util/                path_text.h, json_file.* (lenient reads, all-or-nothing writes),
                       job_queue.* (the worker every file the frame needs goes through),
                       kept_file.h (a file rewritten on it whenever its store moves)
  http/                http.*, background.h
  pack/
    source.*           DataSource: a zip or a folder; path normalization
    zip.*              a .zip/.taco through miniz
    attributes.*       the attribute table
    model.*            Pack / XmlFile / Category / Pathable, explicit vs effective values
    xml_reader.*       pugixml -> model
    writer.*           the document back out, the zip rebuilt
    trl.*              .trl read/write, byte-exact
  viewer/
  editor/
    marker_editor.*    Editor: lifecycle, menu bar, open/save, WndProc, drawing the open pack
    editor_state.*     State: the open Pack, the two selections, visibility, settings, and the
                       history, open trail and resource cache below
    node_ref.h         a node of the tree
    pathable_edit.*    creating and deleting markers and categories
    trail_edit.*       the open trail's working copy (open_trail.h) and what changes it
    undo.*             the undo stack (history.h) and what opens and applies its steps
    file_dialog.*      FileDialog: NFD on a worker
    resources.*        Cache: icons and .trl read from the pack on demand, on a worker of its own
    settings.*         File: settings.json
    ui/                windows.* (the window table), tree_window, property_panel, resource_window,
                       trail_editor
    world/             overlay.* (picking, outlines, point handles), gizmo.*
test/
  corpus/ probe/ http/ pack/ viewer/ editor/
```

## Who holds what

Nexus's callbacks are plain function pointers, so each library is one object behind a static pointer, made
at load and dropped at unload, with forwarding functions for its callbacks: the viewer's `Session` in
`viewer.cpp` (declared in `session.h`, which the options page shares), the `Editor` in `marker_editor.cpp`. Each owns everything it keeps between frames.

The editor's:

- `editor::State`, what belongs to the open pack: the `Pack`, the selections, the undo `History`, the
  `OpenTrail`, and the `resources::Cache` that reads the pack's files. The cache points at `State::pack`, so
  a State cannot be copied or moved; `State::reset` clears all of it but the cache for the next pack.
- What a window keeps of its own between frames, as a `View` (`property_panel`, `trail_editor`) or as the
  window itself (`resource_window::Window`). The tree's reveal lives one frame, so `tree_window::update`
  returns it.
- The settings file, the file dialog, which windows are shown.

The viewer's `Session` holds, beside its packs and stores, the font atlas (`ui::fonts::Fonts`, current
while it lives as an ImGui context is, since windows in both libraries draw with it), the GoodRender
`Subscription`, what a window keeps between frames as its `View` (`pack_catalog`, `map_menu`), and the
recycled scenes, each with the `Workspace` its build works in.

What is left at file scope is one of three things, each commented as such:

- what crosses threads, as atomics (see `onWndProc` below);
- what a callback that is handed nothing else has to reach: RTAPI's events' `AddonAPI*`, GoodRender's
  MumbleLink, the current fonts;
- what the two libraries share, and so outlives a session: the editor's hooks, `TextureUploads`.

A new piece of state goes with the thing it belongs to.

## Easy to get wrong

**The open pack is baked per `State::revision`.** Resolving inherited attributes per marker per frame is
too slow on a 56k-marker pack. Every edit calls `State::touch()`; visibility toggles do not, since they are
view state and `viewer/live/edited.*` compares the flags itself.

**`resources::Cache` decodes a few assets per frame**, so arriving on a map with hundreds of new icons does not
hitch. A miss is cached only when the file is absent; caching "Nexus could not make a texture yet" would
leave a marker a dot until reload.

**Zip saving is `buildZip` then `commitZip`.** The save target is usually the archive still open for
reading, so the caller releases it in between. A failed build never replaces a working pack; the previous
file is kept as `.bak`.

**Actions that reload or close the pack are applied at the end of the frame**: they free what every window
has just drawn from.

**A window's body does not run while it is hidden or collapsed.** State that must stay correct then lives
outside the draw: in a model module ticked from `render()` if anything else needs it (which trail is open:
`trail_edit::followSelection`, since the overlay and gizmo use it), or in `ui::<window>::update(State&)` if
it is the window's own (the tree's pending reveal, which is dropped if the tree is away).

**A panel over a selection is scoped by it.** imgui identifies a field by its label, the same on every node,
so an edit active when the selection changes commits onto the new node. `ui::property_panel::draw` pushes
the primary selection's kind and index; any panel drawing per-selection fields needs the same.

**`onWndProc` runs on the game's message thread**, and the undo and redo binds on one of Nexus's. Neither
touches the `Editor`: they read and write static atomics in `marker_editor.cpp`, which outlive it. Clicks go
to the frame (`pending_select`, `pending_select_point`), and what the handler needs of the frame is published
at its end (`picking`, from the open pack and Select in Viewport, and `mouse_over_ui`, imgui's
`WantCaptureMouse`, which only the render thread may read), as is the overlay's `hovered_pathable`. Anything
new the handler reads has to be published the same way.

**`FileDialog`'s destructor joins with the mutex released**: the worker takes it to store its result.

## Windows

Five, each closable, so the gizmo is never under a big panel: Tree, Details, Toolbar, Trail editor,
Resources. `editor/ui/windows.*` is a table with a row per window; the View menu and settings come from it.
`ui::windows::Shown` is which are on screen, held by the `Editor`. The Tree is the main window: closing it
ends the session.

- **The menu bar is drawn in each large window** from one `drawMenuBar`. The Toolbar has none; the Trail
  menu is only in the trail editor, since it acts on its state.
- **A modal has an owner.** An imgui popup belongs to the window that opened it, so the discard-changes
  confirmation records which window asked, and is dropped if that window closes.

## Settings compare rather than notify

`settings::File::update` runs every frame and writes only when a persisted field moved, so no caller has to
announce a change. It holds the Settings menu toggles, the gizmo mode, which windows are open and where the
file browsers start. Nothing about a pack: those belong to the pack, not the editor. Window positions are
imgui's `imgui.ini`.

## Undo records images, not inverses

A step stores what changed before and after; applying it is assignment. Three kinds of record:

| Record | Before / after | Covers |
|---|---|---|
| attributes | the node's `AttributeCollection` | property panel, gizmo, add attribute, bulk edit |
| existence | the tombstone flag | adding a marker, deleting a marker or category |
| asset | the pending add/remove for one path | added and removed images, every `.trl` edit |

It works because indices never move. Restoring a deleted category puts the kept element back and finds the
descendants' handles inside it again. Steps coalesce by key across consecutive frames, so a drag is one
step; records that change nothing are dropped, and an empty step is popped. After undo the caller prunes
the selection (`editor::prune`) and has the trail editor reread.

## Indices never move

Categories and pathables are indexed positionally by the selection, `State::pathables_by_category`, the
hidden flags, the bake and the trail editor.

- **Deleting tombstones.** `deleted` marks it; everything walking either list skips it, and a missed check
  shows as a deleted thing still doing something. `detached` owns the element, lifted whole into a document
  of its own, so undo is a flag flip and an `insert_copy`. A category's subtree goes with it; its markers
  live in `<POIs>`, so `pathable_edit::remove` deletes them first.
- **Adding appends.** Where it shows is decided separately: in the file by `insert_child_after` on the
  sibling it follows (else the end of a `<POIs>` in the category's file, created if needed), in the tree by
  `State::pathableAdded`. A new category goes inside its parent's declaration; a new root lands in the
  pack's first file. A new trail comes with an empty `.trl`, since a `traildata` naming nothing is a broken
  trail.

## Assets are pending until a save

The `DataSource` is read-only, so `Pack::added_assets` and `removed_assets` hold changes until a save.

- **`resources::Cache` looks at pending adds first.** Nexus returns the existing texture for an identifier
  it has seen, so changing a path's bytes goes through `Cache::forget`, which bumps the generation for a
  fresh name.
- **An added file takes its folder's existing spelling**, or a lowercase path would create `data/icons/`
  beside `Data/icons/`.
- **Only the selected image is previewed**: Nexus never releases a texture.
- What references an image is every attribute of kind `Path`, rebuilt per revision.

## The open trail

`State::open_trail` is the open trail's `.trl` as a working copy shared by its window, the gizmo and the
overlay's handles, read and written through `trail_edit`.

- Points are addressed by a flat index across segments.
- Every change writes the whole trail into `Pack::added_assets` and calls `Cache::forget`; there is no
  separate dirty state.
- Selecting a trail opens it; selecting anything else or closing the trail editor closes it.
- `reindex` runs on every shape change and prunes the point selection.
- A trail whose `.trl` cannot be read closes whatever was open, rather than showing its points under another
  trail's name. `open` tells "still being read, ask next frame" from "tried and failed".
- **Renaming `traildata` moves the `.trl`** (`pathable_edit::attributeRenamed`, once the field is
  committed), since a trail's name is its file. It never overwrites a path the pack has, and never deletes a
  file that shipped.
- `Reload from file` reads the source directly, not waiting for the worker. `Save to file` exports a copy outside
  the pack.

## Two selections

`State::selection` holds categories and pathables; `State::points` the points of the open trail. A point is
a sub-selection inside a selected trail, which one list cannot express.

- `PointSelection` carries its trail index once and drops its points when the trail changes.
- **The gizmo prefers points**, since their trail is always selected too.
- `editor::applyClick` applies plain/ctrl/shift to either; in the viewport shift means ctrl.
- `editor::prune` drops stale nodes after every structural edit; points are pruned by `trail_edit`.

## The gizmo

It sits on the **first** node selected (`Selection` keeps click order), so it does not hop as the
selection grows; the property panel uses the last (`primary()`). A drag is one transform about the anchor:

```
p' = anchor' + R (p - anchor)      orientation' = R · orientation
```

so a group rotation orbits the others, writing position and `rotate`. That stamps `rotate` on billboards,
turning them into fixed quads. `R` is recovered each frame from attributes written to six significant
digits, so a long drag drifts a little. Trails are skipped: their geometry is in the `.trl`. Placement goes
through `viewer::markerTransform`, the bake's, so handles sit on the drawn icon.

## The attribute table

Name, spelling, value kind, default, whether it inherits. Unknown attributes are kept verbatim and shown as
raw text fields. **`applies_to`** is which node kinds consume an attribute (the "does nothing here" warning,
the probe's lint); **`settableOn`** is where it may be written: `applies_to` plus categories whenever it
inherits ([format.md](format.md#a-category-carries-almost-every-attribute)). Filtering attributes by node
kind wants `settableOn`.

## A pack is a zip or a folder

`DataSource` has a zip and a directory reader, as TmfLib's `IDataReader`. A folder is so a pack's source
tree (a git checkout) can be edited in place; it is not shippable, since Blish HUD does not reliably load
unzipped packs. Both normalize paths alike. `Pack::from_directory` decides how a save is written; Save As
always writes a zip and clears it. The directory reader skips dot-directories.

## The tree is categories

XML files are an authoring convenience that every loader merges. Of 31,817 categories across the corpus,
one is declared in more than one file (a wrapper root), so the tree shows categories only. A pathable
records its file; a category records all its declarations. Pathables whose `type` resolves nowhere appear
under "Unresolved".

## Drawing

The viewer draws the open pack ([viewer.md](viewer.md#the-editor-inside)). `editor/world/` draws only the
outline, picking, point handles, a dot where an icon is missing, and the gizmo, on the draw list over
everything, using `viewer/render/overlay.*` and `viewer/game/camera.*`. With the full map open the editor clears
its hover state, so a click on the map selects nothing in the world.
