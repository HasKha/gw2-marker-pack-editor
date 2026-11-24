# Reference material

Where the ground truth lives; the facts themselves are in [format.md](format.md) and the code. None of these
are dependencies: clone the ones you need beside this repo.

## Format, in precedence order

When these disagree, the one higher in the list wins.

| Source | Where | Authority for |
|---|---|---|
| **TmfLib** | `github.com/dlamkins/TmfLib` | **Parsing.** Blish HUD's pack reader and writer, cloned separately from Pathing |
| **Pathing** | `github.com/blish-hud/Pathing` | What attributes *do*: defaults, rendering, filtering, behaviors |
| **gw2pathing.com** | `https://gw2pathing.com/docs/marker-dev/attributes/<name>` | The user-facing attribute reference, with value ranges the source does not give |
| **TaimiHUD** | `codeberg.org/TaimiHUD/TaimiHUD` | Partial; only where the above are silent |

Useful files:

- `TmfLib/Reader/` — `PackReader`, `TrlFileReader`; `TmfLib/NanoXml/` — the lenient parser;
  `TmfLib/Content/AsyncSafeZipArchive.cs` — path normalization; `TmfLib/Prototype/AttributeCollection.cs` —
  explicit vs aggregated attributes
- `Pathing/State/UserResources/Population/` — defaults, traced to GW2TacO
- `Pathing/Entity/Standard{Marker,Trail}*.cs` — rendering per attribute; `Standard*[attr].cs` names the
  attribute each implements
- `Pathing/ref/hlsl/` — `marker.hlsl`, `trail.hlsl`

## Pathing's behaviour beyond rendering

| What | Where in `Pathing/` |
|---|---|
| The Lua environment and its sandbox | `Scripting/PathingGlobal.cs`, `Scripting/ScriptEngine.cs`, `Scripting/Lib/Std/LuaLibraryOS.cs` |
| The Lua API | `Scripting/Lib/*.cs`, `Scripting/Extensions/*ScriptExtensions.cs` |
| Script resets, pack merging, per-map loading | `PackInitiator.cs` (`LoadMapFromEachPack`), `SharedPackCollection.cs` |
| Behaviors | `Behavior/StandardPathableBehavior.cs`, `Behavior/Filter/`, `Behavior/Modifier/` |
| Persisted state | `State/KvStates.cs`, `State/BehaviorStates.cs`, `State/CategoryStates.cs` |
| Opacity, size and fade | `Entity/StandardMarker.World.cs` (`GetOpacity`), `Entity/StandardTrail.World.cs` |
| Minimap and full map | `Entity/MapProjection.cs`, `Entity/Standard{Marker,Trail}.MiniMap.cs`, `UI/Controls/FlatMap.cs`, `State/MapStates.cs`, `ref/fallback/mapDetails.json` |
| Settings | `ModuleSettings.cs` |
| The pack catalog | `MarkerPackRepo/`, `Utility/PackHandlingUtil.cs`, `UI/Views/PackRepoView.cs`, `UI/Controls/MarkerPackHero.cs` |

What else defines the API scripts are written against:

| What | Where | Read for |
|---|---|---|
| **Blish HUD** | `github.com/blish-hud/Blish-HUD` — `Blish HUD/GameServices/Gw2MumbleService.cs`, `Gw2Mumble/` | every `Mumble.*` member: scripts get Blish HUD's service, so its names, units and types are the API, not MumbleLink's. `GraphicsService.cs` (`GetScaleRatio`) for the 0.897 Pathing's map corrects by |
| **NeoLua** | `github.com/neolithos/neolua` | how a .NET object behaves in Lua: call styles, conversions, `Int32`/`Float` arithmetic |
| **Gw2Sharp** | `github.com/Archomeda/Gw2Sharp` — `Gw2Sharp/Models/` | the enum names `Mumble.*` returns as strings, and their numbers |
| **Cronos** | `github.com/HangfireIO/Cronos` — `src/Cronos/CronExpression.cs` | the `schedule` filter's cron expressions |
| **MonoGame 3.8.0.1641** | `github.com/MonoGame/MonoGame` — `Vector3.cs`, `Color.cs`, `GameTime.cs` | the value types scripts hold |
| **GW2 API maps** | `https://api.guildwars2.com/v2/maps?ids=all` | `map_rect` (inches, y up) and `continent_rect` (y down) |

Pathing's Lua API has no documentation (`gw2pathing.com/docs/` is 404); the source is the reference.

## Other

| What | Where |
|---|---|
| Nexus | `github.com/RaidcoreGG/Nexus` — the loader's source, for what Nexus does with what we hand it |
| GoodRender | `github.com/HasKha/gw2-good-render` — `include/good_render.h` is the contract; `README.md` and `docs/how_it_works.md` for how a subscriber should behave |
| A hand-authored pack | `github.com/HasKha/gw2-markers` — `scripts/trltools.py` corroborates the TRL layout, `scripts/shared.py` specs the shape tools |
| GW2TacO | `github.com/BoyC/GW2TacO`, `gw2tactical.h` — where the defaults trace back to |

Fetched dependencies land in `build/_deps/<name>-src/` after a configure.
