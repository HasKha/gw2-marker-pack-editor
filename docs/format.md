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

**Zipped is the shipping format.** Blish HUD does not reliably load images from an
unzipped folder, so a folder is something to author in, never what a pack ships as.

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
- **`route` is deprecated and unimplemented.** TmfLib's `UnpackRoutePoi` is a stub whose
  body is `// TODO: Log deprecated route type` and `return null`, so Blish silently drops
  every route. Pathing's `StandardRoute` is an empty class. No `<route>` exists in any of
  the 9 packs tested. We parse it and carry it through untouched; nothing more.

### A trail's map id comes from its .trl, not its XML

`PathablePrototypeBuilder.UnpackTrailPoi` reads the `.trl` header and then does
`AddOrUpdateAttribute("mapid", ...)` — **overwriting** whatever the XML said. So for a
trail the `.trl` header is authoritative and the XML `mapid` attribute is ignored.

Two consequences for drawing and for the linter: a `mapid` on a `<trail>` is decorative, and
a disagreement between the two is not an error, just a dead attribute.

**We read the header too**, or a trail whose two disagree would draw on a different map here
than it does in Blish. It cannot be resolved with the rest of a trail's attributes, though:
`BakedTrail::map_id` starts unknown and is filled in once the file is read — by the registry's
worker for an installed pack, and for the pack open in the editor through the editor's
per-frame load budget, the trail on no map until its file is there. See `viewer/edited.cpp`.

The attribute is still worth writing, and the editor writes it on a trail it creates: it is
how a reader tells which map a `<Trail>` is for without opening its `.trl`, which is why every
pack in the corpus has one. `trail_edit::setMapId` writes both, so the editor cannot leave the
two disagreeing.

Also from that function: a trail whose `traildata` file is **missing from the archive, or
fails to parse, is dropped entirely** — `UnpackTrailPoi` returns null. Same for a `<trail>`
with no `traildata` attribute at all.

Attribute names are **lowercased** on read (`NanoXmlBase`), so lookups are
case-insensitive. Vendor prefixes (`bh-`, configured via
`PackReaderSettings.VenderPrefixes`) are stripped and the remainder treated as a normal
attribute.

A POI's category is the dot-joined path of ancestor `name` attributes, matched
case-insensitively (`PathingCategory` uses `StringComparer.OrdinalIgnoreCase`).

TmfLib requires `mapid` and all three of `xpos`/`ypos`/`zpos` on a POI and bails on one
that is missing them. **Packs contain such POIs anyway**: `hero.blish.pack` has 679
position-less POIs carrying `ingamevisibility="0"` and an `empty.png` icon, used as
invisible anchors for toggles and achievements. Blish drops every one of them. Our reader
keeps them so they round-trip; the bake must skip anything without a position.

### Explicit vs aggregated attributes

TmfLib treats this as a first-class distinction, and so should we:

- `ExplicitAttributes` — set on this node itself.
- `GetAggregatedAttributeValue(name)` — resolved through the category chain, with the
  node's own value winning.

The property panel needs both: show the effective value, but make clear whether it is
explicit here or inherited, and let the user override or clear it.

### A category carries almost every attribute, and that is the point

Easy to get backwards, and we did: **a `markercategory` is where an author sets `iconSize`,
`iconFile`, `fadeNear` or `behavior` for a whole subtree.** It is not restricted to its own
five structural attributes.

Three facts in the source make it so:

1. `PathableCategoryBuilder.FromNanoXmlNode` strips exactly four attributes off the node —
   `name`, `isseparator`, `defaulttoggle`, `ishidden` — and hands **everything else** to
   `SetAttributes`. (`displayname` is in the strip list but commented out, so it stays in the
   collection as well as being read.)
2. `PathingCategory` implements `IAggregatesAttributes` with `AttributeParent => Parent`, so
   the chain is the same hop repeated: pathable → category → parent category → … → root. A
   nested category inherits from its parents exactly as a marker does.
3. Both `StandardMarker` and `StandardTrail` call `Populate(pointOfInterest
   .GetAggregatedAttributes(), …)`. Every attribute either of them reads, it reads through
   the chain.

So "which attributes may sit on a `markercategory`" has nothing to do with what a category
*does* with them. Two different questions, and the attribute table answers them separately:
`applies_to` for what consumes an attribute, `settableOn` for where it may be written —
`applies_to`, plus categories whenever the attribute inherits.

Only three groups are genuinely not category-settable: the position and identity of a single
pathable (`xpos`/`ypos`/`zpos`, `guid`, `type`), which inherit in Pathing but only
pathologically; the category's own structural attributes, which mean nothing on a pathable;
and `trailData`, for the reason below.

#### A category may hold as many trails as it likes

Nothing pairs a category with one trail. `PackReader.TryLoadPois` walks every child of every
`<POIs>` block and builds each into its own pathable; `type` only files it, and
`Trail.Preprocess` loads that trail's own `traildata`.

#### trailData is the exception

`Trail.Preprocess` resolves `traildata` through the chain like everything else — but
`PathablePrototypeBuilder.UnpackTrailPoi` tests `trailAttributes.Contains(TRAILDATA)` against
the **explicit** collection and returns null before `Preprocess` ever runs. A `trailData`
supplied only by a category therefore drops the trail entirely. Treat it as explicit-only.

### Which attributes each element actually uses

Traced to the Pathing file that reads each one. The `Standard{Marker,Trail}[attr].cs`
filenames name their own attributes; the behaviors under `Behavior/` are per-entity, and
`StandardMarker_Behaviors` registers far more of them than `StandardTrail_Behaviors` does.

| | Attributes |
|---|---|
| **Category only** | `name`, `DisplayName`, `IsSeparator`, `IsHidden`, `DefaultToggle` |
| **Marker only** | `xpos`/`ypos`/`zpos`, `iconFile`, `iconSize`, `heightOffset`, `minSize`, `maxSize`, `mapDisplaySize`, `scaleOnMapWithZoom`, `occlude`, `rotate`(+`-x`/`-y`/`-z`), `title`, `title-color`, `text`, `tip-name`, `triggerRange`, `autoTrigger`, `resetLength`, `behavior`, `invertBehavior`, `resetGuid`, `toggle`, `toggleCategory`, `show`, `hide`, `info`, `infoRange`, `bounce`(+`-height`/`-delay`/`-duration`) |
| **Trail only** | `trailData`, `texture`, `animSpeed`, `trailScale`, `isWall` |
| **Both pathables** | `type`, `MapID`, `GUID`, `alpha`, `color`/`tint`, `cull`, `canFade`, `fadeNear`, `fadeFar`, `inGameVisibility`, `mapVisibility`, `miniMapVisibility`, and every filter: `festival`, `mapType`, `mount`, `profession`, `race`, `specialization`, `raid`, `schedule`(+`-duration`), `script-*` |
| **Category *and* marker** | `achievementId`, `achievementBit`, `tip-description`, `copy`, `copy-message` |

That last row is the surprising one, and it is real: `Utility/CategoryAttributeUtil.cs` and
`UI/Controls/CategoryContextMenuStripItem.cs` read those five off the **category itself** —
the achievement pair greys out and hides a category's menu entry once the achievement is
done, `tip-description` is the entry's tooltip when there is no achievement to show instead,
and clicking a separator category copies its `copy` value with `copy-message` as the
confirmation.

#### Two attributes that look like they work on trails and do not

`StandardTrail_TacOMisc.cs` and `StandardTrail_OnFocus.cs` define `Populate_TacOMisc`
(`resetlength`) and `Populate_Triggers` (`triggerrange`) — and `StandardTrail.Populate` calls
neither. The code is written and dead, so on a trail both attributes do nothing. Measured
against the corpus, no pack sets either on a trail; what packs *do* set on trails is
`mapDisplaySize` (169 times across three packs), also marker-only, also dead.

#### fadecenter

`StandardMarker[canfade].cs` accepts `fadecenter` as a legacy alias for `canfade` ("Old
attribute name from original Markers & Paths module"). It is not in our attribute table, so a
pack using it shows as an unknown attribute. No pack in the corpus does, which is why it has
not been worth a table row plus a "deprecated" flag to keep it out of the picker.

### Real packs do parse with a conformant parser — measured

TmfLib ships a vendored `NanoXml` rather than using a standard parser, which suggested
packs in the wild might contain markup a conformant parser rejects. **Measured, and it
does not hold.** `mpe_probe` over every pack in the Blish HUD markers folder:

| | |
|---|---|
| Packs | 9 (`haskha_markers`, `hero.blish.pack`, `LadyElyssa`, `LadyElyssaAP`, `tw_ALL_IN_ONE`, `TehsTrails`, `moar`, `apo_markers`, `PEAK`) |
| XML files | 937 |
| pugixml parse failures | **0** |
| Elements | 146,146 |
| Elements resolvable to a source byte offset | **146,146** |

So NanoXml is presumably historical or a performance choice, not a tolerance
requirement. pugixml is fine. Re-run the probe if a pack ever fails to load.

### What pugixml does *not* preserve

It keeps the document tree faithfully — comments, attribute order, attribute casing,
values, and the whitespace *between* elements all survive a parse/serialize cycle,
provided you parse with `parse_full | parse_ws_pcdata` and serialize with `format_raw`.

Three things are lost, because pugixml does not model them:

1. **Whitespace inside a tag.** ` />` comes back as `/>`, and an attribute list wrapped
   across several lines is collapsed onto one. Hand-authored packs use both heavily.
2. **The UTF-8 BOM**, stripped on parse. `hero.blish.pack` and other TmfLib-written packs
   have one.
3. **CRLF line endings**, normalized to LF on parse as the XML spec requires.

2 and 3 are recoverable by recording the shape of the source and reapplying it on save,
and the writer does exactly that. 1 is not recoverable from the tree at all.

Blank lines between elements are lost for the same reason: they are whitespace-only text
nodes, and keeping them would fight with the indentation pugixml regenerates on the way
out.

So a pack **reformats once** on its first save. Measured on `gw2-markers`' `w1.xml`
(86 lines): 13 blank lines removed, one wrapped attribute list joined onto a single line,
and `/>` normalized to ` />`. Everything else — comments, attribute order, original
spelling, values verbatim — comes back unchanged, and the file is stable on every save
after that. See [architecture.md](architecture.md) for why that trade was taken.

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
  which case it is a fixed-orientation quad with an explicit rotation. `rotate="0,0,0"`
  is **not** a no-op — the mere presence of the attribute disables billboarding
  (`Populate_Rotation` sets `RotationXyz` whenever any of the four is present).
- Rotation values are **degrees** in the file, converted to radians, applied X then Y
  then Z, over a quad lying in Blish's XY plane — flat on the ground, facing up.
- **UVs come from the sign of each vertex**, not from a conventional corner mapping:

  ```csharp
  new Vector2(vert.X < 0 ? 1 : 0, vert.Y < 0 ? 1 : 0)
  ```

  So the `-X,-Y` corner carries uv `(1,1)` and the `+X,+Y` corner `(0,0)`. Both axes are
  inverted from the obvious `(0,0)->(1,1)` mapping. Getting this wrong draws a rotated
  marker in the right place, on the right plane, with the right normal — and the image
  turned 180 degrees.
- **A billboard cancels half of that, and only half.** `Matrix.CreateBillboard` builds
  `right = up x direction` in Blish's Z-up **right-handed** space, where that points to
  screen *left*. Build the same expression in Mumble's Y-up **left-handed** space and it
  points to screen right, because swapping two axes flips the sign of a cross product.
  So the horizontal inversion cancels and the vertical one is absorbed by the corner
  order: a billboard ends up with the plain top-left-to-bottom-right mapping, and a
  rotated marker does not. Neither is a free choice — they are the same rule read in two
  different spaces.
- **A billboard's projected shape is a tilted quad, not an axis-aligned square.** It is
  perpendicular to the ray to the camera, not to the image plane, so away from the centre
  of the screen it rotates and shears. Blish rasterizes the four projected corners, which
  is visible as soon as a marker is off to one side.
- **The billboard's on-screen size is measured, not computed from the distance.** Blish
  projects the four corners and takes the extent of the result. That is not the same
  number as `iconSize * 2 / distance`, and the difference is large:

  - A billboard is perpendicular to the *ray* to the camera, so its projected height goes
    as one over the view-space **depth**, not the radial distance. Off-axis those differ
    by `r/z`.
  - Off-axis it also lands on the image plane magnified by `1/cos²`, the ordinary tangent
    stretch of a perspective projection. For a marker offset vertically the two effects
    compound to `r/z²` — bigger than the on-axis size, where dividing by radial distance
    makes it smaller.

  At a 70-degree vertical FOV, sizing by radial distance is 1.6x too small at the middle
  of the screen edge and 1.7x at the corner. Anything reproducing Blish's picture has to
  project the quad.
- After projection, on-screen height is clamped between `minsize * 4` and `maxsize * 4`,
  and the model matrix is rescaled to satisfy it. **Those bounds are not in pixels.** The
  quantity being clamped is

  ```csharp
  pixelSizeY = bounds.HalfExtents.Y * 2 * Viewport.Height
  ```

  where `bounds` is in normalized device coordinates. NDC spans **2** across the viewport,
  so multiplying an NDC extent by the viewport height without halving it gives twice the
  pixel height. In real pixels the limits are therefore `minsize * 2` and `maxsize * 2` —
  with the defaults, a marker stops shrinking at **10 px**, not 20. Anything reproducing
  Blish's picture has to reproduce this, or distant markers pin to a fixed size twice as
  far out and twice as large.
- Opacity is `alpha * fade`, where fade ramps linearly between `fadeNear` and `fadeFar`.
- `occlude="1"` switches the blend state to opaque.
- `cull` maps to a rasterizer cull direction.

Trails: `Entity/StandardTrail.World.cs`, `ref/hlsl/trail.hlsl`. `animspeed` scrolls the
texture along the ribbon; `iswall="1"` orients the ribbon vertically; `trailscale`
scales ribbon width and texture repeat.

The exact geometry, from `BuildTrailSection`:

```
TRAIL_WIDTH  = 20 * 0.0254f          // 0.508 m, the half-width offset
normalOffset = TRAIL_WIDTH * trailScale
offset       = cross(pathDirection, isWall ? cross(pathDirection, down) : down)
vertices     = curPoint ± normalize(offset) * normalOffset
UV.v         = distanceAlongPath / (TRAIL_WIDTH * 2)
```

So a trail is 1.016 m wide at `trailScale="1"`, the ribbon is centred on the path, and
the texture repeats once per 1.016 m of length. `down` is Blish's `Vector3.Forward` in its
Z-up space; in the file format's Y-up space that is `(0,-1,0)`.

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
Fetch these when filling in the attribute table's enum value lists — they give value
ranges and semantics the source alone does not.

`GUID` is base64 of 16 raw bytes, not canonical UUID text.

### GUID is optional, and applies to trails too

`StandardMarker`/`StandardTrail`'s `Populate_Guid` both accept a `guid` attribute and, when
it is absent or empty, assign `Guid.NewGuid()` — **a fresh random value on every load**.

The GUID is the key for `BehaviorStates`: `_hiddenUntilMapChange`, `_hiddenUntilTimer`,
`_hiddenInShard`. So it is what makes the `behavior` attribute's "hide until map
change / daily reset / once per instance" persistence stick, and what lets Blish remember
an individually hidden object. Without a stable GUID those states silently reset each
session. A pathable with no `behavior` and no need for per-object hiding loses nothing.

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

### Terminator conventions vary, including within one pack — measured

There is no single convention, so a writer that normalizes will dirty files it did not
edit. Observed across 8,288 `.trl` files:

- Most files terminate **every** segment, including the last, so the stream ends with a
  `(0,0,0)`.
- `gw2-markers`' own `Data/febe/*.trl`, written by its `scripts/trltools.py`, use **no
  terminators at all** — while `Data/*.trl` in the same pack do. Both conventions ship
  together.
- Real packs contain **runs** of consecutive terminators, and occasionally leading ones.
  One file in `tw_ALL_IN_ONE` carries 22 degenerate terminators.

So `pack/trl` stores a per-segment `terminators_after` count plus a `leading_terminators`
count, rather than inferring separators on write. With that, all 8,288 files round-trip
byte-for-byte.

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
