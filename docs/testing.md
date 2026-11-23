# Testing

How the addon is checked without the game, and what the tests are pointed at. Run them after any change
to `src/pack/`, and before calling any pack-layer work done:

```bash
ctest --test-dir build -C Release --output-on-failure
```

They assert the whole read → model → write → save round trip — every pack parses, the writer is stable,
comments and BOM and CRLF survive, a save reopens as the same model — against the real packs rather than
fixtures. No game needed, and a few seconds: about six run one case at a time, which is how ctest
runs by default, and half that with `-j8`. Nearly all of it is three sweeps over the corpus — the format in
`corpus_test.cpp`, the bake, and the scripts — and a process per case.

**A fix comes with a case that fails before it.** A fix without one is a fix that comes back. Never verify
pack-layer work by launching the game, or by hand.

## Two tools, two questions

| Target | Answers | Does |
|---|---|---|
| `mpe_test` | *is it broken?* | asserts, under Catch2, and fails a build |
| `mpe_viewer_test` | *is the viewer broken?* | the same, for `src/viewer/`, linked without the editor |
| `mpe_probe` | *what does this pack look like?* | prints and lints: what it found, what it could not parse, what a save would change |

Both are worth having. The probe's lint, its byte-offset anchoring report and its round-trip diffs are how a
format question gets settled in the first place; every format claim in [format.md](format.md) was measured
with it. Turning it into assertions would throw that away, and turning the tests into printf would put us
back to reading nine blocks of numbers and remembering what they were last time.

```bash
mpe_probe --save-test <scratch-dir> <pack.zip> <pack-folder>
```

`<scratch-dir>` goes under `tmp/`.

## The corpus

Real packs, not fixtures: by default the packs in `<Documents>/Guild Wars 2/addons/blishhud/markers`,
resolved at configure time, plus the `gw2-markers` checkout as a folder source. 947 XML files, 3,863 entries
in the largest archive, comments and BOMs and CRLFs and mixed-case paths that no synthetic file would think
to contain. Nothing is checked in: these are other people's packs, some of them hundreds of megabytes, and
testing against what authors actually ship is the whole point. `mpe_corpus` resolves the location, so the
tests and the probe cannot disagree about it.

Point it elsewhere without reconfiguring:

| Variable | What |
|---|---|
| `MPE_TEST_PACK_DIR` | folder of `.zip`/`.taco` packs |
| `MPE_TEST_FOLDER_PACK` | an *unzipped* pack, to check a folder source matches its zip |

Both exist as CMake cache variables too, for setting the default at configure time.

**A test whose pack is missing skips with a reason; a corpus missing entirely fails**, because a green run
that tested nothing is the one outcome worse than a red one.

### The packs worth having

Whatever is in the folder gets swept, but two shapes matter, and the claims in [plan.md](plan.md) were
measured against them:

| Pack | Shape | Why |
|---|---|---|
| A hand-authored pack | ~80 entries, ten xml files at the root, `Data/`, comments throughout | the round-trip target — losing a comment here is a bug |
| `hero.blish.pack.zip` | 1918 entries, 64 xml, 140 trl, 1375 png; `_markerCategories.xml` + `map<id>.xml` | large, popular, and already TmfLib `PackWriter` output — the scale and machine-generated case |

Plus one pack as an **unzipped folder**, which must produce the same model as the zip built from it. That
zip is built by the test rather than taken from the corpus: a checkout and a shipped archive of "the same"
pack drift the moment either is edited, and the test would then be reporting the drift instead of the
reader.

## The viewer

`mpe_viewer_test`, from `test/viewer/`, links `mpe_viewer` and nothing of the editor; ctest runs it with
the rest. A viewer test that needs an editor header does not compile, which is the point. Nothing in it
needs a device: the quad path draws into a real `ImDrawList` made from a bare ImGui context, the texture
cache runs against an `AddonAPI` whose one texture function is a fake, and the GoodRender sink submits to
a moment whose `world_quad` records what it is handed. Rules get a small pack written to a temp folder;
the pipeline as a whole gets `haskha_markers.zip`, loaded through the registry and built into a scene.

Scripts are tested the same two ways. `script_test.cpp` writes a pack with a `pack.lua` and asks the engine
what a line of Lua prints, one case per rule of the API: call styles, the census, errors and the time
budget, the sandbox, `os`, LSON, markers read and written. `script_corpus_test.cpp` runs every corpus pack
that has a `pack.lua` on each of its maps — reset, load, three ticks a second apart — then calls every
`script-*` function the map's pathables name: every once, tick and filter first, as a first frame would,
then each marker focused, triggered and let go. It fails on any error in the console; for Hero's pack and
Teh's Trails it fails on any census entry too, listing them. A pack whose script starts failing after an
API change shows up there with its file and line, each distinct error once.

An error that is the pack's own — one Pathing raises too, read off the pack's files — goes in that test's
`kPackBugs` with the reason, by pack and the error's text, rather than loosening the check.

Behaviors are tested the same way: `behavior_test.cpp` writes a pack of POIs a hundred metres apart and
walks a hand-set MumbleLink between them, a case per rule; `behavior_data_test.cpp` covers what they read —
attributes with Pathing's parsing quirks, cron schedules, `timers.txt`, the festival answers — on its own. `test/viewer/test_camera.h`
is the camera whose projections can be written by hand, for any test that projects, and
`test/viewer/draw_list.h` an ImGui draw list with no device, for any test that draws.

The maps are tested over a kilometre square whose continent coordinates are its own metres, so every
expected position can be worked out by hand (`map_scene_test.cpp`); the projection itself against
Queensdale's rectangles, as the API gives them and as Pathing's fallback file has them (`map_test.cpp`).
Nothing asks the API: the map catalog takes its request as an argument.

The pack catalog likewise: `catalog_test.cpp` reads a catalog written in the test, and runs downloads
against a fetch of its own that copies the corpus's smallest pack. One case reaches the real catalog and
downloads its smallest pack; it is hidden, so ctest never runs it. Run it by hand after touching `http.*`
or how the catalog is read:

```bash
bin/Release/mpe_viewer_test.exe "[.web]"
```

## Above the pack layer

`mpe_test` links `mpe_editor`, so anything in the editor a test reaches is linked in, and what touches
Nexus goes through an `AddonAPI*` left null ([architecture.md](architecture.md#build-targets)).
`selection_test.cpp` covers the click rules, pruning and the two selections; `trail_test.cpp` covers
creating trails, following the selection and renaming `traildata`. The rest of the editor is plain
functions over `editor::State` where it can be, which is what makes it testable the same way. The windows
and the world drawing are not: each change to them is checked in game once, with what to look for decided
before loading in.
