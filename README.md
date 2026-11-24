# Markus

Yet another marker pack addon for [Nexus](https://raidcore.gg/Nexus).

## For players

**[Download the latest release](https://github.com/HasKha/gw2-markus/releases/latest)**

1. Download `Markus.dll` from the release page.
2. Put it in `<Guild Wars 2>/addons/`.
3. Enable it in Nexus.

If you had Marker Pack Editor (`MarkerPackEditor.dll`), delete it.

### Features

- View marker packs.
- Optionally, use the built-in editor.
- Support for API key.
- Support for LUA scripting.
- Feature parity with BlishHUD Pathing module.
- Draw markers behind in-game windows, characters, or terrain when used together with the [Good Render](https://github.com/HasKha/gw2-good-render).

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

### Contributing

Pull requests are welcome.

- Run the tests before opening it. A bug fix ideally comes with a test that fails without the fix.
- Follow [docs/code_style.md](docs/code_style.md).

### Docs

Every doc in `docs/` is listed in [CLAUDE.md](CLAUDE.md#docs), with when to read it.
