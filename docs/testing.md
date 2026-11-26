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
worse than a red one.

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
| `script_corpus_test.cpp` | every corpus `pack.lua` on each of its maps: reset, load, three ticks, every `script-*` function its pathables name. Fails on any console error, and on census entries for Hero's pack and Teh's Trails. A pack's own bug, which Pathing raises too, goes in `kPackBugs` with the reason |
| `behavior_test.cpp`, `behavior_data_test.cpp` | each filter and modifier against a hand-set MumbleLink; attributes, cron, timers, festivals |
| `map_test.cpp`, `map_scene_test.cpp` | the projection against Queensdale's rectangles; the map scene over a square whose continent units are metres |
| `catalog_test.cpp` | the catalog and downloads against a local fetch |
| `edited_test.cpp` | the editor's pack as a live map: revisions, eye toggles, deferred trail reads, picking |
| `blish_import_test.cpp` | a Blish HUD markers folder laid out as Pathing does |
| `http_test.cpp` | cancelling a request against a localhost server that never answers |
| `selection_test.cpp`, `trail_test.cpp` | the editor's click rules, pruning, both selections; trails and renaming `traildata` |

**What a frame costs** is measured offline, on a Release build: a hidden case builds and submits the world
scene over the corpus's busiest maps and prints milliseconds a frame. Run it before and after a change to
`scene.*`, `ribbon.*` or the sinks, on one machine.

```bash
bin/Release/markus_viewer_test.exe "[.bench]"
```

One case reaches the real catalog and is hidden from ctest. Run it after touching `http.*` or how the
catalog is read:

```bash
bin/Release/markus_viewer_test.exe "[.web]"
```

Not covered: undo, the editor's indexes, `saveToDirectory`'s asset half (the folder source is a real
checkout), the windows, and the sinks in game. Those are checked once in game per change, with what to look
for decided in advance.
