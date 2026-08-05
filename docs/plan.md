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
| Create | Add POI, Trail and Category from the toolbar, placed by the selection. A POI copies the selected marker's attributes; a trail comes with an empty `.trl` |
| Delete | a marker, or a category and its whole subtree, from the details window. Tombstoned, never erased |
| Undo / redo | Ctrl+Z / Ctrl+Y and an Edit menu, over every edit there is: attributes, add, delete, images, trails |
| Settings | the Settings toggles, gizmo mode, which windows are open and where the browsers start, kept in `settings.json` |
| Create pack | `New Pack...` builds an empty pack in memory; Save As writes the first zip |
| Write | serializes the owned document; comments, order, spelling, BOM, CRLF and the file split survive. 947/947 files stable on rewrite |
| Tests | `mpe_test`, 22 cases over the real pack corpus, run by `ctest` |

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
mpe_probe --save-test <scratch-dir> "<...>/markers/haskha_markers.zip" "<...>/git/gw2-markers"
```

### Test packs

In `C:\Users\Giorgio\Documents\Guild Wars 2\addons\blishhud\markers`:

| Pack | Shape | Why |
|---|---|---|
| `haskha_markers.zip` | 80 entries, 10 hand-authored xml at the root, `Data/`, comments throughout | the round-trip target — losing a comment here is a bug |
| `hero.blish.pack.zip` | 1918 entries, 64 xml, 140 trl, 1375 png; `_markerCategories.xml` + `map<id>.xml` | large, popular, and already TmfLib `PackWriter` output — the scale and machine-generated case |

Plus `C:\Users\Giorgio\Documents\git\gw2-markers` as a **folder**, which must produce the
same model as the zip built from it. Others in that folder are not test targets, but
`mpe_test` sweeps every pack it finds and `mpe_probe` accepts them, and running over all of
them is how the format claims were measured.

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
- More shape tools beside the circle: a straight line between two points is the obvious
  next one. `gw2-markers/scripts/shared.py` remains the spec.
- **Enum value lists** from the gw2pathing.com attribute pages, so `ValueKind::Enum` gets a
  dropdown instead of a text box.
- **Validation pass in the UI**, reusing what `mpe_probe`'s lint already checks. Worth more
  now that new content can be authored in the editor rather than only edited.
- **`animspeed`** — parsed and stored, but trail textures do not scroll.
- **Move pack loading off the render thread** if a large pack starts to stutter. 56k
  markers has not been slow enough to need it.
- **Tests above the pack layer.** `mpe_test` links `mpe_pack` only, so undo, selection and
  the editor's own indexes are still covered by hand. `editor::State` has no dependency on
  imgui or Nexus and could be tested the same way; the windows cannot.
- **README with screenshots, GitHub releases.**

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
- Window positions are whatever ImGui's `imgui.ini` remembers. Visibility is ours and is
  persisted; position is not, deliberately.
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

## Open questions

None open. The last one — tombstone or erase — was settled by building it: tombstones, for
the reasons in [architecture.md](architecture.md).
