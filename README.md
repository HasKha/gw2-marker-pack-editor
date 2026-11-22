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
3. Enable it in Nexus.

## Using it

Every pack in `<Guild Wars 2>/addons/MarkerPackEditor/packs/` is drawn in the world, and so is
every pack in Blish HUD's markers folder unless **Read Blish HUD's packs** is turned off in
the addon's options — turn it off while Blish HUD is running, or every marker shows twice.
**Markers**, in the Nexus menu or on a keybind you set, turns categories on and off. With
[GoodRender](https://github.com/HasKha/gw2-good-render) loaded, markers draw under the game's
windows and hide behind walls; without it they draw over everything.

Editing is off on a fresh install. Turn on **Enable pack editing** in the addon's options in
Nexus, then reload the addon; an install that already had the editor keeps it on.

**File → Open Pack...** takes a `.zip`/`.taco`, **Open Folder...** takes an unzipped one,
and **New Pack...** starts an empty pack from nothing. One pack is open at a time.

The editor is five windows rather than one — Tree, Details, Toolbar, Trail editor,
Resources — each shown or hidden from the **View** menu.

**Save** writes the pack back where it came from. A folder is written in place; a zip is
rebuilt beside itself and swapped in, keeping the previous file as `.bak`.
Make backups before saving, the editor may change your formatting.

## Contributing
- Pull requests are welcome.

## Build

```bash
cmake -S . -B build && cmake --build build --config Release
```

The DLL lands in `bin/Release/`. For the IDE, `cmake --open build`.

## Test

```bash
ctest --test-dir build -C Release --output-on-failure
```

The pack layer is tested against real marker packs rather than fixtures, by default the ones in
`<Documents>/Guild Wars 2/addons/blishhud/markers`. None of them ship with this repo; how to point the
suite at your own is [docs/testing.md](docs/testing.md).


## Docs

Every doc in `docs/` is listed in [CLAUDE.md](CLAUDE.md#docs), with when to read it.
