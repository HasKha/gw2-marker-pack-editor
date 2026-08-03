# Marker pack format — ground truth

Everything here is taken from the reference implementations, not from guesswork. Their
clone paths, the precedence order between them, and the specific files worth reading are
in [references.md](references.md). In short: **TmfLib** for parsing, **Pathing** for what
attributes do, gw2pathing.com for the attribute reference, TaimiHUD only where those are
silent.

## Archive layout

A pack is a zip (`.zip` or `.taco`) containing one or more `.xml` files plus resources
(`.png` icons, `.trl` trails). Nothing requires the xml files to be at the root, and
nothing requires resources to live under `Data/`.

**Resource paths are normalized before lookup.** TmfLib's `AsyncSafeZipArchive` does:

```
path.Replace("\\", "/").Replace("//", "/").ToLowerInvariant().Trim().TrimStart('.')
```

So `Data/Skills/Blink.png`, `data\skills\blink.png` and `./Data/Skills/Blink.png` all
resolve to the same entry. Our zip module must match this or packs that load in Blish
will appear to have missing icons here.

## XML

Elements (compared case-insensitively): `overlaydata`, `markercategory`, `pois`, `poi`,
`trail`, `route`.

- `markercategory` nodes nest and live directly under `overlaydata`.
- `poi`, `trail` and `route` live under a `pois` node.
- **`route` is a third pathable type.** TmfLib reads it; its `PackWriter` throws
  `InvalidOperationException` on it. Our reader must not choke on it.

Attribute names are **lowercased** on read (`NanoXmlBase`), so lookups are
case-insensitive. Vendor prefixes (`bh-`, configured via
`PackReaderSettings.VenderPrefixes`) are stripped and the remainder treated as a normal
attribute.

A POI's category is the dot-joined path of ancestor `name` attributes, matched
case-insensitively (`PathingCategory` uses `StringComparer.OrdinalIgnoreCase`).

### Explicit vs aggregated attributes

TmfLib treats this as a first-class distinction, and so should we:

- `ExplicitAttributes` — set on this node itself.
- `GetAggregatedAttributeValue(name)` — resolved through the category chain, with the
  node's own value winning.

The property panel needs both: show the effective value, but make clear whether it is
explicit here or inherited, and let the user override or clear it.

### Real packs are not strictly valid XML

TmfLib does **not** use a standard XML parser. It ships a vendored, lenient `NanoXml`
parser. That strongly implies packs in the wild contain markup a conformant parser
rejects, and Blish HUD accepts them anyway.

**Risk for us:** pugixml is a conformant parser. A pack that loads in Blish may fail to
load here. Must be tested against real third-party packs in M1, not just `gw2-markers`.
If it bites, options are pugixml's lenient parse flags, a pre-sanitizing pass, or
porting NanoXml's tolerance.

## Coordinates and units

- Marker XML positions (`xpos`/`ypos`/`zpos`) and `.trl` points are **metres, Y-up** —
  the same space MumbleLink reports.
- Blish HUD's internal world space is **Z-up**. It swaps axes on load; that swap is
  Blish's business, not part of the file format.
- **Distance-like attributes are in inches**, not metres. `marker.hlsl` computes
  `distance(...) / 0.0254f` before comparing against `fadeNear`/`fadeFar`. So
  `fadeNear="3000"` means 3000 inches ≈ 76 m. This applies to `fadenear`, `fadefar`,
  `triggerrange`, `inforange` and friends.
- `heightoffset` is applied along the up axis, default **1.5**.

## Rendering semantics (from Pathing)

Markers, per `Entity/StandardMarker.World.cs`:

- Quad is the unit square scaled by `iconSize * 2`.
- Billboarded toward the camera **unless** any of `rotate`/`rotate-x/y/z` is set, in
  which case it is a fixed-orientation quad with an explicit rotation.
- After projection, on-screen height is clamped between `minsize * 4` and `maxsize * 4`
  **pixels**, and the model matrix is rescaled to satisfy it.
- Opacity is `alpha * fade`, where fade ramps linearly between `fadeNear` and `fadeFar`.
- `occlude="1"` switches the blend state to opaque.
- `cull` maps to a rasterizer cull direction.

Trails: `Entity/StandardTrail*.cs`, `ref/hlsl/trail.hlsl`. `animspeed` scrolls the
texture along the ribbon; `iswall="1"` orients the ribbon vertically; `trailscale`
scales ribbon width and texture repeat.

## Defaults

Straight from `Pathing/State/UserResources/Population/*.cs`, which cite GW2TacO's
`gw2tactical.h` as their source. These are user-overridable in Pathing's settings, but
they are the defaults a pack author writes against.

### Markers

| Attribute | Default |
|---|---|
| `alpha` | 1 |
| `color` / `tint` | white |
| `fadenear` | -1 (disabled) |
| `fadefar` | -1 (disabled) |
| `heightoffset` | 1.5 |
| `iconsize` | 1 |
| `minsize` | 5 |
| `maxsize` | 2048 |
| `triggerrange` | 2 |
| `mapdisplaysize` | 20 |
| `mapvisibility` | true |
| `minimapvisibility` | true |
| `ingamevisibility` | true |
| `scaleonmapwithzoom` | true |
| `canfade` | true |
| `cull` | none |
| `rotate-x/y/z` | 0 |
| `title-color` | white |
| `guid` | empty |

### Trails

| Attribute | Default |
|---|---|
| `alpha` | 1 |
| `animspeed` | 1 |
| `color` / `tint` | white |
| `cull` | none |
| `fadenear` | 700 |
| `fadefar` | 900 |
| `trailscale` | 1 |
| `mapvisibility` | true |
| `minimapvisibility` | true |
| `ingamevisibility` | true |

Note trails default to *enabled* fade (700/900 inches ≈ 18/23 m) while markers default
to disabled. Easy to get wrong.

## Documented attribute set

From gw2pathing.com's attribute reference — grouped pages, so several cover multiple
raw attribute names (e.g. "Fade" covers `fadenear`/`fadefar`, "Position" covers
`xpos`/`ypos`/`zpos`):

Achievement, Alpha, AnimSpeed, AutoTrigger, Behavior, Bounce, CanFade, Color, Copy,
Cull, DefaultToggle, DisplayName, Fade, Festival, GUID, HeightOffset, IconFile,
IconSize, Info, InvertBehavior, IsHidden, IsSeparator, IsWall, MapDisplaySize, MapID,
MapType, Mount, Occlude, Position, Profession, Race, Raid, ResetGUID, ResetLength,
Rotate, ScaleOnMapWithZoom, Schedule, Script, ShowHide, Size, Specialization, Texture,
Tip, Toggle, TrailData, TrailScale, TriggerRange, Type, Visibility.

Each page is at `https://gw2pathing.com/docs/marker-dev/attributes/<lowercase-name>`.
Fetch these when filling in the attribute table in M1 — they give value ranges and
semantics the source alone does not.

`GUID` is base64 of 16 raw bytes, not canonical UUID text.

## TRL

Per `TmfLib/Reader/TrlFileReader.cs` and `gw2-markers/scripts/trltools.py`:

```
int32   version    (must be 0; TmfLib bails out otherwise)
int32   map_id
float32 x, y, z    repeated to EOF, in the same order as xpos/ypos/zpos
```

- **`(0,0,0)` terminates a segment.** TmfLib splits the point stream into
  `TrailSection`s on it. A pack can therefore hold several disjoint polylines in one
  file, and we must preserve that when writing.
- A zero-length or truncated file yields no sections rather than an error.
- TmfLib reads the three floats and assigns them `x, z, y` before building its `Vector3`
  — that is its Y-up to Z-up swap, not a quirk of the file layout.

## Writing

**TmfLib's `PackWriter` is a re-packer, not a round-trip writer.** Do not model ours on
it. It:

- collapses every category into a single `_markerCategories.xml`,
- regroups all pathables into one `map<id>.xml` per map,
- drops all comments and original formatting,
- lowercases every resource path,
- omits attributes equal to their default,
- throws on routes.

That is the right behaviour for a distribution re-packer and exactly wrong for an
editor operating on a hand-authored pack. Our writer keeps the original file split,
comment placement, attribute order and original attribute casing, and only rewrites
what actually changed. This is why `docs/plan.md` open question 2 matters.
