#pragma once

#include <Nexus.h>
#include <imgui.h>

#include <memory>
#include <optional>
#include <string>

#include "pack/model.h"
#include "pack/source.h"
#include "pack/trl.h"
#include "viewer/textures.h"

// Images and trail data pulled out of the open pack on demand.
//
// Both are loaded lazily rather than up front: the largest pack tested ships 1,375 images
// and 4,328 .trl files, and decoding all of that on open would stall the game for the
// sake of assets the user may never look at.
namespace resources {

void init(AddonAPI* api);
void shutdown();

// Resets the per-frame load budget, which images and trails share — see viewer::LoadBudget.
// Call once at the top of the render callback.
void beginFrame();

// Takes ownership of the pack's source and drops every cached asset. The source has to
// outlive the pack because assets are read from it long after loading.
void setSource(std::unique_ptr<pack::DataSource> source);
const pack::DataSource* source();

// An image added in the editor is not in the DataSource until the pack is saved, so the
// cache has to look at the pack's pending adds first — otherwise a new icon would show as
// missing until a save and reopen. The pointer must outlive the addon; it is the one
// long-lived State.
void setPendingAssets(const pack::Pack* pack);

// Drops one cached texture. Needed when the bytes behind a path change — a pending add
// replaced or withdrawn — because a cache hit would otherwise keep the old image forever.
void forget(const std::string& path);

// Paths are resolved through pack::normalizePath, so any spelling a pack uses works.
viewer::Image image(const std::string& path);
ImTextureID texture(const std::string& path);

// nullptr if the file is missing or unparseable. Shared, because the viewer draws the open
// pack's trails from it and keeps one past setSource, forget or shutdown.
//
// `deferred` tells the two kinds of nullptr apart: true means the frame's budget was spent
// before the read was attempted, so next frame will do better; false means it was tried and
// the miss is cached, so no later frame will.
std::shared_ptr<const pack::trl::Trail> trail(const std::string& path, bool* deferred = nullptr);

// The colour the image at `path` gives a trail on the map: viewer::trail_colour::sample, or its
// NO_COLOUR for a file that is not there. Cached, and spending the same budget; nothing, with
// `*deferred` set, when it is spent.
std::optional<ImU32> trailColour(const std::string& path, bool* deferred = nullptr);

} // namespace resources
