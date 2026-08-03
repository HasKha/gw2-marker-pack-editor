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

The full loop works and is confirmed end to end: **open a pack, edit it, save it back, and
the edited markers show correctly in Blish HUD.**

| Area | State |
|---|---|
| Read | zip and folder sources, XML, attribute inheritance, `.trl`. 937/937 files across 9 test packs |
| Tree + properties | category tree with visibility toggles; attribute form generated from the attribute table |
| World | billboards, trail ribbons, rotation, selection highlight, click-to-pick |
| Selection | ctrl+click in the tree and the world, shift+click ranges in the tree, world picks reveal the node in the tree |
| 3D edit | ImGuizmo translate/rotate on the selection, anchored on the first node selected; a group rotation orbits the rest about it |
| Write | serializes the owned document; comments, order, spelling, BOM, CRLF and the file split survive. 947/947 files stable on rewrite |

The gap: the editor has no way to **add or delete** anything, and no undo.

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

Roughly in order. 1 unblocks comfortable use of everything else.

### 1. Split the UI into separate windows

One window that owns both the tree and the details is wrong: it forces the gizmo into
whatever screen the big window is not covering. Break it into windows the user places
themselves, each independently closable, with a **View** menu listing all of them.

| Window | Contents | State |
|---|---|---|
| Tree | the main window: category hierarchy, selection, visibility toggles | exists, needs to shed the details panel |
| Details | the attribute form for the selection | exists, needs its own window |
| Toolbar | gizmo mode, new POI, and whatever comes later | new |
| Trail editor | see [2](#2-trail-editor) | new |
| Resources | see [3](#3-resources-window) | new |

Settled:

- **The menu bar is drawn in each of the large windows** — Tree, Details, Trail editor —
  not in the Toolbar. The same bar, so it is reachable whichever of them is open, which
  means the menu drawing has to be one shared function rather than three copies.
- **Nexus options get a checkbox per window**, so everything is reopenable even with all
  windows closed. That replaces today's single "Show Marker Pack Editor window".
- Window visibility should persist — see [settings](#smaller-work-unblocked).

### 2. Trail editor

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

### 3. Resources window

Inspect, add and remove the images a pack carries. Needs the `DataSource` to grow a write
path for binary entries (today only `.xml` is rewritten), and needs a way to answer "what
references this icon" so removing one is not silently destructive.

### 4. Add and delete

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
  [5](#5-undo--redo).
- A new node needs its `SourceNode.file` set, and a category needs a declaration appended,
  or the writer will not know where to put it. The sensible default file is the one the
  node's siblings are already in.
- `Pack` is move-only. That is deliberate; do not "fix" it by adding a copy constructor.

### 5. Undo / redo

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
  selection. This is the argument for settling [4](#4-add-and-delete)'s deletion strategy
  first.
- Visibility toggles must **not** be undoable — they are view state and deliberately do
  not bump the revision.

Verdict to confirm while implementing: attribute-level undo is a small, contained change.
Structural undo is affordable only under the tombstone design. Decide the deletion
strategy before writing either.

### 6. Selection and the group gizmo — done, needs verifying in game

Built, builds clean, **not yet exercised in game**:

- Shift+click in the tree selects the range between the anchor and the clicked row, within
  one level. Shift in the world does what ctrl does — a range needs an order between two
  markers and the world has none.
- A world pick reveals the marker in the tree: every ancestor category opens and the row
  scrolls into view.
- The gizmo works on a multi-selection, anchored on the first node selected, and a group
  rotation **orbits** the others about the anchor — writing both their `rotate` and their
  position. See the anchor note in [architecture.md](architecture.md).

Left over:

- A group rotation stamps `rotate` onto markers that had none, which turns a billboard
  into a fixed-orientation quad. That is what orbiting means, but it is worth a look in
  game before deciding it is fine.
- Trails are skipped by the gizmo entirely — moving one means moving its `.trl` points,
  which is [2](#2-trail-editor)'s job.

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

1. Delete strategy — tombstone or erase. It decides how hard undo and the index rebuild
   are. (See [4](#4-add-and-delete) and [5](#5-undo--redo).)
