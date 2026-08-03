# MarkerPackEditor — plan

Roadmap and current state. Living document: update it in the same change that makes it
stale. It holds *what is left and in what order* — not how anything works.

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

The full loop works and is confirmed in game: **open a pack, edit it, save it back.**

| Area | State |
|---|---|
| Read | zip and folder sources, XML, attribute inheritance, `.trl`. 937/937 files across 9 test packs |
| Tree + properties | category tree with visibility toggles; attribute form generated from the attribute table |
| World | billboards, trail ribbons, rotation, selection highlight, click-to-pick |
| 3D edit | ImGuizmo translate/rotate on one selected POI, writing straight back to attributes |
| Write | serializes the owned document; comments, order, spelling, BOM, CRLF and the file split survive. 947/947 files stable on rewrite |

Two things are unproven: **no saved pack has been loaded in Blish HUD yet**, and the
editor has no way to add or delete anything.

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
same model as the zip built from it. Others in that folder are not test targets, but
`mpe_probe` accepts them and running it over all of them is how the format claims were
measured.

---

## Next up

Roughly in order. 1 is a one-off check; 2 unblocks comfortable use of everything else.

### 1. Prove a saved pack loads in Blish HUD

Save `haskha_markers`, drop it in the markers folder, load it in Blish HUD, confirm the
markers still show. `mpe_probe` proves the bytes are self-consistent, not that Blish
accepts them. Until this passes, saving is not really done.

### 2. Split the UI into separate windows

One window that owns both the tree and the details is wrong: it forces the gizmo into
whatever screen the big window is not covering. Break it into windows the user places
themselves, each independently closable, with a **View** menu listing all of them.

| Window | Contents | State |
|---|---|---|
| Tree | the main window: category hierarchy, selection, visibility toggles | exists, needs to shed the details panel |
| Details | the attribute form for the selection | exists, needs its own window |
| Toolbar | gizmo mode, new POI, and whatever comes later | new |
| Trail editor | see [3](#3-trail-editor) | new |
| Resources | see [4](#4-resources-window) | new |

Points to settle while doing it:

- **The menu bar cannot live in a closable window.** Today File/Settings hang off the
  single window; if Tree can be closed, they go with it. Either the Toolbar window is the
  one that always carries the menus, or the menus move somewhere that cannot be closed.
- Nexus options currently expose one "show window" checkbox. It needs one per window, or
  the View menu has to be reachable another way after everything is closed.
- Window visibility and layout should persist — see [settings](#smaller-work-unblocked).

### 3. Trail editor

There is no trail editing at all today: a `.trl` is read, drawn, and written back
unchanged. The window should:

- List the points of the selected trail, editable as numbers, with add / insert / delete
  and reorder.
- Select a point in the list to select it in the world, and vice versa; drag it with the
  gizmo. (This is the "trail points selectable and draggable individually" item that has
  been deferred twice.)
- Generate geometry the way `gw2-markers/scripts/shared.py` does by hand today —
  `make_circle` / `make_arc` around a centre at a radius, in game inches, plus at least a
  straight line between two points. That script is the spec: the editor should make those
  scripts unnecessary.
- Respect `(0,0,0)` segment terminators as real breaks in the point list, not as points.

### 4. Resources window

Inspect, add and remove the images a pack carries. Needs the `DataSource` to grow a write
path for binary entries (today only `.xml` is rewritten), and needs a way to answer "what
references this icon" so removing one is not silently destructive.

### 5. Add and delete

The writer became a serializer specifically to make this possible, so it is unblocked.
Adding or removing a marker is a `pugi::xml_node` insert or erase on the document the
`Pack` owns, plus the matching entry in `Pack::pathables` / `categories`.

- Add / delete a POI, a trail and a category.
- Place a marker at the player's position (MumbleLink `AvatarPosition`).
- Create a new pack from scratch — needs a `Pack` built without a `DataSource`, and a
  first save that writes a zip from nothing.

Watch out:

- `Pathable::category` and every index in `State::pathables_by_category`,
  `orphan_pathables`, `Selection` and the renderer cache are **positional**. Erasing from
  `Pack::pathables` invalidates all of them — tombstone entries instead of erasing, or
  rebuild the index and remap the selection. Tombstoning also makes undo simpler; see
  [6](#6-undo--redo).
- A new node needs its `SourceNode.file` set, and a category needs a declaration appended,
  or the writer will not know where to put it. The sensible default file is the one the
  node's siblings are already in.
- `Pack` is move-only. That is deliberate; do not "fix" it by adding a copy constructor.

### 6. Undo / redo

Deferred twice. **Preliminary read: feasible, and cheaper than feared — but only if it is
built before add/delete, not after.**

The reason it is cheap is that edits do not touch the XML document. `AttributeCollection`
on the model is the live edited state; `writer.cpp` syncs it onto `pugi::xml_node`s only
at save. So every edit that exists today is "set or erase attribute *k* on node *n*", and
an undo stack of `(node, key, old, new)` covers all of it — the property panel and the
gizmo both funnel through the same attribute writes.

What actually needs deciding:

- **One choke point.** Every mutation already calls `State::touch()`; undo needs the
  edits recorded there or immediately around it, not at each call site, or the "no
  copy-paste" rule gets violated one attribute at a time.
- **Drag coalescing.** A gizmo drag calls `touch()` every frame. One drag must become one
  undo step — open the step on `ImGuizmo::IsUsing()`'s rising edge, close it on the
  falling edge.
- **Structural edits are the hard half**, and only because of positional indices. If
  delete tombstones rather than erases, an undo is just un-tombstoning and indices never
  move; if it erases, undo has to reparse and rebuild, which also invalidates the
  selection. This is the argument for settling [5](#5-add-and-delete)'s deletion strategy
  first.
- Visibility toggles must **not** be undoable — they are view state and deliberately do
  not bump the revision.

Verdict to confirm while implementing: attribute-level undo is a small, contained change.
Structural undo is affordable only under the tombstone design. Decide the deletion
strategy before writing either.

### 7. Selection

- **Shift+click range select is not implemented anywhere.** `tree_window::selectNode`
  handles `KeyCtrl` only; `Selection` already documents its ordering as existing so the
  last-clicked item can anchor a range, but nothing uses it. Needs a flattened view of the
  visible tree rows to define what "range" means.
- Ctrl+click works in both the tree and the viewport. Shift+click in the viewport should
  do whatever it ends up meaning in the tree, so the two stay consistent.
- **The gizmo should appear on a multi-selection**, anchored on the first node selected,
  with any transform applied to all selected nodes. Today `world::gizmo::draw` bails on
  `selection.size() != 1`, and `Selection::primary()` returns the *last* item — the anchor
  wants the first, so either add an accessor or reconsider what `primary()` means for the
  details panel. A rotate applied to a group has to be decided too: rotate each marker in
  place, or orbit them about the anchor.

### Smaller work, unblocked

- **Enum value lists** from the gw2pathing.com attribute pages, so `ValueKind::Enum` gets
  a dropdown instead of a text box.
- **`animspeed`** — parsed and stored, but trail textures do not scroll.
- **Settings persisted** to `MarkerPackEditor/settings.json`: gizmo mode, the Settings
  menu toggles, and the new per-window visibility.
- **Validation pass in the UI**, reusing what `mpe_probe`'s lint already checks.
- **Move pack loading off the render thread** if a large pack starts to stutter. 56k
  markers has not been slow enough to need it.
- **README with screenshots, GitHub releases.**

## Open questions

1. Group rotate semantics — rotate each marker in place, or orbit the selection about the
   anchor? (See [7](#7-selection).)
2. Where the menu bar lives once the main window is closable. (See [2](#2-split-the-ui-into-separate-windows).)
3. Delete strategy — tombstone or erase. It decides how hard undo and the index rebuild
   are. (See [5](#5-add-and-delete) and [6](#6-undo--redo).)
