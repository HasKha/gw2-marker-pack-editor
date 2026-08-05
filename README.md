# MarkerPackEditor

A [Nexus](https://raidcore.gg/Nexus) addon for creating and editing GW2 marker packs
(the `.zip`/`.taco` packs consumed by the Blish HUD Pathing module and TaimiHUD).

Edit a pack in-game: browse its categories, POIs and trails in a tree, edit attributes
in a form, see everything drawn in the world at its real position, and drag markers
around with a 3D gizmo.

## Status

Early development. See [docs/plan.md](docs/plan.md) for the roadmap and current state.

Other docs: [architecture.md](docs/architecture.md) (how it is built),
[format.md](docs/format.md) (the marker pack format),
[references.md](docs/references.md) (the reference implementations).

## Build

```
cmake -B build
cmake --build build --config Release
```

Output DLL lands in `bin/`. Copy it to `<GW2>/addons/` to load it. For the IDE, run
`cmake --open build`.

## Test

```
ctest --test-dir build -C Release --output-on-failure
```

The pack layer is tested against **real marker packs** rather than fixtures — by default
the ones in `<Documents>/Guild Wars 2/addons/blishhud/markers`. None of them ship with this
repo, so point the suite at your own with `MPE_TEST_PACK_DIR` (and `MPE_TEST_FOLDER_PACK`
for an unzipped pack). Tests whose pack is missing skip and say so.

`mpe_probe` is the companion tool: it loads a pack and prints what it found, what it could
not parse, and what a save would change. Reach for it when the question is what a pack
looks like rather than whether something is broken.

## License

MIT — see [LICENSE](LICENSE).
