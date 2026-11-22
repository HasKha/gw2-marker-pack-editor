# Reference material

Where the ground truth lives. What the format facts actually *are* lives in
[format.md](format.md).

None of these are dependencies — nothing here is fetched or linked. They are read. Clone
the ones you need side by side with this repo; the paths below are written relative to
whatever directory you keep checkouts in.

## Format ground truth, in precedence order

When these disagree, the one higher in the list wins.

| Source | Where | Authority for |
|---|---|---|
| **TmfLib** | `github.com/dlamkins/TmfLib` | **Parsing.** Blish HUD's marker pack read/write library and the shared parsing spec, used by the Pathing module, the linter and the re-packer. Not bundled with Pathing — cloned separately |
| **Pathing** | `github.com/blish-hud/Pathing` | What attributes *do*: defaults, rendering, filtering, behaviors |
| **gw2pathing.com** | `https://gw2pathing.com/docs/marker-dev/attributes/<name>` | User-facing attribute reference. The old `blishhud.com/docs/markers/` URLs 302 here |
| **TaimiHUD** | `codeberg.org/TaimiHUD/TaimiHUD` | Explicitly partial — use only where the above are silent |

Useful files within them:

- `TmfLib/Reader/` — `PackReader`, `TrlFileReader`; `TmfLib/Writer/PackWriter.cs` (a
  re-packer, **not** a model for our writer); `TmfLib/NanoXml/` — the lenient parser;
  `TmfLib/Content/AsyncSafeZipArchive.cs` — resource path normalization;
  `TmfLib/Prototype/AttributeCollection.cs` — explicit vs aggregated attributes
- `Pathing/State/UserResources/Population/` — authoritative defaults, traced to GW2TacO
- `Pathing/Entity/Standard{Marker,Trail}*.cs` — per-attribute rendering behaviour; the
  `Standard*[attr].cs` filenames say which attribute each file implements
- `Pathing/ref/hlsl/` — `marker.hlsl`, `trail.hlsl`; the metres-to-inches conversion
  lives here
- `TaimiHUD/pathing/pack/` — parser; `TaimiHUD/src/space/` — world rendering

## Other references

| What | Where |
|---|---|
| Nexus loader and addon API | `github.com/RaidcoreGG/Nexus` — the loader's own source, not the API header we fetch. Read it when the question is what Nexus does with what we hand it |
| A hand-authored pack to test against | `github.com/HasKha/gw2-markers` — `scripts/trltools.py` corroborates the TRL layout, `scripts/shared.py` is the spec for the shape tools |
| Addon templates this project was seeded from | `github.com/HasKha/VfxDenoiser`, `github.com/HasKha/gw2-oops-wrong-template` |
| GW2TacO — the original implementation the defaults trace back to | `github.com/BoyC/GW2TacO`, `gw2tactical.h` |
| GoodRender — the service the viewer draws through | `github.com/HasKha/gw2-good-render` — `include/good_render.h` is the whole contract |

## For the viewer

Pathing is the behaviour the viewer copies. The files that define it, beyond the rendering
ones above:

| What | Where in `Pathing/` |
|---|---|
| The Lua environment and its sandbox | `Scripting/PathingGlobal.cs`, `Scripting/ScriptEngine.cs`, `Scripting/Lib/Std/LuaLibraryOS.cs` |
| The Lua API | `Scripting/Lib/*.cs`, `Scripting/Extensions/*ScriptExtensions.cs` |
| When scripts reset, how packs merge, per-map loading | `PackInitiator.cs` (`LoadMapFromEachPack`), `SharedPackCollection.cs` |
| Behaviors | `Behavior/StandardPathableBehavior.cs`, `Behavior/Filter/`, `Behavior/Modifier/` |
| Persisted state | `State/KvStates.cs` (script `Storage`), `State/BehaviorStates.cs`, `State/CategoryStates.cs` |
| Opacity, size and fade rules | `Entity/StandardMarker.World.cs` (`GetOpacity`, min/max size), `Entity/StandardTrail.World.cs` |
| Minimap and full map | `Entity/MapProjection.cs`, `Entity/StandardMarker.MiniMap.cs`, `UI/Controls/FlatMap.cs` |
| Settings players expect | `ModuleSettings.cs` |
| The pack catalog | `MarkerPackRepo/MarkerPackRepo.cs`, `MarkerPackRepo/MarkerPackPkg.cs` |

The Lua corpus is the scripts inside the test packs, Hero's pack above all; see
[viewer.md](viewer.md#the-census).

## Fetched dependencies

Sources land in `build/_deps/<name>-src/` after a configure — handy when checking what
an API actually offers (e.g. which imgui version we are pinned to).
