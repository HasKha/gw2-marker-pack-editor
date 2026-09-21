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

Released as **1.0**. The full loop works and is confirmed end to end: **open a pack, edit
it, save it back, and the edited markers show correctly in Blish HUD.**

| Area | State |
|---|---|
| Read | zip and folder sources, XML, attribute inheritance, `.trl`. 937/937 files across 9 test packs |
| UI | five independent windows — Tree, Details, Toolbar, Trail editor, Resources — with a View menu and a Nexus options checkbox each |
| Resources | the pack's images as a folder tree, with reference counts, a preview, and add / remove pending until a save |
| Trail editor | map id, the point list, drag fields, point handles in the world, and a Trail menu: reload, export, add circle. A trail whose `.trl` the pack does not have says so and names the file |
| Tree + properties | category tree with visibility toggles; attribute form generated from the attribute table, per-element attribute sets traced to Pathing, inheritance shown for categories as well as pathables |
| World | billboards, trail ribbons, rotation, click-to-pick for markers and trail points; nothing drawn while the map is open. Selection is shown by an outline on a marker and by the point handles on a trail — a selected ribbon is left untinted on purpose, so what the pack actually looks like stays readable |
| Selection | two independent selections — nodes and trail points — sharing one click rule. ctrl+click in the tree and the world, shift+click ranges in the tree and the point list, world picks reveal the node in the tree. Cleared by a click that hits nothing — blank space in the tree, empty world in the viewport — where a miss lets go of the points first and keeps the trail |
| 3D edit | ImGuizmo translate/rotate on the selection — markers and trail points alike — anchored on the first node selected; a group rotation orbits the rest about it |
| Rename | a trail is named by its `traildata` path, and editing that in the details window moves the `.trl` with it, unless the new path already names data — that is a re-point, and nothing is written over |
| Create | Add POI, Trail and Category from the toolbar, placed by the selection. A POI copies the selected marker's attributes; a trail comes with an empty `.trl`. Both get the current `MapID`, unless an ancestor category already supplies it. Add Category prompts for a dotted id (defaulted to the selection's own, so editing its last segment makes a sibling and appending one makes a child), a required display name, an optional description, and whether to copy the selection's attributes |
| Delete | a marker, or a category and its whole subtree, from the details window. Tombstoned, never erased |
| Undo / redo | Ctrl+Z / Ctrl+Y and an Edit menu, over every edit there is: attributes, add, delete, images, trails |
| Settings | the Settings toggles, gizmo mode, which windows are open and where the browsers start, kept in `settings.json` |
| Create pack | `New Pack...` builds an empty pack in memory; Save As writes the first zip |
| Write | serializes the owned document; comments, order, spelling, BOM, CRLF and the file split survive. 947/947 files stable on rewrite |
| Tests | `mpe_test`, 60 cases over the real pack corpus, the selection rules and the trail editing layer, run by `ctest` |

A pack can be edited end to end, grown new branches, and started from nothing.

Before touching the pack layer, run the tests:

```
ctest --test-dir build -C Release --output-on-failure
```

They assert what this table claims — every pack parses, the writer is stable, comments and
BOM and CRLF survive, a save reopens as the same model — against the real packs rather than
fixtures. Under two seconds, no game needed. See [Testing](#testing) below.

`mpe_probe` is still there and is still the thing to reach for when the question is *what
does this pack look like* rather than *is it broken*: it prints and lints, it does not
assert. Every format claim in `format.md` was measured with it.

```
mpe_probe --save-test <scratch-dir> <pack.zip> <pack-folder>
```

### Test packs

Whatever is in the corpus folder gets swept, but two shapes are the ones worth having
there, and the claims above were measured against them:

| Pack | Shape | Why |
|---|---|---|
| A hand-authored pack | ~80 entries, ten xml files at the root, `Data/`, comments throughout | the round-trip target — losing a comment here is a bug |
| `hero.blish.pack.zip` | 1918 entries, 64 xml, 140 trl, 1375 png; `_markerCategories.xml` + `map<id>.xml` | large, popular, and already TmfLib `PackWriter` output — the scale and machine-generated case |

Plus one pack as an **unzipped folder**, which must produce the same model as the zip built
from it. That zip is built by the test rather than taken from the corpus: a checkout and a
shipped archive of "the same" pack drift the moment either is edited, and the test would
then be reporting the drift instead of the reader.

### Testing

`mpe_test` reads the packs in `<Documents>/Guild Wars 2/addons/blishhud/markers`, resolved
at configure time. Nothing is checked into the repo: these are other people's packs, some
of them hundreds of megabytes, and testing against what authors actually ship is the whole
point. Point it elsewhere without reconfiguring:

| Variable | What |
|---|---|
| `MPE_TEST_PACK_DIR` | folder of `.zip`/`.taco` packs |
| `MPE_TEST_FOLDER_PACK` | an *unzipped* pack, to check a folder source matches its zip |

Both exist as CMake cache variables too, for setting the default at configure time. A test
whose pack is missing skips with a reason rather than failing; a corpus that is missing
*entirely* fails, because a suite that silently tests nothing is worse than no suite.

---

## Next up

Roughly in order.

### 1. Whatever is still thin

- Reorder a trail's points. Adding and deleting them already work.
- **A rename field in the trail editor.** Renaming a trail means editing `traildata` in the
  details window, while the trail editor is where that path is on screen. A field there
  calling `pathable_edit::attributeRenamed` is the obvious next step.
- More shape tools beside the circle: a straight line between two points is the obvious
  next one. `gw2-markers/scripts/shared.py` remains the spec.
- **Enum value lists** from the gw2pathing.com attribute pages, so `ValueKind::Enum` gets a
  dropdown instead of a text box.
- **Validation pass in the UI**, reusing what `mpe_probe`'s lint already checks. Worth more
  now that new content can be authored in the editor rather than only edited. Two of the
  lint's checks already have a counterpart in the property panel — an unknown attribute and
  one that does nothing where it sits are both flagged there — so the pattern is set.
- **`fadecenter`**, Pathing's legacy alias for `canFade`, is not in the attribute table and
  so reads as an unknown attribute. Adding it means also adding a `deprecated` flag to keep
  it out of the "add attribute" picker. No pack in the corpus uses it; see
  [format.md](format.md).
- **`animspeed`** — parsed and stored, but trail textures do not scroll.
- **Move pack loading off the render thread** if a large pack starts to stutter. 56k
  markers has not been slow enough to need it.
- **Tests above the pack layer.** `mpe_test` links `mpe_editor`, so anything there is
  reachable: `selection_test.cpp` covers the click rules and `trail_test.cpp` covers
  creating trails, following the selection and renaming traildata. Undo itself and the
  editor's indexes are still covered by hand and could go the same way. The windows cannot.
- **Screenshots in the README.** The rest of the release prep is done.
- **CI.** A Windows job that configures and builds would catch a break from a clean clone,
  which is the one thing a local build cannot. The suite skips itself without a corpus, so
  it would run green and prove only that it links — worth having anyway.

Watch out, for anything that adds to the pack:

- `Pack` is move-only, and `Category` / `Pathable` are now move-only too, since a tombstone
  owns a document. That is deliberate; do not "fix" it by adding copy constructors.
- Anything new that mutates the pack needs an `undo::Step` around it and the matching
  capture inside, or it will be the one edit Ctrl+Z cannot take back.
- `State::pathables_by_category` and `category_hidden` are indexed by category, so a new
  category has to grow them — `State::categoryAdded` is the one place that does.

### Known rough edges

Everything built so far has now been driven in game: the tree and the property form, the
world view, selection and the gizmo, save, add and delete, the resources window, the trail
editor, and undo/redo. What is left is not doubt about whether it works, but these:

- A group rotation stamps `rotate` onto markers that had none, which turns a billboard
  into a fixed-orientation quad. That is what orbiting means, but it is worth a look in
  game before deciding it is fine.
- A trail is still moved a point at a time; there is no "move the whole trail" handle. The
  gizmo skips trail pathables themselves.
- **A trail ribbon is not pickable in the viewport** — only markers and the open trail's
  point handles are, so a trail is still selected from the tree. Clicking a ribbon counts as
  a miss, which lets go of the open trail's points and leaves the trail itself selected (see
  `deselectInViewport`), so the painful case is covered; clicking some *other* trail's ribbon
  still deselects. A segment-wise distance test against the projected polyline would make
  ribbons pickable and close it properly.
- Window positions are whatever ImGui's `imgui.ini` remembers. Visibility is ours and is
  persisted; position is not, deliberately.
- Only the tree has an `update()` so far. If a second and third window grow one, drive the
  calls from the `ui/windows.*` table instead of by hand — see [A window that is not
  drawing](architecture.md#a-window-that-is-not-drawing).
- `saveToDirectory`'s asset half — writing added images into a folder pack and deleting
  removed ones from disk — is the one write path the tests do not exercise, because the
  folder they point at is a real git checkout and a test that deletes from it would be
  vandalism. Both halves are confirmed by hand instead. Copying the checkout to a temp dir
  first would fix this and is the obvious next test.
- The resources window lists images only. `.trl` files are resources too and are already
  counted as references; showing them is a one-line change to `kImageExtensions` if it
  turns out to be wanted.
- **Add POI stores the raw character position.** A copied `heightoffset` then lifts the
  marker off that spot when it draws, which is either what you want or a surprise.
- **A new root category lands in the pack's first XML file.** It has no parent declaration
  to nest inside, so something has to choose, and nothing better suggests itself.
- **Perspective-correct texturing is capped at 16 subdivisions a side.** That covers every
  marker anyone would author; a quad both enormous and right in the camera's face still
  carries a little of the affine bend near the corner closest to you. Raising the cap is a
  constant in `world/camera.h`, paid for in vertices.
- **Quads are clipped against the near plane only, not the sides of the frustum.** A marker
  you are standing inside can project a corner tens of thousands of pixels off screen, and
  those cells are subdivided and emitted like any other before ImGui's scissor throws them
  away. Correct, just wasteful; side planes would be four more of the same clip.

## Open questions

None open. The last one — tombstone or erase — was settled by building it: tombstones, for
the reasons in [architecture.md](architecture.md).
