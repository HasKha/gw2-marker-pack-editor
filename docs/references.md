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

## Fetched dependencies

Sources land in `build/_deps/<name>-src/` after a configure — handy when checking what
an API actually offers (e.g. which imgui version we are pinned to).
