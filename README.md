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
mkdir build
cd build
cmake ..
cmake --open .
```

Output DLL lands in `bin/`. Copy it to `<GW2>/addons/` to load it.

## License

MIT — see [LICENSE](LICENSE).
