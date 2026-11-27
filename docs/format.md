# Marker pack format

What the reference implementations do, with the corpus measurements that settled it. Sources and their
precedence are [references.md](references.md): TmfLib for parsing, Pathing for what attributes do.

## Archive

A zip (`.zip` or `.taco`) of `.xml` files and resources (`.png`, `.trl`), anywhere in the archive. **Zipped
is the shipping format**: Blish HUD does not reliably load images from a folder.

Resource paths are normalized before lookup, as TmfLib's `AsyncSafeZipArchive`:

```
path.Replace("\\", "/").Replace("//", "/").ToLowerInvariant().Trim().TrimStart('.')
```

## XML

Elements, case-insensitive: `overlaydata`, `markercategory` (nested, under `overlaydata`), `pois`, and under
it `poi`, `trail`, `route`. **`route` is dead**: TmfLib's `UnpackRoutePoi` returns null and no corpus pack has
one; we carry it through untouched.

Attribute names are lowercased on read. Vendor prefixes (`bh-`) are stripped. A POI's category is the
dot-joined `name` path, case-insensitive.

TmfLib keeps a POI missing `mapid` or any of `xpos`/`ypos`/`zpos`, and reads both through the category
chain: `mapid` is often a category's. Pathing places a missing coordinate at 0. `hero.blish.pack` has
679 such POIs, hidden anchors for toggles and for its scripts, which look them up by guid.

pugixml parses every corpus file (937 files, 146k elements); NanoXml's leniency is not needed.

### A trail's map id comes from its .trl

`UnpackTrailPoi` overwrites the XML `mapid` with the `.trl` header's, so a disagreement is a dead attribute,
not an error. We read the header too: `BakedTrail::map_id` is unknown until the file is read (see
`viewer/live/edited.cpp`). The editor still writes the XML `mapid`, since readers use it without opening the
`.trl`, and `trail_edit::setMapId` writes both.

A trail with no `traildata`, or whose file is missing or fails to parse, is dropped.

### Explicit vs aggregated

TmfLib's `ExplicitAttributes` is what the node sets; `GetAggregatedAttributeValue` resolves through the
category chain, the node's own value winning. The property panel shows both.

### A category carries almost every attribute

A `markercategory` is where an author sets `iconSize`, `iconFile`, `fadeNear` or `behavior` for a subtree:

1. `PathableCategoryBuilder.FromNanoXmlNode` strips only `name`, `isseparator`, `defaulttoggle`, `ishidden`
   and keeps the rest (`displayname` too).
2. `PathingCategory.AttributeParent => Parent`: a nested category inherits as a marker does.
3. `StandardMarker` and `StandardTrail` read everything through `GetAggregatedAttributes()`.

Not category-settable: position and identity (`xpos`/`ypos`/`zpos`, `guid`, `type`), the category's
structural attributes, and **`trailData`**: `UnpackTrailPoi` checks the *explicit* collection for it, so one
from a category drops the trail.

A category may hold any number of trails; `type` only files a pathable.

### Which attributes each element uses

Traced to the Pathing file that reads each.

| | Attributes |
|---|---|
| **Category only** | `name`, `DisplayName`, `IsSeparator`, `IsHidden`, `DefaultToggle` |
| **Marker only** | `xpos`/`ypos`/`zpos`, `iconFile`, `iconSize`, `heightOffset`, `minSize`, `maxSize`, `mapDisplaySize`, `scaleOnMapWithZoom`, `occlude`, `rotate`(+`-x`/`-y`/`-z`), `title`, `title-color`, `text`, `tip-name`, `triggerRange`, `autoTrigger`, `resetLength`, `behavior`, `invertBehavior`, `resetGuid`, `toggle`, `toggleCategory`, `show`, `hide`, `info`, `infoRange`, `bounce`(+`-height`/`-delay`/`-duration`) |
| **Trail only** | `trailData`, `texture`, `animSpeed`, `trailScale`, `isWall` |
| **Both pathables** | `type`, `MapID`, `GUID`, `alpha`, `color`/`tint`, `cull`, `canFade`, `fadeNear`, `fadeFar`, `inGameVisibility`, `mapVisibility`, `miniMapVisibility`, and every filter: `festival`, `mapType`, `mount`, `profession`, `race`, `specialization`, `raid`, `schedule`(+`-duration`), `script-*` |
| **Category *and* marker** | `achievementId`, `achievementBit`, `tip-description`, `copy`, `copy-message` |

The last row is read off the category itself by `CategoryAttributeUtil.cs` and
`CategoryContextMenuStripItem.cs`: the achievement greys out the menu entry, `tip-description` is its
tooltip, and clicking a separator copies `copy`.

`resetLength` and `triggerRange` on a trail are dead: `StandardTrail.Populate` never calls the functions that
read them. Packs set `mapDisplaySize` on trails (169 times), also dead.

`fadecenter` is Pathing's legacy alias for `canFade`. It is not in our table; no corpus pack uses it.

### What pugixml does not preserve

Parsed with `parse_full | parse_ws_pcdata` and written with `format_raw`, the tree survives: comments,
attribute order and casing, values. Lost:

1. **Whitespace inside a tag**: ` />` and attribute lists wrapped over lines.
2. **The UTF-8 BOM** and 3. **CRLF**: both recorded on read and reapplied by the writer.

Blank lines between elements are dropped too. So a pack reformats once on its first save, then is stable.

## Coordinates and units

- `xpos`/`ypos`/`zpos` and `.trl` points are **metres, Y-up**, MumbleLink's space. Blish HUD swaps to Z-up
  internally.
- **Distance attributes are inches**: `fadeNear`, `fadeFar`, `triggerRange`, `infoRange`. `marker.hlsl`
  divides metres by 0.0254.

## Rendering (Pathing)

Markers, `Entity/StandardMarker.World.cs`:

- The quad is the unit square scaled by `iconSize * 2`, billboarded unless any of `rotate`/`rotate-x/y/z` is
  present; `rotate="0,0,0"` still disables billboarding. Rotations are degrees, applied X, Y, Z, to a quad
  flat on the ground.
- **UVs come from each vertex's sign**: `new Vector2(vert.X < 0 ? 1 : 0, vert.Y < 0 ? 1 : 0)`, so both axes are
  inverted from the obvious mapping and a rotated marker draws turned 180° if this is missed.
  `Matrix.CreateBillboard` cancels the horizontal inversion (its cross product flips sign between Blish's
  right-handed Z-up and Mumble's left-handed Y-up) and the corner order absorbs the vertical: a billboard
  ends up with the plain mapping, a rotated marker does not.
- **A billboard faces the ray to its centre**, not the image plane, so off-centre it projects tilted and
  sheared, and its size goes as 1/depth, not 1/distance. Blish projects the four corners; sizing by
  distance is 1.6x too small at the screen edge at a 70° FOV.
- **Size is clamped between `minSize * 2` and `maxSize * 2` real pixels.** Blish clamps
  `bounds.HalfExtents.Y * 2 * Viewport.Height` in NDC between `* 4` bounds, which is twice the pixel height.
  The default stops shrinking at 10 px.
- Opacity is `alpha * fade`, linear between `fadeNear` and `fadeFar` (`GetOpacity`); `marker.hlsl` fades
  again per pixel with a noise dissolve. Either fade negative turns both off. The shader is handed Max View
  Distance as its far limit, so markers are cut off at 25000 inches (635 m) by default.
- `occlude="1"` draws opaque with zero alpha, erasing what is behind it from Blish's overlay.
- `cull`: `None`, `Clockwise` or `CounterClockwise`, any case, else none.

Trails, `Entity/StandardTrail.World.cs` and `trail.hlsl`: `animSpeed` scrolls the texture, `isWall="1"`
stands the ribbon up, `trailScale` scales width and texture repeat. `fadeNear` is never read: a trail is whole
up to `fadeFar`. From `BuildTrailSection`:

```
TRAIL_WIDTH  = 20 * 0.0254f          // 0.508 m half-width
normalOffset = TRAIL_WIDTH * trailScale
offset       = cross(pathDirection, isWall ? cross(pathDirection, down) : down)
vertices     = curPoint ± normalize(offset) * normalOffset
UV.v         = distanceAlongPath / (TRAIL_WIDTH * 2)
```

`down` is `(0,-1,0)` in the file's Y-up space.

## Defaults

From `Pathing/State/UserResources/Population/*.cs`.

| Attribute | Marker | Trail |
|---|---|---|
| `alpha` | 1 | 1 |
| `color` / `tint`, `title-color` | white | white |
| `fadenear` / `fadefar` | -1 / -1 (off) | **700 / 900** |
| `heightoffset` | 1.5 | |
| `iconsize` | 1 | |
| `minsize` / `maxsize` | 5 / 2048 | |
| `triggerrange` | 2 | |
| `mapdisplaysize` | 20 | |
| `animspeed`, `trailscale` | | 1 |
| `cull` | none | none |
| `rotate-x/y/z` | 0 | |
| visibilities, `scaleonmapwithzoom`, `canfade` | true | true |

### occlusion (ours)

Our own attribute, on a marker, a trail or a category, inherited like any other. Other readers ignore it.
It lets a pack choose the player's **Marker occlusion** for its own pathables, and it overrides the
player's choice. Each value is one of that option's choices:

| Value | Marker occlusion | Drawn with GoodRender |
|---|---|---|
| `none` | None | over the world, under the game's HUD and windows |
| `characters` | Behind characters | through walls and hills, never through a character in front of it |
| `scene` | Behind characters and terrain | behind whatever of the world is in front of it |
| absent, empty or other | (the player's) | as the player's Marker occlusion says |

Matched in any case and untrimmed, as Pathing reads an enum. The editor offers the three values in a
dropdown. The options window describes the attribute under Marker occlusion, so authors find it there.

```xml
<MarkerCategory name="walls" occlusion="scene"/>
```

Without GoodRender it changes nothing. It has nothing to do with `occlude`, which makes markers hide other
markers.

v2.2.0 shipped it as `render-layer`, with `windows` and `terrain`. No corpus pack used it, so the old
names are not read.

### GUID

Base64 of 16 raw bytes. Optional, on markers and trails: without one Pathing assigns a fresh random Guid each
load. It keys `BehaviorStates`, so without a stable one `behavior`'s hide-until states reset every session.

## TRL

Per `TmfLib/Reader/TrlFileReader.cs`:

```
int32   version    (0; TmfLib bails otherwise)
int32   map_id
float32 x, y, z    to EOF, as xpos/ypos/zpos
```

- **`(0,0,0)` ends a segment**; one file can hold several polylines. Empty or truncated yields no sections.
- **Terminator conventions vary, even within one pack**: most end every segment including the last;
  `gw2-markers`' `Data/febe/*.trl` use none; some have runs of terminators or leading ones. So `pack/trl`
  records them per segment, and all 8,288 corpus files round-trip byte for byte.

## Writing

TmfLib's `PackWriter` is a re-packer: it merges categories into one file, regroups pathables by map, drops
comments and defaults, lowercases paths. Ours keeps the file split, comments, attribute order and casing.
