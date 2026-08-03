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
    attributes.*       THE attribute table: name, kind, default, inheritance
    model.*            Pack / XmlFile / Category / Pathable, explicit vs effective values
    xml_reader.*       pugixml -> model, recording each node's source byte span
    xml_writer.*       splices edits into the original bytes (see format.md)
    trl.*              .trl read/write, byte-exact
    zip.*              miniz read of the pack archive, TmfLib path normalization
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
