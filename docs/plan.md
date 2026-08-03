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
| World | billboards, trail ribbons, rotation, selection highlight, click-to-pick for markers and trail points; nothing drawn while the map is open |
| Selection | ctrl+click in the tree and the world, shift+click ranges in the tree and the point list, world picks reveal the node in the tree |
| 3D edit | ImGuizmo translate/rotate on the selection — markers and trail points alike — anchored on the first node selected; a group rotation orbits the rest about it |
| Create | Add POI at the character's position, placed by the selection and copying its attributes |
| Delete | a marker, or a category and its whole subtree, from the details window. Tombstoned, never erased |
| Undo / redo | Ctrl+Z / Ctrl+Y and an Edit menu, over every edit there is: attributes, add, delete, images, trails |
| Write | serializes the owned document; comments, order, spelling, BOM, CRLF and the file split survive. 947/947 files stable on rewrite |

The gap: creating a **trail** or a **category** — everything else about building a pack from
existing parts is there.

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

### 1. The rest of create

- Add a **trail** and a **category**. A trail needs a `.trl` to point at — an empty one
  written as a pending asset, which the trail editor can then fill.
- Insert a point into a trail, and reorder points. Deleting them already works.
- More shape tools beside the circle: a straight line between two points is the obvious
  next one. `gw2-markers/scripts/shared.py` remains the spec.
- Create a new pack from scratch — needs a `Pack` built without a `DataSource`, and a
  first save that writes a zip from nothing.

Watch out:

- A new category needs a declaration appended, or the writer will not know where to put it.
- `Pack` is move-only, and `Category` / `Pathable` are now move-only too, since a tombstone
  owns a document. That is deliberate; do not "fix" it by adding copy constructors.
- Anything new that mutates the pack needs an `undo::Step` around it and the matching
  capture inside, or it will be the one edit Ctrl+Z cannot take back.

### Built, not yet exercised in game

Confirmed in game: adding and deleting markers and categories, and removing an image
through the resources window (including the folder save that deletes it from disk). Still
undriven:

- **The trail editor**, end to end: the map id field, the point list, the handles in the
  world, dragging a point with the gizmo, delete, reload, export, and add circle.
- **Undo / redo.** Its pack-layer half is covered without the game: `mpe_probe` deletes a
  marker and a category subtree, restores both, and requires every file to serialize
  byte-identically to before the delete. What is unverified is the *feel* — whether a drag
  really comes back in one Ctrl+Z, and whether the Nexus binds behave in game.

Known rough edges:

- A group rotation stamps `rotate` onto markers that had none, which turns a billboard
  into a fixed-orientation quad. That is what orbiting means, but it is worth a look in
  game before deciding it is fine.
- A trail is still moved a point at a time; there is no "move the whole trail" handle. The
  gizmo skips trail pathables themselves.
- Window positions are whatever ImGui's `imgui.ini` remembers; visibility is not persisted
  at all yet — see [settings](#smaller-work-unblocked).
- `saveToDirectory`'s asset half — writing added images into a folder pack and deleting
  removed ones from disk — is the one write path `mpe_probe` does not exercise, because the
  test folder is a real git checkout. Both halves are confirmed by hand instead.
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

None open. The last one — tombstone or erase — was settled by building it: tombstones, for
the reasons in [architecture.md](architecture.md).
