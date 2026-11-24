# Markus

A [Nexus](https://raidcore.gg/Nexus) addon for Guild Wars 2 that shows marker packs in the game:
markers and trails in the world, on the minimap and on the full map. It reads the same `.zip` and
`.taco` packs as Blish HUD and TacO.

## For players

**[Download the latest release](https://github.com/HasKha/gw2-markus/releases/latest)**

1. Download `Markus.dll` from the release page.
2. Put it in `<Guild Wars 2>/addons/`.
3. Enable it in Nexus.

Upgrading from Marker Pack Editor (1.6 or older)? Delete `MarkerPackEditor.dll` from the same folder.
Your packs and settings move over by themselves the first time Markus loads.

### What you can do

- **Get packs.** **Manage Marker Packs** in the menu lists the public marker packs: download them,
  update them, or tick **Keep updated** so they update whenever the game starts. You can also drop
  a pack into `<Guild Wars 2>/addons/Markus/packs/` yourself.
- **Bring your Blish HUD packs.** On the first start, if Blish HUD has marker packs, Markus offers to
  copy them over, along with which categories you turned on or off and which markers you have
  hidden. You can do it again any time from the options.
- **Choose what you see.** **All Markers** in the menu turns categories on and off. The menu is in
  Nexus's menu, and behind the Markus icon in Nexus's bar. You can set a key in Nexus to show or
  hide every marker at once.
- **Use markers.** Walk up to a marker to see what it is about; press your game's Interact key to
  copy a waypoint code, hide a marker you have done, and so on.
- **On the maps.** Markers and trails show on the minimap and the full map. Point at one for a
  tooltip; hold Shift to see its categories, or right-click it to hide them.
- **Hide finished achievements.** Add a GW2 API key in the options and markers for achievements you
  have finished hide themselves.
- **Draw behind the scenery.** With [GoodRender](https://github.com/HasKha/gw2-good-render) installed,
  markers draw under the game's interface, and **Hide markers behind terrain** puts them behind
  walls and hills. Without it they draw over everything.

Everything else, from opacity and view distance to what shows on each map, is in the addon's
options in Nexus.

Found a bug or have a request? [Open an issue](https://github.com/HasKha/gw2-markus/issues).

---

## For developers

### Build

You need Windows, Visual Studio 2022 with the C++ workload, and CMake 3.31 or newer. Every other
dependency is fetched by CMake on the first configure.

```bash
cmake -S . -B build && cmake --build build --config Release
```

The DLL lands in `bin/Release/`; `--config Debug` builds into `bin/Debug/`. A new source file needs a
fresh configure. To work in Visual Studio, run `cmake --open build`.

### Test

```bash
ctest --test-dir build -C Release --output-on-failure
```

The tests take a few seconds and need no game. The pack layer is tested against real marker packs
rather than fixtures, by default the ones in `<Documents>/Guild Wars 2/addons/blishhud/markers`.
None ship with this repo; [docs/testing.md](docs/testing.md) explains how to point the tests at your own.

### Contributing

Pull requests are welcome.

- Work on a branch and open a pull request; nothing goes straight to `main`.
- Run the tests before opening it. A bug fix comes with a test that fails without the fix.
- Follow [docs/code_style.md](docs/code_style.md).
- Keep [docs/plan.md](docs/plan.md) up to date when a change finishes, starts or reorders something on it.

### Docs

Every doc in `docs/` is listed in [CLAUDE.md](CLAUDE.md#docs), with when to read it.
