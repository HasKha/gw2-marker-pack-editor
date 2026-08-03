# Architecture

How the addon is built and why. For the pack format itself see [format.md](format.md);
for what is done and what is next see [plan.md](plan.md).

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Formatting | VfxDenoiser's `.clang-format` verbatim | 4 spaces, no tabs, 140 col; enforceable |
| Naming | VfxDenoiser style | snake_case files/namespaces, camelCase functions, PascalCase types |
| Dependencies | FetchContent only, one `cmake/*.cmake` per dep | nothing vendored into `src/` |
| entry.cpp | oops-wrong-template's minimal form | no globals beyond `AddonDef`/`hSelf`, no feature logic |
| World rendering | ImGui draw list + hand-built matrices | see [Rendering](#rendering) |
| Tree top level | categories only; XML files never appear as nodes | see [The tree is categories](#the-tree-is-categories) |
| Packs open | exactly one at a time | keeps the model a single source of truth |
| Shipping format | zip in, zip out | Blish HUD fails to load images from unzipped folders |

## Dependencies

Carried over from the templates: `imgui` (RaidcoreGG fork), `mumble`, `nexus`,
`gw2relib`, `nlohmann_json`.

New for this project:

| Dep | Purpose |
|---|---|
| `pugixml` | XML read/write; keeps comments and attribute order as real nodes |
| `miniz` | zip read/write |
| `nativefiledialog-extended` | native open/save dialogs |
| `ImGuizmo` | 3D translate/rotate/scale gizmo |
| `glm` | vec3/mat4 for the projection and the matrices ImGuizmo consumes |

## Build targets

Three targets, because the pack layer is worth keeping testable outside the game:

| Target | Kind | Contents |
|---|---|---|
| `mpe_pack` | static lib | `src/pack/**` — the format. Links only pugixml, miniz, glm. **Knows nothing about Nexus, imgui or the game.** |
| `MarkerPackEditor` | shared lib | everything else in `src/` — the addon. Links `mpe_pack` plus imgui, ImGuizmo, Nexus, Mumble, gw2relib, NFD |
| `mpe_probe` | console exe | `test/**` — harness that loads real packs and reports what it found. Links `mpe_pack` only |

`mpe_probe` is how format work gets verified: it runs against every pack in the Blish HUD
markers folder without launching GW2. Run it after any change to `src/pack/`.

```
mpe_probe "<...>/markers/haskha_markers.zip" "<...>/markers/hero.blish.pack.zip"
```

## Module layout

```
src/
  entry.cpp            Nexus entry, AddonDefinition, hands DataLinks to marker_editor
  marker_editor.*      lifecycle, window registration, settings, wires everything together
  file_dialog.*        NFD on a worker thread (never block the render thread)
  textures.*           pack images -> Nexus textures -> ImTextureID  (addon layer: needs Nexus)
  pack/                — mpe_pack, no Nexus/imgui dependency
    source.*           DataSource: a pack is a zip OR a folder. Path normalization.
    zip.*              DataSource over a .zip/.taco, via miniz
    attributes.*       THE attribute table: name, kind, default, inheritance
    model.*            Pack / XmlFile / Category / Pathable, explicit vs effective values
    xml_reader.*       pugixml -> model, recording each node's source byte span
    xml_writer.*       splices edits into the original bytes (see format.md)
    trl.*              .trl read/write, byte-exact
  ui/
    tree_window.*      hierarchical tree, selection
    property_panel.*   attribute form + "add attribute"
  world/
    camera.*           MumbleLink -> view/projection, world->screen, screen ray
    renderer.*         billboards + trail ribbons into an ImGui draw list
    gizmo.*            ImGuizmo integration, picking
test/
  pack_probe.cpp       the harness
```

## The attribute table is the core abstraction

Attributes get defined **once**, as data: name, canonical spelling, value kind
(float / int / bool / string / color / enum / path), default, whether it inherits from
the category. Parsing, serialization, the property form, and the "add attribute"
picker are all driven off that one table. Adding a new attribute must be a one-line
change. This is the single most important structural rule in the project.

Unknown attributes are kept verbatim as string key/values so an unrecognised extension
survives a round-trip and shows up in the property panel as a raw text field.

The table also has to carry the explicit-vs-inherited distinction, because the property
panel needs to show the effective value while making clear whether it is set here or
came from an ancestor category — see [format.md](format.md).

## A pack is a zip or a folder

Everything above `pack/source.h` reads through the abstract `DataSource`, with two
implementations: `zip::Archive` and a directory reader. TmfLib draws the same line
(`IDataReader` / `ZipArchiveReader` / `DirectoryReader`).

The folder case exists so a pack's **source tree** can be edited in place — a git checkout
like `gw2-markers` rather than the built zip. That matters because the zip is a build
artifact: editing it and then rebuilding from source would throw the edits away.

Both paths behave identically. The same `normalizePath` applies to directory entries, so a
folder and the zip built from it resolve resources the same way — verified by running
`mpe_probe` over both and getting identical models.

Two things stay different, and both are recorded on `Pack`:

- `from_directory` decides how a save is written: files back in place, or a rebuilt archive.
- A folder is an **editing convenience, not a shippable pack**. Blish HUD does not reliably
  load unzipped packs, so the UI says so on the button tooltip.

The directory reader skips dot-directories. Without that, opening a pack repo would walk
`.git`.

## The tree is categories

The XML files a pack is split across are an authoring convenience. Every loader merges
them into one category tree and one pathable list, so the split has no meaning to the
game — which is exactly how pack authors treat it.

That was the open question, and the data settled it. Across all 9 test packs:

| | |
|---|---|
| Categories | 31,817 |
| Declared in more than one file | **1** |

The single exception is `gw2-markers`' `haskha` root, which each of its 10 files reopens
as a wrapper. No leaf category anywhere is split. So a file-oriented tree would add a
level of nesting that carries no information and, for hand-authored packs, actively
obscures the structure.

The tree therefore shows categories, with each pathable under its own category. Files are
still tracked and still matter — just as metadata rather than hierarchy:

- Every pathable records the file it came from; the property panel shows it, and the
  writer needs it.
- A category records **all** of its declarations, since one may legitimately span files.
- New markers will need a target file; the sensible default is the file its siblings are
  already in.

Pathables whose `type` resolves to no category appear under an "Unresolved" node rather
than being dropped, so a broken `type` is visible instead of silent.

## Rendering

Nexus hands us `AddonAPI::SwapChain` and, via `Textures.GetOrCreateFromMemory`, an
`ID3D11ShaderResourceView*` per image — directly usable as an `ImTextureID`. So no
custom DX11 pipeline is needed:

1. Each frame, read `Mumble::Data::CameraPosition` / `CameraFront` and the FOV out of
   the identity JSON.
2. Build view and projection with glm. Positions are metres; `fadeNear`/`fadeFar` are
   inches, so divide distances by 0.0254 before comparing. Verify against a known marker
   position early.
3. Cull by `MapID` against `Context.MapID`, then by `fadeNear`/`fadeFar`.
4. Project each marker centre to screen, size the quad from `iconSize * 2`, clamp its
   on-screen height between `minSize * 4` and `maxSize * 4` pixels the way
   `StandardMarker.World.cs` does, and push a textured quad into the draw list, sorted
   back-to-front. Billboard unless `rotate*` is set.
5. Trails: project each `.trl` point, emit a triangle strip of quads along the polyline,
   breaking into separate ribbons at each `(0,0,0)` segment terminator.

Known limitation: no depth buffer, so markers are not occluded by world geometry. That
matches how the pack looks in Blish HUD with `occlude` off and is acceptable for an
editor. Keep the renderer behind a narrow interface so a DX11 backend can be added
later if occlusion becomes necessary.

ImGuizmo takes the same view/projection matrices plus a model matrix per selected
object, so it drops straight in once step 2 is correct.

## Threading

Everything runs on the Nexus render callback (render thread). Two exceptions:

- The native file dialog runs on a worker thread; `render()` polls `takeResult()`.
- Pack loading (unzip + parse) should move to a worker once packs get big enough to
  stutter. Not yet needed.
