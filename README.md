# MarkerPackEditor

A [Nexus](https://raidcore.gg/Nexus) addon for creating and editing GW2 marker packs —
the `.zip`/`.taco` packs consumed by the [Blish HUD Pathing
module](https://github.com/blish-hud/Pathing) and TaimiHUD.

Edit a pack in-game, where you can see what you are editing: browse its categories, POIs
and trails in a tree, edit attributes in a form, watch everything drawn in the world at
its real position, and drag markers and trail points around with a 3D gizmo. Save, and
the pack is a zip again.

## Install

1. Download `MarkerPackEditor.dll` from the
   [latest release](https://github.com/HasKha/gw2-marker-pack-editor/releases/latest).
2. Drop it in `<Guild Wars 2>/addons/`.
3. Nexus picks it up on the next launch — or load it now from its addon list. Nexus will
   offer updates from this repo's releases from then on.

Open the editor from the Nexus quick access menu, or bind a key to **Toggle marker pack
editor** in the Nexus input binds. Undo and redo come bound to `Ctrl+Z` / `Ctrl+Y`.

## Using it

**File → Open Pack...** takes a `.zip`/`.taco`, **Open Folder...** takes an unzipped one,
and **New Pack...** starts an empty pack from nothing. One pack is open at a time.

The editor is five windows rather than one — Tree, Details, Toolbar, Trail editor,
Resources — each shown or hidden from the **View** menu and each remembered between
sessions. Markers and trails are drawn in the world as soon as a pack is open; clicking
one selects it and reveals it in the tree.

**Save** writes the pack back where it came from. A folder is written in place; a zip is
rebuilt beside itself and swapped in, keeping the previous file as `.bak`. Comments,
attribute order and original spelling all survive a save, so a hand-authored pack stays
readable and its diffs stay small — the one thing given up is whitespace inside a tag,
which reformats once on the first save.

Note that Blish HUD does not reliably load an unzipped folder: a folder is an editing
convenience, and a zip is what you ship.

## Build

Windows, Visual Studio 2022 or newer (C++20), CMake 3.31+. Every dependency is fetched by
CMake — there is nothing to install first.

```bash
cmake -B build && cmake --build build --config Release
```

The DLL lands in `bin/Release/`. For the IDE, `cmake --open build`.

## Test

```bash
ctest --test-dir build -C Release --output-on-failure
```

The pack layer is tested against **real marker packs** rather than fixtures — by default
the ones in `<Documents>/Guild Wars 2/addons/blishhud/markers`. None of them ship with this
repo, so point the suite at your own with `MPE_TEST_PACK_DIR` (and `MPE_TEST_FOLDER_PACK`
for an unzipped pack). Tests whose pack is missing skip and say so; a corpus that is
missing entirely fails, because a suite that silently tests nothing is worse than none.

`mpe_probe` is the companion tool: it loads a pack and prints what it found, what it could
not parse, and what a save would change. Reach for it when the question is what a pack
looks like rather than whether something is broken.

## Docs

[plan.md](docs/plan.md) — roadmap and current state ·
[architecture.md](docs/architecture.md) — how it is built and why ·
[format.md](docs/format.md) — the marker pack format ·
[references.md](docs/references.md) — where the format's ground truth lives

## License

MIT — see [LICENSE](LICENSE).
