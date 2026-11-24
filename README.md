# Markus

A [Nexus](https://raidcore.gg/Nexus) addon for creating and editing GW2 marker packs —
the `.zip`/`.taco` packs consumed by the [Blish HUD Pathing
module](https://github.com/blish-hud/Pathing) and TaimiHUD.

Edit a pack in-game, where you can see what you are editing: browse its categories, POIs
and trails in a tree, edit attributes in a form, watch everything drawn in the world at
its real position, and drag markers and trail points around with a 3D gizmo. Save, and
the pack is a zip again.

## Install

1. Download `Markus.dll` from the
   [latest release](https://github.com/HasKha/gw2-markus/releases/latest).
2. Drop it in `<Guild Wars 2>/addons/`. Up to 1.6 the addon was called Marker Pack Editor:
   delete `MarkerPackEditor.dll` from there. Its folder, `addons/MarkerPackEditor/`, moves to
   `addons/Markus/` on the first load, with your packs and settings.
3. Enable it in Nexus.

## Using it

Every pack in `<Guild Wars 2>/addons/Markus/packs/` is drawn in the world. On the
first load, if Blish HUD has marker packs, the addon offers to import them: it copies them into
its own folder with what Pathing remembers — the categories you turned on or off, what pack
scripts stored, markers hidden until a time. **Import from Blish HUD...** in the options offers
it again. Blish HUD's folder is never drawn from, so both can run without every marker showing
twice.
The addon's menu is Blish HUD's: **All Markers** turns categories on and off, **Scripts** holds
what packs' scripts add, **Manage Marker Packs** opens the catalog and **Open Editor...** the
editor. It is in Nexus's menu, and behind the addon's icon in Nexus's bar — left or right click
it — as the top of the addon's options choose. Like Pathing, it leaves out categories with
nothing on the current map; **Smart categories** in the options shows them all. A keybind you
set in Nexus shows or hides all markers.
Packs' Lua scripts run as they do in Pathing; **Script console**, in the options, shows what they
print and any error, and **Run pack scripts** in the options turns them off. Markers behave as
they do there too: they hide by festival, character, schedule or once you have used them, show
what they are about when you walk up, and the game's Interact key, as Nexus's game binds have it,
interacts, copying a waypoint or toggling a category. The options also hold Pathing's choices
for whether markers may hide themselves, animate or copy to the clipboard. Markers and trails
show on the minimap and the full map too, with a tooltip when you point at one — Shift shows
its categories, a right click hides them — and Pathing's map settings under **Minimap** and
**Full map** in the options. With
[GoodRender](https://github.com/HasKha/gw2-good-render) loaded, markers draw under the game's
HUD and windows, and **Hide markers behind terrain** in the options, or a pack's `render-layer`, puts
them behind walls and hills; without it they draw over everything.

**Marker Packs** lists the packs Blish HUD's Pathing offers: download one
into the addon's own folder, update it, open its page or delete it. Packs you keep updated
update themselves when the addon loads.

The editor runs only when you ask for it: **Open Editor...** in the menu starts it at any
time. Closing its main window ends the session — it asks first if there are unsaved changes —
and **Ctrl+Z** and **Ctrl+Y** go back to the game with it.

Whatever the editor has open is drawn as you edit it, with every marker shown. The addon keeps
drawing your installed packs too, and reads a pack again as soon as you save it.

**File → Open Pack...** takes a `.zip`/`.taco`, **Open Folder...** takes an unzipped one,
and **New Pack...** starts an empty pack from nothing. One pack is open at a time.

The editor is five windows rather than one: the main one, with the pack's tree, and Details,
Toolbar, Trail editor and Resources, each shown or hidden from its **View** menu.

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
