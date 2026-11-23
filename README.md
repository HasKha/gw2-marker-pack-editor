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
**Markers**, in the Nexus menu or on a keybind you set, turns categories on and off.
Packs' Lua scripts run as they do in Pathing; **Script console**, beside it, shows what they
print and any error, and **Run pack scripts** in the options turns them off. Markers behave as
they do there too: they hide by festival, character, schedule or once you have used them, show
what they are about when you walk up, and **F** — or the key you pick in the options —
interacts, copying a waypoint or toggling a category. The options also hold Pathing's choices
for whether markers may hide themselves, animate or copy to the clipboard. Markers and trails
show on the minimap and the full map too, with a tooltip when you point at one — Shift shows
its categories, a right click hides them — and Pathing's map settings under **Maps** in the
options. With
[GoodRender](https://github.com/HasKha/gw2-good-render) loaded, markers draw under the game's
windows and hide behind walls; without it they draw over everything.

**Pack catalog**, in the Nexus menu, lists the packs Blish HUD's Pathing offers: download one
into the addon's own folder, update it, open its page or delete it. Packs you keep updated
update themselves when the addon loads.

Editing is off on a fresh install. Turn on **Enable pack editing** in the addon's options in
Nexus, then reload the addon; an install that already had the editor keeps it on.

With editing on, the **Packs** window, in the Nexus menu, lists every pack drawn. **Edit a
copy** copies one into the addon's `edits` folder, where it stands in for the original, so an
update cannot overwrite your changes, and opens it in the editor. Whatever the editor has
open is drawn as you edit it, exactly as a player will see it.

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
