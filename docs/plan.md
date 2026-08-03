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
| UI | five independent windows — Tree, Details, Toolbar, Trail editor, Resources — with a View menu and a Nexus options checkbox each |
| Resources | the pack's images as a folder tree, with reference counts, a preview, and add / remove pending until a save |
| Trail editor | map id, the point list, drag fields, point handles in the world, and a Trail menu: reload, export, add circle |
| Tree + properties | category tree with visibility toggles; attribute form generated from the attribute table |
| World | billboards, trail ribbons, rotation, selection highlight, click-to-pick for markers and trail points |
| Selection | ctrl+click in the tree and the world, shift+click ranges in the tree and the point list, world picks reveal the node in the tree |
| 3D edit | ImGuizmo translate/rotate on the selection — markers and trail points alike — anchored on the first node selected; a group rotation orbits the rest about it |
| Create | Add POI at the character's position, placed by the selection and copying its attributes |
| Write | serializes the owned document; comments, order, spelling, BOM, CRLF and the file split survive. 947/947 files stable on rewrite |

The gap: nothing can be **deleted** — no marker, no category — and there is no undo.

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

Roughly in order.

### 1. Delete, and the rest of create

Adding a POI works; the mechanism is described in [architecture.md](architecture.md). What
is left:

- **Delete** a POI, a trail and a category. This is the one that needs the strategy
  decision below, and nothing else in this list is blocked by it.
- Add a **trail** and a **category**. A trail needs a `.trl` to point at — an empty one
  written as a pending asset, which the trail editor can then fill.
- Insert a point into a trail, and reorder points. Deleting them already works.
- More shape tools beside the circle: a straight line between two points is the obvious
  next one. `gw2-markers/scripts/shared.py` remains the spec.
- Create a new pack from scratch — needs a `Pack` built without a `DataSource`, and a
  first save that writes a zip from nothing.

Watch out:

- `Pathable::category` and every index in `State::pathables_by_category`,
  `orphan_pathables`, `Selection`, `trail_edit` and the renderer cache are **positional**.
  Erasing from `Pack::pathables` invalidates all of them — tombstone entries instead of
  erasing, or rebuild the index and remap the selection. Adding sidesteps this by appending
  only; deleting cannot. Tombstoning also makes undo simpler; see [2](#2-undo--redo).
- A new category needs a declaration appended, or the writer will not know where to put it.
- `Pack` is move-only. That is deliberate; do not "fix" it by adding a copy constructor.

### 2. Undo / redo

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
  selection. This is the argument for settling [1](#1-delete-and-the-rest-of-create)'s deletion strategy
  first.
- Visibility toggles must **not** be undoable — they are view state and deliberately do
  not bump the revision.

Verdict to confirm while implementing: attribute-level undo is a small, contained change.
Structural undo is affordable only under the tombstone design. Decide the deletion
strategy before writing either.

### Built, not yet exercised in game

Compiles clean and is in `bin/Release`, but nobody has driven it in GW2 yet:

- **The trail editor**, end to end: the map id field, the point list, the handles in the
  world, dragging a point with the gizmo, delete, reload, export, and add circle.
- **Add POI.** Its pack-layer half — the node insert and the reload — *is* covered without
  the game, by `mpe_probe`'s add check on all three test packs.

Known rough edges:

- A group rotation stamps `rotate` onto markers that had none, which turns a billboard
  into a fixed-orientation quad. That is what orbiting means, but it is worth a look in
  game before deciding it is fine.
- A trail is still moved a point at a time; there is no "move the whole trail" handle. The
  gizmo skips trail pathables themselves.
- Window positions are whatever ImGui's `imgui.ini` remembers; visibility is not persisted
  at all yet — see [settings](#smaller-work-unblocked).
- `saveToDirectory`'s asset half — writing added images into a folder pack and **deleting**
  removed ones from disk — is the one write path `mpe_probe` does not exercise, because the
  test folder is a real git checkout. The write half is confirmed by hand; the delete half
  is not.
- The resources window lists images only. `.trl` files are resources too and are already
  counted as references; showing them is a one-line change to `kImageExtensions` if it
  turns out to be wanted.
- **Add POI stores the raw character position.** A copied `heightoffset` then lifts the
  marker off that spot when it draws, which is either what you want or a surprise.

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
   are. (See [1](#1-delete-and-the-rest-of-create) and [2](#2-undo--redo).)
