# Reference material

Local clone paths and external links. What the format facts actually *are* lives in
[format.md](format.md).

## Format ground truth, in precedence order

When these disagree, the one higher in the list wins.

| Source | Path | Authority for |
|---|---|---|
| **TmfLib** | `C:\Users\Giorgio\Documents\git\TmfLib` | **Parsing.** Blish HUD's marker pack read/write library and the shared parsing spec, used by the Pathing module, the linter and the re-packer. Not bundled with Pathing — cloned separately from `github.com/dlamkins/TmfLib` |
| **Pathing** | `C:\Users\Giorgio\Documents\git\Pathing` | What attributes *do*: defaults, rendering, filtering, behaviors |
| **gw2pathing.com** | `https://gw2pathing.com/docs/marker-dev/attributes/<name>` | User-facing attribute reference. The old `blishhud.com/docs/markers/` URLs 302 here |
| **TaimiHUD** | `C:\Users\Giorgio\Documents\git\TaimiHUD` | Explicitly partial — use only where the above are silent |

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

| What | Path / link |
|---|---|
| The pack being edited | `C:\Users\Giorgio\Documents\git\gw2-markers` — `scripts/trltools.py` corroborates the TRL layout |
| Nexus loader and addon API | `C:\Users\Giorgio\Documents\git\Nexus` |
| Addon templates this project was seeded from | `C:\Users\Giorgio\Documents\git\VfxDenoiser`, `C:\Users\Giorgio\Documents\git\gw2-oops-wrong-template` |
| GW2TacO — the original implementation the defaults trace back to | `github.com/BoyC/GW2TacO`, `gw2tactical.h` |

## Fetched dependencies

Sources land in `build/_deps/<name>-src/` after a configure — handy when checking what
an API actually offers (e.g. which imgui version we are pinned to).
