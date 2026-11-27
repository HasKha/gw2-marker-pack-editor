# Markus

Yet another marker pack addon for [Nexus](https://raidcore.gg/Nexus).

## For players

**[Download the latest release](https://github.com/HasKha/gw2-markus/releases/latest)**

1. Download `Markus.dll` from the release page.
2. Put it in `<Guild Wars 2>/addons/`.
3. Enable it in Nexus.

If you had Marker Pack Editor (`MarkerPackEditor.dll`), delete it.

### Features

- View marker packs. Displayed markers are accurate to Blish HUD's Pathing module.
- Combine with the the [Good Render](https://github.com/HasKha/gw2-good-render) for smooth marker rendering under the game's interface and optionally occluded by only characters, or characters + terrain.
- Full support for Lua scripts in addons.
- Enter your API Key to have markers hide your completed achievements. Achievement descriptions are also loaded from the API.
- Contains a marker pack editor.
- Optimized for sub-ms rendering performance to not impact your framerate even with tons of markers loaded.

---

## For developers

### Build

You need Windows, Visual Studio with the C++ workload, and CMake 3.31 or newer. CMake fetches
everything else.

```bash
cmake -S . -B build && cmake --build build --config Release
```

### Test

```bash
ctest --test-dir build -C Release --output-on-failure
```

The tests run against real marker packs, which this repo does not ship; see
[docs/testing.md](docs/testing.md).

### Scripts

`scripts/` generates the sources that are checked in rather than built: the icon font, the addon's own
icon, the map's icons. They run in the [uv](https://docs.astral.sh/uv/) environment `pyproject.toml`
pins, and each docstring says when to run it:

```bash
uv run scripts/convert_fonts.py
```

### Contributing

Pull requests are welcome.

- Run the tests before opening it. A bug fix ideally comes with a test that fails without the fix.
- Follow [docs/code_style.md](docs/code_style.md).

