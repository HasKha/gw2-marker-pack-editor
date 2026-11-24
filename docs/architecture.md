# Architecture

How the editor is built and why. For the pack format itself see [format.md](format.md);
for what is done and what is next see [plan.md](plan.md); for how code is written,
[code_style.md](code_style.md).

The addon is becoming two halves in one DLL: a viewer that every player runs, and this
editor behind a setting. The viewer, and the `viewer/` library that sits between `pack/` and
`editor/` with the dependency running one way, are [viewer.md](viewer.md). The editor reaches
into `viewer/` for the camera, textures and the draw-list quad path; nothing in `viewer/`
reaches back. What follows describes the editor.

## Rules

The invariants every change keeps. The sections below are why.

- **`markus_pack` knows nothing of Nexus, imgui or the game.** `src/pack/` links only `markus_util`,
  pugixml, miniz and glm, which is what lets the whole format be tested offline. Never add an
  addon header to it.
- **An attribute is defined once, in the attribute table.** Parsing, serialization, the
  property form and the resources window's reference counts are all generated from it; an
  attribute named anywhere else is a second definition. See
  [The attribute table](#the-attribute-table-is-the-core-abstraction).
- **Never block the render thread.** Anything modal or slow — native file dialogs today, pack
  loading later — runs on a worker and is polled from `render()`.
- **One pack open at a time.** The loaded pack is the single source of truth in memory; the
  zip on disk is touched only on open and on save.
- **The parsed XML document is the storage.** `Pack` owns a `pugi::xml_document` per file for
  its whole lifetime; the model indexes into it, saving serializes it, and adding or removing a
  marker is a node insert or erase. `Pack` is move-only as a result, and so are `Category` and
  `Pathable`, since a tombstone owns a document: never "fix" that with a copy constructor.
- **Nothing a pack author wrote is dropped on save** — no unknown attribute, no comment.
  Whitespace is the one thing given up: a pack reformats once, on its first save. See
  [Saving serializes](#saving-serializes-it-does-not-diff).
- **Every edit is an `undo::Step`** with the matching capture inside, or it is the one edit
  Ctrl+Z cannot take back. See [Undo](#undo-records-images-not-inverses).
- **The viewer draws the editor's pack**, as it draws an installed one, from the live map the
  editor hands it through `EditorHooks::edited_live`; the editor draws only what an editor
  needs over it. `render` is not a Nexus callback: the viewer's render calls it through
  `EditorHooks`, and it does nothing while the editor does not run (see
  [viewer.md](viewer.md#the-editor-inside)).
- **Nothing is erased from `Pack::categories` or `Pack::pathables`**, and nothing is inserted
  into their middle: delete tombstones, add appends. Every index into them stays valid. A new
  category also grows the per-category indexes, through `State::categoryAdded` alone.
- **World rendering is ImGui draw lists**, not a pipeline of our own. See
  [Rendering](#rendering).
- **Each module is a `snake_case` namespace** with a matching `.h`/`.cpp` pair, and its header
  exposes only what other modules need.
- **`src/addon/entry.cpp` stays minimal**: it fills in the `AddonDefinition`, resolves the DataLink
  pointers, connects the editor's hooks to the viewer and loads the viewer. The editor's own
  `load` registers nothing with Nexus. No feature logic, no globals beyond `AddonDef` and `hSelf`. **Its flags never include `EAddonFlags_IsVolatile`**, which is for
  addons that hook the game: Nexus refuses to load a volatile addon once the game build is
  more than 350 past the one it last recorded, until the player re-enables it by hand
  (`IsVolatileDisabled` in Nexus's `Host/Addons/Addon.cpp`).
- **Every dependency comes in through FetchContent**, one `cmake/<dep>.cmake` each, pinned to a
  tag or commit. Nothing is vendored into `src/`: the one font compiled in is generated there from
  `assets/fonts/`, whose `NOTICES.md` holds its licence and source.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Dependencies | FetchContent only, one `cmake/*.cmake` per dep | nothing vendored into `src/` |
| entry.cpp | oops-wrong-template's minimal form | no globals beyond `AddonDef`/`hSelf`, no feature logic |
| World rendering | ImGui draw list + hand-built matrices | see [Rendering](#rendering) |
| Tree top level | categories only; XML files never appear as nodes | see [The tree is categories](#the-tree-is-categories) |
| Packs open | exactly one at a time | keeps the model a single source of truth |
| UI shape | several small windows, not one big one | see [The UI is windows](#the-ui-is-windows) |
| Group transform | anchored on the first node selected; a rotation orbits the rest | see [The gizmo anchor](#the-gizmo-anchor) |
| Shipping format | zip in, zip out | Blish HUD fails to load images from unzipped folders |

## Dependencies

Carried over from the templates: `imgui` (RaidcoreGG fork), `mumble`, `nexus`,
`nlohmann_json`. (`gw2relib` came with them too and was dropped before 1.0 — nothing here
reads the game, so it was a compiled dependency nothing included.)

New for this project:

| Dep | Purpose |
|---|---|
| `pugixml` | XML read/write; keeps comments and attribute order as real nodes |
| `miniz` | zip read/write |
| `nativefiledialog-extended` | native open/save dialogs |
| `ImGuizmo` | 3D translate/rotate/scale gizmo |
| `glm` | vec3/mat4 for the projection and the matrices ImGuizmo consumes |
| `Catch2` | test framework. **Test-only** — nothing in `src/` links it |

## Build targets

Thirteen, because the code worth testing outside the game is most of it:

| Target | Kind | Contents |
|---|---|---|
| `markus_util` | static lib | `src/util/` — paths as UTF-8 text, the small JSON files kept between sessions. Links nlohmann_json, **includes nothing else of ours** |
| `markus_http` | static lib | `src/http/` — requests on a worker through WinHTTP, and `Background`, the one-job thread. Links `markus_util` |
| `markus_pack` | static lib | `src/pack/` — the format. Links `markus_util`, pugixml, miniz, glm. **Knows nothing about Nexus, imgui or the game.** |
| `markus_viewer` | static lib | `src/viewer/` — the viewer. Links `markus_pack` and `markus_http` plus imgui, Nexus, Mumble, GoodRender, sol2. **Never the editor** |
| `markus_editor` | static lib | `src/editor/` — the editor. Links `markus_viewer` plus ImGuizmo, NFD |
| `Markus` | shared lib | `src/addon/` — `entry.cpp` and nothing else. Links `markus_editor` |
| `markus_corpus` | static lib | `test/corpus/` — where the real packs are. Shared by the test targets so they cannot disagree about it |
| `markus_probe` | console exe | `test/probe/` — the harness that prints. Links `markus_pack` |
| `markus_http_test` | console exe | `test/http/` — requests that cancel, and `Background`. Links `markus_http` and nothing else of ours |
| `markus_pack_test` | console exe | `test/pack/` — the format's assertions. Links `markus_pack` and not the viewer |
| `markus_viewer_test` | console exe | `test/viewer/` — the viewer's, and the corpus sweep, which checks the format and the bake in one pass. Links `markus_viewer` and not the editor, which is the proof it can |
| `markus_editor_test` | console exe | `test/editor/` — the editor's. Links `markus_editor` |

**One folder per target, and its `CMakeLists.txt` defines it.** The root `CMakeLists.txt` brings
in the dependencies and adds the folders, from the bottom of the layering up; a folder's own file
names its target, what it links, and what it may include. `markus_target_sources()` in
`cmake/markus.cmake` takes every `.h` and `.cpp` under the folder, so a file's folder says which
target it is in and no target is described by subtracting another's sources.

**The libraries are layered, and the build holds it**: `util` < `http`, `pack` < `viewer` <
`editor` < `addon`, each including only what is below it. Every library sees all of `src/` as its
include root, so a header from above would still compile; `markus_layering()` registers a ctest
case per folder, `<folder>_layering`, that fails on an `#include` of a folder it does not list.
A test binary that links one library and nothing above it is the same rule checked at the link.

`markus_editor` exists so `markus_editor_test` links the code the addon runs rather than naming source
files a second time. Only the objects a test reaches get linked in, so nothing there needs
a live Nexus or an imgui context; what does touch Nexus goes through an `AddonAPI*` that
stays null, which `editor/resources.cpp` already has to handle for a pack made in the
editor. The windows and the overlay are in the library and simply never pulled in.

Why there are two test targets, and what they are pointed at, is [testing.md](testing.md).

## Module layout

```
src/
  addon/entry.cpp      — Markus.dll
                       Nexus entry, AddonDefinition, loads the viewer and, when enabled, the editor
  util/                — markus_util: path_text.h (paths as UTF-8 text), json_file.* (lenient
                       reads, all-or-nothing writes of small JSON files)
  http/                — markus_http: http.* (GETs and downloads that cancel), background.h
  pack/                — markus_pack, no Nexus/imgui dependency
    source.*           DataSource: a pack is a zip OR a folder. Path normalization.
    zip.*              DataSource over a .zip/.taco, via miniz
    attributes.*       THE attribute table: name, kind, default, inheritance
    model.*            Pack / XmlFile / Category / Pathable, explicit vs effective values
    xml_reader.*       pugixml -> model; the document is kept and owned by the Pack
    writer.*           serializes the document back out, rebuilds the zip (format.md)
    trl.*              .trl read/write, byte-exact
  viewer/              — markus_viewer; see viewer.md for what it grows into
  editor/              — markus_editor
    marker_editor.*    lifecycle, the windows and the shared menu bar, save/open, WndProc,
                       drawing the open pack
    pathable_edit.*    creating and deleting markers and categories
    undo.*             the undo/redo stack: before/after images of what changed
    trail_edit.*       the open trail's working copy, shared by its window and the gizmo
    editor_state.*     State: the open Pack, the two selections and the rules over them,
                       per-node visibility, settings
    file_dialog.*      NFD on a worker thread (never block the render thread)
    resources.*        icons + .trl pulled from the pack source on demand, budgeted;
                       the images through viewer::TextureCache
    settings.*         the few things that outlive a session, as settings.json
    ui/
      windows.*        the window registry: titles, visibility, View menu
      tree_window.*    category tree, selection (ctrl / shift range), per-node eye toggles
      property_panel.* attribute form + "add attribute" + inherited values
      resource_window.* the pack's images, what references them, add / remove
      trail_editor.*   the open trail: map id, point list, the Trail menu, shape tools
    world/
      overlay.*        over the viewer's scene: hit testing, the selection outline, point
                       handles, a dot where an icon is missing
      gizmo.*          ImGuizmo translate/rotate over the selection, writes to attributes
test/
  corpus/              where the real packs are
  probe/               the harness that prints
  http/ pack/ viewer/ editor/
                       the assertions, a binary each, over that library and what is below it
```

## Things that are easy to get wrong

Three mechanisms that exist for non-obvious reasons.

**The open pack is baked per `State::revision`.** Resolving inherited
attributes walks the category chain, and the largest pack tested has 55,887 markers — doing
that per marker per frame is not viable inside the render callback. Any edit calls
`State::touch()`, which bumps the revision, and `viewer/edited.*` bakes again. Visibility
toggles deliberately do *not* bump it: they are view state, not edits, and `edited.*` compares
the flags themselves.

**`resources` decodes at most a few assets per frame.** Arriving on a map with hundreds of
unseen markers would otherwise decode them all in one frame and hitch. A miss is only
cached when the file is genuinely absent — caching a "Nexus could not make a texture yet"
result is what once made every marker fall back to a dot until the pack was reloaded.

**Zip saving is two calls, `buildZip` then `commitZip`.** The usual save target *is* the
pack still open for reading, and Windows will not rename over a live handle, so the caller
has to release the source in between. Building first also means a failure never replaces a
working pack with a half-written one; the previous file is kept as `.bak`.

## The UI is windows

One window holding both the tree and the details forced the gizmo into whatever screen
that window was not covering. The editor is instead five independent windows, each
closable, each remembering its own place:

| Window | Contents |
|---|---|
| Tree | pack header, category hierarchy, selection, visibility toggles |
| Details | the attribute form for the selection |
| Toolbar | gizmo mode, Add POI, and the create/place controls as they arrive |
| Trail editor | the open trail's map id and points, with drag fields for the selected one |
| Resources | the pack's images: folder tree, references, preview, add / remove |

`editor/ui/windows.*` is the registry: one table row per window with its title, its menu label
and its default visibility, and everything else — the View menu, the settings file — is
generated from that table. The Tree is the main window: closing it ends the session, and the
others are drawn only while it is open. Adding a window
is one row plus its draw function, and an `update` if it needs one — see
[A window that is not drawing](#a-window-that-is-not-drawing).

Two consequences of every other window being closable:

- **The menu bar is drawn in each large window**, from one shared `drawMenuBar`, so File
  and View are reachable from whichever one is open. The Toolbar has no menu bar; a bar
  would be taller than its contents. The one exception to "the same bar everywhere" is the
  Trail menu, which is drawn last and only in the trail editor, because it acts on that
  window's own state.
- **A modal has an owner.** An ImGui popup is scoped to the window that opened it, so the
  discard-changes confirmation records which window's menu asked for it and is only begun
  there. If that window is closed while the modal is up, the pending action is dropped
  rather than left armed.

Actions that reload or close the pack are applied at the *end* of the frame. They free the
vectors every window has just drawn from, and a menu click happens partway through that.

### A panel over a selection is scoped by that selection

The property panel identifies each field by its attribute name, which is the same name on
every node — so to imgui one node's `xpos` box and the next node's are one widget, and an
edit still active when the selection changes commits onto whatever is now selected. Typing
a new `traildata` on one trail and clicking the other left the second pointing at the
first's file.

`ui::property_panel::draw` pushes the primary selection's kind and index around everything
it draws. Any other panel that draws same-named fields for whatever is selected needs the
same.

### A window that is not drawing

A window's body does not run when it is hidden — and, easy to forget, not when it is merely
*collapsed* either: `ImGui::Begin` returns false and `beginWindow` gives up before the body.
Anything that has to stay correct across that has to run outside it.

Editing `State` directly from a draw is fine and is the whole point of immediate mode. The
question is only what happens on the frames where the draw does not happen. Two answers,
and which one applies is decided by **lifetime**:

| The state | Where it goes |
|---|---|
| Outlives the window — another subsystem would still need it if the window were deleted | a model module, ticked from `render()` |
| The window's own, but has to stay correct while it is not drawing | `ui::<window>::update(State&)`, called from `render()` |

Two live examples, one of each:

- **Which trail is open** fails the lifetime test: the world overlay draws its handles and
  the gizmo targets its points, and neither is the trail editor. So `followSelection` lives
  in `trail_edit` and runs from `render()`. It used to run from the window body, and a
  collapsed trail editor would then keep an unselected trail open — handles still in the
  world, gizmo still on its points.
- **The pending reveal** passes it: `reveal_active` and the ancestor chain are the tree's
  alone. So `ui::tree_window::update` takes the request off `State` and works out what to
  open, whether or not the tree draws afterwards. Consuming and dropping it when the tree is
  away is the point — a reveal is about the moment it was asked for, and one replayed later
  scrolls to a node picked minutes ago.

`update()` is added where it has something to do, not as a stub on every window: an empty
one invites model state into a window, which is the problem it exists to prevent. If a third
window grows one, the calls are worth driving from the `editor/ui/windows.*` table — which already
carries a row per window — rather than by hand. A base class is the wrong shape for it: the
draw signatures genuinely differ (a camera here, a returned request there, the Toolbar with
no module at all), so a shared virtual would either widen every signature or keep the side
channels that motivated it.

A window closed with its X comes back from the main window's View menu. The addon's options
page is the viewer's alone; the editor has nothing on it.

## One trail, one working copy

`trail_edit` holds the trail being edited as a mutable copy of its `.trl`, and it is a
module of its own rather than part of the trail editor window because the window is not the
only thing editing it: the gizmo drags the same points, and the overlay draws handles for
them. All three go through the one model.

- **Points are addressed by a flat index** across segments — a `.trl` is one or more runs of
  points separated by `(0,0,0)` terminators, and the editor lists them as one numbered list
  with a mark where a new run begins.
- **Every change writes the whole trail back** into `Pack::added_assets`, the same pending
  mechanism the resources window uses, and calls `resources::forget` so the next frame
  redraws from the new bytes. There is no separate "trail is dirty" state and no save step
  of its own: a pack save picks the trail up with everything else. Reserializing a few
  thousand points per edited frame is nothing next to a frame of rendering.
- **Points are their own selection**, `State::points`, not entries in `State::selection` —
  see [Two selections](#two-selections).
- **Only the open trail gets handles** in the world. Every trail's points at once would be
  an unreadable field of dots, and a point that cannot be seen cannot be clicked.
- **"Open" tracks the selection and the window, both ways.** Selecting a trail opens it;
  selecting anything else closes it; closing the trail editor closes it too. Both halves run
  from `marker_editor::render`, against `trail_edit::followSelection` — *not* from the trail
  editor's window body. A window that is hidden draws nothing, and one that is merely
  collapsed draws nothing either, so anything that keeps model state in step has to run
  outside it. Left inside, a collapsed trail editor would keep an unselected trail open:
  handles in the world, and the gizmo still on its points. Dropping the working copy costs
  nothing — every edit is already committed to the pack — and reopening reads the same bytes
  back.
- **`reindex` prunes the point selection.** It runs on every change to a trail's shape and
  is the only thing that knows the new one, which is what lets everything downstream treat a
  selected point index as one that exists.
- **A trail that cannot be opened closes whatever was.** `resources::trail` caches a failed
  read as a miss, so a `traildata` naming a file the pack does not have never comes good.
  Leaving the previous trail open over that showed one trail's points under another trail's
  name — two differently named trails looking like they shared a point list. `open` therefore
  tells "over budget, ask next frame" from "tried and failed" and closes on the second.
  Nothing records *why*: a trail selected with nothing open is a trail whose data is missing,
  which is what the window says.

### Renaming traildata moves the .trl

A trail has no name of its own — the tree labels it with its `.trl`'s basename, the trail
editor shows the path — so renaming `traildata` is how a trail gets named, and the attribute
is explicit-only ([format.md](format.md)) so no category can carry a name instead.

Left alone, that rename points the trail at a file the pack does not have and strands its
points under the old path. `pathable_edit::attributeRenamed` is the hook the property panel
calls after every attribute edit; `traildata` on a trail is the one case it acts on. It never
writes over a path the pack already has — that is a re-point, and sharing one `.trl` between
trails is legal — and never deletes a file that shipped in the pack.

It runs once the field is committed, not per keystroke, because it moves a file and
`InputText` reports every character. That needs the value the field started from, which the
panel keeps between `IsItemActivated` and `IsItemDeactivatedAfterEdit`.

`Trail > Reload from file` drops the pending bytes and rereads the source **directly**
rather than through `resources`, whose reads are budgeted a few per frame: a menu action
that silently does nothing because the budget was spent is worse than one extra read.
`Save to file` exports a copy outside the pack — for a zip pack there is no other coherent
meaning, since a single `.trl` cannot be written back into the archive on its own.

## Settings compare rather than notify

`settings::update` runs every frame and writes only when something it persists has moved.
Nothing calls it to announce a change, which means a setting added later cannot be added to
the file and then forgotten at the six places that might change it. The comparison is field
by field against the last written values rather than against a rebuilt snapshot, so the
common case — nothing happened — allocates nothing.

What is persisted is small on purpose: the Settings menu toggles, the gizmo mode, which
windows are open, and where the two file browsers should start. **Nothing about a pack**
goes in there. Visibility toggles, the selection and the open trail all belong to a pack
rather than to the editor, and a settings file that remembered them would be wrong the
moment a different pack was opened. Window *positions* are ImGui's business and already
live in its own `imgui.ini`.

## Undo records images, not inverses

`undo` stores what a thing looked like before and after, and applying a step is assignment.
Nothing computes an inverse operation, because that is where undo systems go wrong: an
inverse has to be re-derived correctly for every edit, forever.

There are only three kinds of record, and every edit in the editor is made of them:

| Record | Before / after | Covers |
|---|---|---|
| attributes | the node's whole `AttributeCollection` | the property panel, the gizmo, add attribute, bulk edit |
| existence | the tombstone flag | adding a marker, deleting a marker or a category |
| asset | the pending add/remove for one path | added images, removed images, every `.trl` edit |

Snapshots are affordable because the units are small: an attribute collection is a handful
of strings, existence is a bool. Only assets are bulky — an image or a `.trl` is held twice
— so the stack is capped at 100 steps rather than growing without bound.

This works **only** because deleting tombstones instead of erasing. No index ever moves, so
a record taken now still points at the same thing ten edits later. Restoring a deleted
category also relies on the element being kept whole: put the copy back, then find the
descendants' declaration handles again inside it, since the originals died with the parent.

Two details worth knowing:

- **Steps coalesce by key across consecutive frames.** A gizmo drag writes attributes every
  frame; the guard reopens the same step as long as the key matches and the last frame was
  the previous one, so a drag is one entry and two drags are two. The property panel keys on
  the field being dragged, so moving to another attribute starts a new step.
- **Records that change nothing are dropped when the step closes**, and a step left empty is
  popped. Clicking into a field and out again does not fill the stack with no-ops.

Undo bumps the revision like any other edit, calls `editor::prune` to drop selection entries
whose node no longer exists, and — from the caller, not from `undo` — tells the trail editor to reread, since its
working copy is a cache of bytes that just changed underneath it.

## Deleting leaves a tombstone

Nothing is ever erased from `Pack::categories` or `Pack::pathables`. Both are indexed
positionally by the selection, `State::pathables_by_category`, the hidden flags, the
viewer's bake and the trail editor, so erasing one entry would move every entry after it
and invalidate all of them at once. Instead each entry carries a `Tombstone`:

- `deleted` marks it gone. Everything that walks either list skips it — the tree, the
  bake, `countByType`, the writer's attribute sync, the resources window's
  reference scan, the gizmo. A missed check shows up as a deleted thing still doing
  something.
- `detached` owns the element itself, lifted out of the file's document into a document of
  its own, and the model's `SourceNode` is repointed at that copy. The element is gone from
  what gets serialized, but it is kept **whole** — attributes, spelling, child comments —
  so putting it back is a copy rather than a reconstruction.

That second half is what makes undo affordable: with indices stable and the element
preserved, undoing a delete is a flag flip plus an `insert_copy`, not a reparse.

Deleting a **category** removes each of its declarations, and since child categories are
nested inside them, the whole subtree's elements go in one operation — the descendants are
tombstoned too and their now-dangling node handles cleared. Markers are separate: they live
in `<POIs>`, not inside the category, so `pathable_edit::remove` deletes them explicitly
first. The pack layer deliberately does not do that for you; leaving them would be valid
XML, just markers with a dangling `type`.

The cost of tombstones is memory that is not reclaimed until the pack is reloaded, which
for an editor session is nothing.

## A new marker is appended, not inserted

`Pack::pathables`, `State::pathables_by_category`, the selection and the viewer's bake are
all positional, so inserting into the middle of the pathable vector would shift every index
after it and invalidate all four at once. `Pack::addPathable` therefore **appends** to the
vector, always, and no existing index ever moves.

Categories are appended the same way, and nest by *element*: a new category's
`<MarkerCategory>` goes inside one of its parent's declarations, which is what makes it a
child — so the file it lands in is the parent's, not one of its own choosing. Only a root
category picks a file, and it goes in before the `<POIs>` block where a reader expects
categories to be. A new trail comes with an empty `.trl` written as a pending asset, since
a `traildata` pointing at nothing reads as a broken trail rather than an empty one.

Where the marker appears is decided in the two places that actually care:

- **In the file**, by `pugi::xml_node::insert_child_after` on the sibling it should follow,
  so the document reads in the order the editor shows. With no sibling to follow it goes at
  the end of a `<POIs>` block in the category's own file, created if that file has none.
- **In the tree**, by `State::pathableAdded` inserting the new index into
  `pathables_by_category` after its sibling. That list *is* the tree order; it only happens
  to match the vector order on a freshly loaded pack.

Deleting gets the same property a different way; see the tombstone section above.

## Assets are pending until a save

The `DataSource` is read-only, and for a zip it is the very archive still open for reading,
so an added image cannot go anywhere at the moment it is added. `Pack::added_assets` and
`Pack::removed_assets` hold both kinds of change until a save resolves them: `buildZip`
writes the adds in and leaves the removals out, `saveToDirectory` writes and deletes files
on disk, and the caller clears the lists afterwards.

Two things fall out of that:

- **`resources` looks at the pending adds first.** Otherwise a freshly added icon would
  read as missing everywhere until the pack was saved and reopened. And because Nexus hands
  back the *existing* texture for an identifier it has already seen, changing the bytes
  behind a path has to go through `resources::forget`, which drops the cache entry and bumps
  the generation so the next load asks under a name Nexus has not seen.
- **An added file takes its folder's existing spelling.** The window works in normalized
  (lowercase) paths, so writing one straight back would put `data/icons/x.png` beside an
  existing `Data/icons/` — two directories in the archive differing only in case.

What references an image comes from the attribute table: every attribute whose kind is
`Path` is a reference, so `iconfile`, `texture`, `traildata` and anything added later are
all counted without being named in the UI. The counts and the folder tree are rebuilt per
`State::revision`, not per frame, for the same reason the open pack is baked per revision.

The resources window previews **only the selected image**, deliberately. Nexus never
releases a texture, so drawing a thumbnail per row would pin every image in a 1,375-image
pack in memory for the session the first time the folder was opened.

## Two selections

The editor holds two independent selections, and which one a click lands in is decided by
what was clicked, not by a mode:

| | `State::selection` | `State::points` |
|---|---|---|
| Holds | categories and pathables, as `NodeRef` | point indices of one trail |
| Filled by | the tree, the resources window, a world pick on a marker | the trail editor's list, a world pick on a handle |
| Read by | details, the tree, delete, add, the gizmo | the trail editor, the handles, the gizmo |

They were one list once, with `NodeKind::TrailPoint` and a `sub` field on every `NodeRef`.
Folding them together looked cheaper — multi-select and the gizmo anchor came for free —
but a trail point is not a sibling of a marker in the tree, it is a sub-selection *inside* a
trail that is itself selected, and one list could not say that. Clicking a point evicted its
trail from the details window; deleting a point had to clear the list and so read as
deselecting the trail; a viewport miss needed a special case to put the trail back; and
every consumer filtered the mixture apart by kind and bounds-checked the point index itself.
Splitting them deleted all four, and `NodeRef` lost `sub` with them.

What holds it together:

- **`PointSelection` carries its trail index once**, rather than on every entry, and drops
  its points when the trail changes. A stale point from a trail that is no longer open
  cannot exist.
- **The gizmo prefers points.** Whenever any are selected, the trail they belong to is
  necessarily selected too — that is what opened the editor — so obeying both would drag the
  whole trail every time one point was nudged. The cost is that a marker and a trail point
  can no longer be transformed as one group, which nothing wanted.
- **One click rule for both.** `editor::applyClick` applies plain/ctrl/shift to either
  selection, with the range handed in by the caller — tree rows for one, an index span for
  the other — so the two lists cannot drift apart on what a modifier means. The viewport
  passes its own `ClickMods`: it has no rows to run between, so out there shift is a second
  way to say ctrl.
- **`editor::prune` is the only thing that checks for stale nodes**, and it runs after every
  structural edit. Points are pruned by `trail_edit` instead, for the reason above.

All of this is covered by `test/selection_test.cpp` rather than by hand.

## The gizmo anchor

The gizmo draws on the **first** node selected, not the last. `Selection` keeps click
order and only ever appends, so the anchor is stable as the selection grows — the handles
do not hop to whatever was clicked most recently, which is what makes a group drag
predictable. The property panel wants the opposite (`primary()`, the last click), so both
accessors exist.

Everything a drag does is expressed as one transform about the anchor:

```
p' = anchor' + R (p - anchor)      orientation' = R · orientation
```

A pure translation has `R = identity`, so it falls out as a shift. A pure rotation leaves
`anchor'` at `anchor`, so the anchor turns in place while the others **orbit** it — which
is why a group rotation writes position as well as `rotate` on every non-anchor node. The
anchor's own offset is zero, so the same loop covers it without a special case.

Two consequences worth knowing. `R` is recovered per frame from the anchor's stored
attributes, which are formatted to six significant digits, so a long group drag drifts a
little. And rotating a group stamps `rotate` onto markers that had none, turning them from
billboards into fixed-orientation quads — unavoidable, since orbiting is a rotation.

Only POIs take part. A trail's geometry lives in its `.trl`, so moving one means moving
its points; the gizmo skips them rather than pretending.

## The attribute table is the core abstraction

Attributes get defined **once**, as data: name, canonical spelling, value kind
(float / int / bool / string / color / enum / path), default, whether it inherits from
the category. Parsing, serialization, the property form, and the "add attribute"
picker are all driven off that one table. Adding a new attribute must be a one-line
change. This is the single most important structural rule in the project.

Unknown attributes are kept verbatim as string key/values so an unrecognised extension
survives a round-trip and shows up in the property panel as a raw text field.

The table also has to carry the explicit-vs-inherited distinction, because the property
panel needs to show the effective value while making clear whether it is set here or
came from an ancestor category — see [format.md](format.md).

### Where an attribute *applies* and where it may be *set* are two questions

`AttributeDef::applies_to` answers the first: which node kinds consume the attribute, traced
to the Pathing file that reads it. `pack::settableOn` answers the second, and it is wider —
`applies_to` plus categories whenever the attribute inherits, because a `markercategory` is
precisely where an author sets `iconSize` for a whole subtree without consuming one itself.

Asking `applies_to` where the answer is `settableOn` was a real bug: the "add attribute"
picker offered a category only its own five structural attributes, so there was no way to
give a category an icon size at all. Anything filtering the attribute table by node kind
wants `settableOn`; `applies_to` is for deciding whether a value has an effect where it sits,
which is what the property panel's "does nothing here" warning and `markus_probe`'s matching
lint are for. The reasoning behind each classification is in [format.md](format.md).

## A pack is a zip or a folder

Everything above `pack/source.h` reads through the abstract `DataSource`, with two
implementations: `zip::Archive` and a directory reader. TmfLib draws the same line
(`IDataReader` / `ZipArchiveReader` / `DirectoryReader`).

The folder case exists so a pack's **source tree** can be edited in place — a git checkout
like `gw2-markers` rather than the built zip. That matters because the zip is a build
artifact: editing it and then rebuilding from source would throw the edits away.

Both paths behave identically. The same `normalizePath` applies to directory entries, so a
folder and the zip built from it resolve resources the same way — verified by running
`markus_probe` over both and getting identical models.

Two things stay different, and both are recorded on `Pack`:

- `from_directory` decides how a save is written: files back in place, or a rebuilt archive.
  **Save As always writes a zip and clears it**, along with `source_path` — the pack now
  lives in an archive whatever it was read from, and leaving the flag set would send the
  next plain Save down the folder path with a `.zip` as its target directory.
- A folder is an **editing convenience, not a shippable pack**. Blish HUD does not reliably
  load unzipped packs, so the UI says so on the button tooltip.

The directory reader skips dot-directories. Without that, opening a pack repo would walk
`.git`.

## Saving serializes, it does not diff

An earlier version of the writer spliced replacement start tags into the original bytes,
so an unedited file came back byte-identical. It was replaced with a plain serializer.

The splice preserved more — it kept intra-tag whitespace, which pugixml cannot represent —
but it could only ever *rewrite tags that already existed*. Adding a marker, deleting one,
or creating a pack from scratch all need node insertion, which a splice cannot express
without also inventing the surrounding indentation. Those are the features that matter
next, and they fall out of a serializer for free.

The trade was made deliberately: a pack reformats **once**, on its first save, and is
stable from then on.

So the parsed `pugi::xml_document` is now kept alive for the pack's lifetime — it *is* the
storage. The model indexes into it with `SourceNode` handles, saving pushes the model's
attributes back onto those nodes and serializes, and adding or removing a marker becomes a
node insert or erase. `Pack` is therefore move-only: copying it would leave two packs
sharing one document.

What still survives a save: comments, processing instructions, the declaration, attribute
order, original attribute spelling, verbatim attribute values, the BOM, CRLF, and the file
split. What does not: blank lines between elements, wrapped attribute lists, and `/>`
without a preceding space.

## The tree is categories

The XML files a pack is split across are an authoring convenience. Every loader merges
them into one category tree and one pathable list, so the split has no meaning to the
game — which is exactly how pack authors treat it.

That was the open question, and the data settled it. Across all 9 test packs:

| | |
|---|---|
| Categories | 31,817 |
| Declared in more than one file | **1** |

The single exception is `gw2-markers`' `haskha` root, which each of its 10 files reopens
as a wrapper. No leaf category anywhere is split. So a file-oriented tree would add a
level of nesting that carries no information and, for hand-authored packs, actively
obscures the structure.

The tree therefore shows categories, with each pathable under its own category. Files are
still tracked and still matter — just as metadata rather than hierarchy:

- Every pathable records the file it came from; the property panel shows it, and the
  writer needs it.
- A category records **all** of its declarations, since one may legitimately span files.
- New markers will need a target file; the sensible default is the file its siblings are
  already in.

Pathables whose `type` resolves to no category appear under an "Unresolved" node rather
than being dropped, so a broken `type` is visible instead of silent.

## Rendering

The viewer draws the editor's pack in its own scene, with the installed packs and through the
same sinks (viewer.md, *The editor inside*). What only an editor draws — the outline, the
picking, the point handles, a dot where an icon is missing, the gizmo — is `editor/world/`, over
the scene the viewer published. The camera is `viewer/camera.*`, and clipping, subdividing and
emitting a textured quad, which the overlay sink and the outlines both use, is
`viewer/render/overlay.*`. What follows is how that overlay path works.

Nexus hands us `AddonAPI::SwapChain` and, via `Textures.GetOrCreateFromMemory`, an
`ID3D11ShaderResourceView*` per image — directly usable as an `ImTextureID`. So no
custom DX11 pipeline is needed:

1. Each frame, read `Mumble::Data::CameraPosition` / `CameraFront` and the FOV out of
   the identity JSON. The **viewport is ImGui's `io.DisplaySize`**, not
   `NexusLinkData::Width/Height` — see below.
2. Build view and projection with glm. Positions are metres; `fadeNear`/`fadeFar` are
   inches, so divide distances by 0.0254 before comparing. Verify against a known marker
   position early.
3. Cull by `MapID` against `Context.MapID`, then by `fadeNear`/`fadeFar`.
4. Project each marker centre to screen, size the quad from `iconSize * 2`, clamp its
   on-screen height to what `StandardMarker.World.cs` ends up with — `minSize * 2` and
   `maxSize * 2` **real** pixels, because the quantity Blish clamps is twice the pixel
   height; see [format.md](format.md) — and push a textured quad into the draw list,
   sorted back-to-front. Billboard unless `rotate*` is set.
5. Trails: project each `.trl` point, emit a triangle strip of quads along the polyline,
   breaking into separate ribbons at each `(0,0,0)` segment terminator.

**The viewport is whatever ImGui says it is.** `NexusLinkData` carries a `Width`/`Height`
and it is the obvious thing to reach for, but those come from the DXGI swap chain and Nexus
writes them from its `ResizeBuffers` hook alone — a swap chain recreated at another
resolution (`Present_Internal` notices the new pointer and updates only the window handle)
leaves them behind. The draw list is rasterized against `io.DisplaySize`, so projecting with
one shape and drawing into another stretches the whole world along one axis: markers come
out squashed and the gizmo's rings come out as ellipses. TaimiHUD takes the same view and
uses `io.DisplaySize` too. Nothing here reads `DL_NEXUS_LINK` any more.

**Nothing is culled on a marker's centre.** A marker large enough to be worth scaling up is
one you can stand inside, and its centre goes behind the camera long before the marker
leaves the screen. Deciding on the centre — or dropping a quad because one corner is behind
— makes a large marker vanish as you walk into it, which for an editor is the worst possible
moment to lose it. So every quad is **clipped against the near plane** instead, in its own
`(u,v)` parameter space, and what survives (a polygon of three to five corners) is what gets
drawn, outlined and clicked. The only centre test left is a cheap reject for markers a full
quad-width behind the camera, which no orientation can bring back into view. Blish never
faces the question: the GPU clips for it.

**Textured quads are subdivided, because a draw list cannot divide by w.** `AddImageQuad`
is two triangles with affine UVs, which is only right when all four corners projected at the
same depth. They do not: a billboard faces the ray to its own centre, so the further
off-axis it sits the more its corners differ in depth, and a `rotate`d marker or a trail
segment running away from the camera is raked by construction. Affinely, the texture bends
along the quad's diagonal instead of foreshortening — on a large marker that reads as a
saddle, and on a trail as the pattern sliding rather than compressing into the distance. So
every textured quad is split in **world** space and each vertex projected on its own; the
corners are coplanar, so a bilinear blend of them stays on the real surface.
`viewer::affineQuadSubdivisions` sizes the grid from the error it is removing and returns 1 —
one `AddImageQuad`, exactly as before — whenever there is nothing to correct, which is the
common case. Blish gets all of this from the GPU for free.

The grid covers the whole quad rather than its visible part, so that its cells tile. Cells
wholly behind the near plane are dropped and the few it cuts are clipped again at cell
scale, coming out as fans of up to five corners. That is why the grid is emitted through
`PrimReserve`/`PrimQuadUV`/`PrimWriteVtx` rather than a loop of `AddImageQuad`: one draw
command for the lot, with the worst-case reservation handed back at the end.

The size clamp is the one thing a clipped quad does *not* get. `minSize`/`maxSize` are
expressed in projected pixel height, and a quad straddling the near plane has none to
measure, so it is drawn at its true size — Blish's own screen bounds are meaningless there
too, and inventing a height would resize a marker for the crime of being walked into.

Nothing is drawn into the world while the **full map is open** (`Context.IsMapOpen` from
MumbleLink, carried on `Camera`). The camera keeps updating behind the map, so without the
check the markers would sit at positions that mean nothing over it. The viewer does not call
the overlay then, and the editor clears its hover state, so a click on the map cannot select
whatever the mouse was last over in the world either.

A draw list has no depth buffer, so through the overlay sink markers are not occluded by
world geometry. Through GoodRender those behind the terrain are: `world_quad` hides each pixel
behind what the game drew in front of it (see [viewer.md](viewer.md#drawing)). The editor's outlines and handles stay on the draw list, over
everything, which is what they are for.

ImGuizmo takes the same view/projection matrices plus a model matrix per selected
object, so it drops straight in once step 2 is correct.

**The gizmo resolves a marker's placement through the bake, not its own copy.**
`viewer::markerTransform` is what both use: position with `heightOffset` applied, and
rotation, resolved through the category chain. The handles are supposed to sit on the drawn
icon, so a second implementation is a second chance to disagree with the picture — and the
two did, once. One took `rotate="90"` the lenient way Blish does, filling the rest with
zero; the other required all three components and read it as no rotation at all, so the
gizmo drew unrotated handles on a rotated marker and a drag then wrote the wrong angle
back.

## Threading

Everything runs on the Nexus render callback (render thread). Three exceptions:

- The native file dialog runs on a worker thread; `render()` polls `takeResult()`.
- **`onWndProc` runs on the game's message thread.** A world click cannot reach ImGui (see
  CLAUDE.md), so the marker under the cursor is read there and handed to the render thread
  through atomics — `pending_select`, `pending_select_point`, and the overlay's
  `hovered_pathable`. The handoff *out* is atomic; the reads *in* (`state.loaded`,
  `state.settings.select_in_viewport`, `io.WantCaptureMouse`) are plain and are a data race
  in the letter of the model. They are single words that change between frames rather than
  during one, and the worst outcome is a click landing one frame late or being ignored, so
  they are left alone deliberately rather than by oversight. Anything read there in future
  needs the same look.
- Pack loading (unzip + parse) should move to a worker once packs get big enough to
  stutter. Not yet needed.

`file_dialog::shutdown` joins the worker with the mutex **released** — the worker takes that
same mutex to store its result on the way out, so joining under it deadlocks, and unload
with a dialog still open is exactly when that runs.
