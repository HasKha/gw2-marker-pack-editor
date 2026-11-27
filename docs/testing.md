# Testing

```bash
ctest --test-dir build -C Release --output-on-failure
```

A few seconds, faster with `-j8`, no game needed. Most of the time is three sweeps over the real packs: the
format (`corpus_test.cpp`), the bake, and the scripts.

**A fix comes with a case that fails before it.** Never verify pack-layer work in game or by hand.

## Targets

| Target | Does |
|---|---|
| `markus_{pack,http,viewer,editor}_test` | Catch2 assertions, one binary per library, linking that library and nothing above it |
| `<folder>_layering` | fails on an include of a library above it ([architecture.md](architecture.md#build-targets)) |
| `markus_probe` | prints and lints a pack: what it found, what failed to parse, what a save would change |

The probe is how a format question gets settled; every measurement in [format.md](format.md) came from it.

```bash
markus_probe --save-test <scratch-dir under tmp/> <pack.zip> <pack-folder>
```

The format's corpus sweep is in `markus_viewer_test`, not `markus_pack_test`: the same pass checks the bake,
and loading the corpus is most of its time.

## The corpus

Real packs, never checked in: by default `<Documents>/Guild Wars 2/addons/blishhud/markers`, resolved at
configure time, plus the `gw2-markers` checkout as a folder source. `markus_corpus` resolves it.

| Variable (environment or CMake cache) | What |
|---|---|
| `MARKUS_TEST_PACK_DIR` | folder of `.zip`/`.taco` packs |
| `MARKUS_TEST_FOLDER_PACK` | an unzipped pack, checked against a zip the test builds from it |

**A missing pack skips with a reason; a missing corpus fails**, since a green run that tested nothing is
worse than a red one. CI has no corpus and excludes the cases that fail without one by name, so **a case
that needs the whole corpus has "corpus" in its name** ([releasing.md](releasing.md#what-ci-does-not-test)).

Two shapes matter: a hand-authored pack with comments throughout (the round-trip target: losing a comment is
a bug), and `hero.blish.pack.zip`, large and already TmfLib `PackWriter` output.

## What covers what

Rules get a small pack written to a temp folder; the pipeline gets `haskha_markers.zip` through the registry
into a scene. Nothing needs a device: the quad path draws into a bare-context `ImDrawList`
(`test/viewer/draw_list.h`), textures use a fake `AddonAPI`, the GoodRender sink records what `world_quad`
is handed, and `test/viewer/test_camera.h` is a camera with hand-written projections.

| File | Covers |
|---|---|
| `corpus_test.cpp`, `bake_test.cpp` | every pack parses, saves stably, bakes; a baked value equals the editor's effective value |
| `script_test.cpp` | one case per API rule: call styles, census, errors and budget, sandbox, `os`, LSON |
| `script_corpus_test.cpp` | every corpus `pack.lua` on each of its maps: reset, load, three ticks, every `script-*` function its pathables name, three ticks more for what those set going. Fails on any console error, and on census entries for Hero's pack and Teh's Trails. A pack's own bug, which Pathing raises too, goes in `kPackBugs` with the reason |
| `behavior_test.cpp`, `behavior_data_test.cpp` | each filter and modifier against a hand-set MumbleLink; attributes, cron, timers, festivals |
| `lua_parity_test.cpp` | every case of `lua_parity/cases.lua` against NeoLua's output for it, and the libraries' names against NeoLua's; each difference listed with its reason, and a listed one that has gone away fails |
| `interaction_test.cpp` | the info window's layout, and that only its close button takes the mouse |
| `map_test.cpp`, `map_scene_test.cpp` | the projection against Queensdale's rectangles; the map scene over a square whose continent units are metres |
| `catalog_test.cpp` | the catalog and downloads against a local fetch |
| `edited_test.cpp` | the editor's pack as a live map: revisions, eye toggles, deferred trail reads, picking |
| `blish_import_test.cpp` | a Blish HUD markers folder laid out as Pathing does |
| `http_test.cpp` | cancelling a request against a localhost server that never answers |
| `selection_test.cpp`, `trail_test.cpp` | the editor's click rules, pruning, both selections; trails and renaming `traildata` |

**What a frame costs** is measured offline, on a Release build: a hidden case takes the corpus's busiest maps
and prints milliseconds for rebuilding the live map, building and submitting the world scene, building the
minimap, and drawing the full map's trails and icons at three zooms. Run it before and after a change to
`scene.*`, `ribbon.*`, `map_scene.*`, `runtime.*` or the sinks, on one machine.

```bash
bin/Release/markus_viewer_test.exe "[.bench]"
```

**What loading a pack costs** is the same tag's per-pack table: the archive's index, inflating and parsing
the XML, building the model, the bake, the `.trl` files, sampling trail colours, the scripts, and
`registry::loadPack`'s own total. Run it before and after a change to anything `loadPack` calls. The comma
is escaped because Catch2 reads a bare one as "or":

```bash
bin/Release/markus_viewer_test.exe "loading each pack\, timed"
```

The same tag times what unload frees: the corpus's packs, a live map and its scripts. Measured at about
100 ms in Release and under a second in Debug, so a slow unload is waiting on a thread, most often the
pack loader still reading packs, rather than freeing.

One case reaches the real catalog and is hidden from ctest. Run it after touching `http.*` or how the
catalog is read:

```bash
bin/Release/markus_viewer_test.exe "[.web]"
```

Not covered: undo, the editor's indexes, `saveToDirectory`'s asset half (the folder source is a real
checkout), the windows, and the sinks in game. Those are checked once in game per change, with what to look
for decided in advance.

## The NeoLua oracle

How Pathing's Lua behaves is settled by running it. `scripts/neolua_oracle` is a .NET Framework 4.8 tool
that runs `test/viewer/lua_parity/cases.lua` under the NeoLua package Pathing ships, built as Pathing builds
it, and writes `expected.txt` and `surface.txt` beside it. Both are checked in, so the test needs no .NET.
Run it again after adding or changing a case; it needs the .NET SDK, and restores NeoLua from NuGet:

```bash
dotnet run -c Release --project scripts/neolua_oracle
```

A question about NeoLua becomes a case: a few lines that print the answer, with nothing in them that changes
between runs. `cases.lua`'s header says how a case is written. A new case that differs goes in one of the
test's lists, with its reason. The lists say whether matching it would take another interpreter, whether
NeoLua fails there, or whether it is not matched yet. A listed case passes whatever Markus prints, so a
case shows one difference: keep it small.
