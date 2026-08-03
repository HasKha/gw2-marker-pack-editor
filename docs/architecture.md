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
| UI shape | several small windows, not one big one | see [The UI is windows](#the-ui-is-windows) |
| Group transform | anchored on the first node selected; a rotation orbits the rest | see [The gizmo anchor](#the-gizmo-anchor) |
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
  marker_editor.*      lifecycle, the windows and the shared menu bar, save/open, WndProc
  pathable_edit.*      creating markers: what a new one is and where it belongs
  trail_edit.*         the open trail's working copy, shared by its window and the gizmo
  editor_state.*       State: the open Pack, selection, per-node visibility, settings
  file_dialog.*        NFD on a worker thread (never block the render thread)
  resources.*          icons + .trl pulled from the pack source on demand, budgeted
  pack/                — mpe_pack, no Nexus/imgui dependency
    source.*           DataSource: a pack is a zip OR a folder. Path normalization.
    zip.*              DataSource over a .zip/.taco, via miniz
    attributes.*       THE attribute table: name, kind, default, inheritance
    model.*            Pack / XmlFile / Category / Pathable, explicit vs effective values
    xml_reader.*       pugixml -> model; the document is kept and owned by the Pack
    writer.*           serializes the document back out, rebuilds the zip (format.md)
    trl.*              .trl read/write, byte-exact
  ui/
    windows.*          the window registry: titles, visibility, View menu, options page
    tree_window.*      category tree, selection (ctrl / shift range), per-node eye toggles
    property_panel.*   attribute form + "add attribute" + inherited values
    resource_window.*  the pack's images, what references them, add / remove
    trail_editor.*     the open trail: map id, point list, the Trail menu, shape tools
  world/
    camera.*           MumbleLink -> view/projection, world->screen
    renderer.*         billboards + trail ribbons into an ImGui draw list, hit testing
    gizmo.*            ImGuizmo translate/rotate over the selection, writes back to attributes
test/
  pack_probe.cpp       the harness
```

## Things that are easy to get wrong

Three mechanisms that exist for non-obvious reasons.

**The world renderer caches resolved values per `State::revision`.** Resolving inherited
attributes walks the category chain, and the largest pack tested has 55,887 markers — doing
that per marker per frame is not viable inside the render callback. Any edit calls
`State::touch()`, which bumps the revision and invalidates the cache. Visibility toggles
deliberately do *not* bump it: they are view state, not edits.

**`resources` decodes at most a few assets per frame.** Arriving on a map with hundreds of
unseen markers would otherwise decode them all in one frame and hitch. A miss is only
cached when the file is genuinely absent — caching a "Nexus could not make a texture yet"
result is what once made every marker fall back to a dot until the pack was reloaded.

**Zip saving is two calls, `buildZip` then `commitZip`.** The usual save target *is* the
pack still open for reading, and Windows will not rename over a live handle, so the caller
has to release the source in between. Building first also means a failure never replaces a
working pack with a half-written one; the previous file is kept as `.bak`.

## The UI is windows

One window holding both the tree and the details forced the gizmo into whatever screen
that window was not covering. The editor is instead five independent windows, each
closable, each remembering its own place:

| Window | Contents |
|---|---|
| Tree | pack header, category hierarchy, selection, visibility toggles |
| Details | the attribute form for the selection |
| Toolbar | gizmo mode, Add POI, and the create/place controls as they arrive |
| Trail editor | the open trail's map id and points, with drag fields for the selected one |
| Resources | the pack's images: folder tree, references, preview, add / remove |

`ui/windows.*` is the registry: one table row per window with its title, its menu label
and its default visibility, and everything else — the View menu, the Nexus options
checkboxes, the keybind's hide-and-restore — is generated from that table. Adding a window
is one row plus its draw function.

Two consequences of every window being closable:

- **The menu bar is drawn in each large window**, from one shared `drawMenuBar`, so File
  and View are reachable from whichever one is open. The Toolbar has no menu bar; a bar
  would be taller than its contents.
- **A modal has an owner.** An ImGui popup is scoped to the window that opened it, so the
  discard-changes confirmation records which window's menu asked for it and is only begun
  there. If that window is closed while the modal is up, the pending action is dropped
  rather than left armed.

Actions that reload or close the pack are applied at the *end* of the frame. They free the
vectors every window has just drawn from, and a menu click happens partway through that.

The Nexus options page carries a checkbox per window, because a window closed with its X
would otherwise only come back through a keybind that is unbound by default.

## One trail, one working copy

`trail_edit` holds the trail being edited as a mutable copy of its `.trl`, and it is a
module of its own rather than part of the trail editor window because the window is not the
only thing editing it: the gizmo drags the same points, and the renderer draws handles for
them. All three go through the one model.

- **Points are addressed by a flat index** across segments — a `.trl` is one or more runs of
  points separated by `(0,0,0)` terminators, and the editor lists them as one numbered list
  with a mark where a new run begins.
- **Every change writes the whole trail back** into `Pack::added_assets`, the same pending
  mechanism the resources window uses, and calls `resources::forget` so the next frame
  redraws from the new bytes. There is no separate "trail is dirty" state and no save step
  of its own: a pack save picks the trail up with everything else. Reserializing a few
  thousand points per edited frame is nothing next to a frame of rendering.
- **A trail point is a `NodeRef`** like anything else — `NodeKind::TrailPoint`, with `sub`
  carrying the point index. That is what lets the gizmo's group transform, the anchor and
  ctrl/shift multi-select work on points without a second selection mechanism. The property
  panel says so and defers; visibility ignores them, since they are not tree rows.
- **Only the open trail gets handles** in the world. Every trail's points at once would be
  an unreadable field of dots, and a point that cannot be seen cannot be clicked.

`Trail > Reload from file` drops the pending bytes and rereads the source **directly**
rather than through `resources`, whose reads are budgeted a few per frame: a menu action
that silently does nothing because the budget was spent is worse than one extra read.
`Save to file` exports a copy outside the pack — for a zip pack there is no other coherent
meaning, since a single `.trl` cannot be written back into the archive on its own.

## A new marker is appended, not inserted

`Pack::pathables`, `State::pathables_by_category`, the selection and the renderer cache are
all positional, so inserting into the middle of the pathable vector would shift every index
after it and invalidate all four at once. `Pack::addPathable` therefore **appends** to the
vector, always, and no existing index ever moves.

Where the marker appears is decided in the two places that actually care:

- **In the file**, by `pugi::xml_node::insert_child_after` on the sibling it should follow,
  so the document reads in the order the editor shows. With no sibling to follow it goes at
  the end of a `<POIs>` block in the category's own file, created if that file has none.
- **In the tree**, by `State::pathableAdded` inserting the new index into
  `pathables_by_category` after its sibling. That list *is* the tree order; it only happens
  to match the vector order on a freshly loaded pack.

Deleting will not have this luxury, which is why the deletion strategy — tombstone or
erase — is still an open question in [plan.md](plan.md).

## Assets are pending until a save

The `DataSource` is read-only, and for a zip it is the very archive still open for reading,
so an added image cannot go anywhere at the moment it is added. `Pack::added_assets` and
`Pack::removed_assets` hold both kinds of change until a save resolves them: `buildZip`
writes the adds in and leaves the removals out, `saveToDirectory` writes and deletes files
on disk, and the caller clears the lists afterwards.

Two things fall out of that:

- **`resources` looks at the pending adds first.** Otherwise a freshly added icon would
  read as missing everywhere until the pack was saved and reopened. And because Nexus hands
  back the *existing* texture for an identifier it has already seen, changing the bytes
  behind a path has to go through `resources::forget`, which drops the cache entry and bumps
  the generation so the next load asks under a name Nexus has not seen.
- **An added file takes its folder's existing spelling.** The window works in normalized
  (lowercase) paths, so writing one straight back would put `data/icons/x.png` beside an
  existing `Data/icons/` — two directories in the archive differing only in case.

What references an image comes from the attribute table: every attribute whose kind is
`Path` is a reference, so `iconfile`, `texture`, `traildata` and anything added later are
all counted without being named in the UI. The counts and the folder tree are rebuilt per
`State::revision`, not per frame, for the same reason the world renderer caches.

The resources window previews **only the selected image**, deliberately. Nexus never
releases a texture, so drawing a thumbnail per row would pin every image in a 1,375-image
pack in memory for the session the first time the folder was opened.

## The gizmo anchor

The gizmo draws on the **first** node selected, not the last. `Selection` keeps click
order and only ever appends, so the anchor is stable as the selection grows — the handles
do not hop to whatever was clicked most recently, which is what makes a group drag
predictable. The property panel wants the opposite (`primary()`, the last click), so both
accessors exist.

Everything a drag does is expressed as one transform about the anchor:

```
p' = anchor' + R (p - anchor)      orientation' = R · orientation
```

A pure translation has `R = identity`, so it falls out as a shift. A pure rotation leaves
`anchor'` at `anchor`, so the anchor turns in place while the others **orbit** it — which
is why a group rotation writes position as well as `rotate` on every non-anchor node. The
anchor's own offset is zero, so the same loop covers it without a special case.

Two consequences worth knowing. `R` is recovered per frame from the anchor's stored
attributes, which are formatted to six significant digits, so a long group drag drifts a
little. And rotating a group stamps `rotate` onto markers that had none, turning them from
billboards into fixed-orientation quads — unavoidable, since orbiting is a rotation.

Only POIs take part. A trail's geometry lives in its `.trl`, so moving one means moving
its points; the gizmo skips them rather than pretending.

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

## Saving serializes, it does not diff

An earlier version of the writer spliced replacement start tags into the original bytes,
so an unedited file came back byte-identical. It was replaced with a plain serializer.

The splice preserved more — it kept intra-tag whitespace, which pugixml cannot represent —
but it could only ever *rewrite tags that already existed*. Adding a marker, deleting one,
or creating a pack from scratch all need node insertion, which a splice cannot express
without also inventing the surrounding indentation. Those are the features that matter
next, and they fall out of a serializer for free.

The trade was made deliberately: a pack reformats **once**, on its first save, and is
stable from then on.

So the parsed `pugi::xml_document` is now kept alive for the pack's lifetime — it *is* the
storage. The model indexes into it with `SourceNode` handles, saving pushes the model's
attributes back onto those nodes and serializes, and adding or removing a marker becomes a
node insert or erase. `Pack` is therefore move-only: copying it would leave two packs
sharing one document.

What still survives a save: comments, processing instructions, the declaration, attribute
order, original attribute spelling, verbatim attribute values, the BOM, CRLF, and the file
split. What does not: blank lines between elements, wrapped attribute lists, and `/>`
without a preceding space.

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
