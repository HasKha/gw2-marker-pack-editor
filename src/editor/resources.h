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

// Images and trail data read out of the open pack on demand, since a large pack ships thousands,
// on a worker of the editor's own: a frame asks, and a later frame has the answer.
namespace resources {

void init(AddonAPI* api);
// Drops everything and waits for the reads in flight.
void shutdown();

// Blocks until every read asked for so far has run. Never from a frame: for tests.
void waitForReads();

// Takes ownership of the pack's source and drops every cached asset. The source has to
// outlive the pack because assets are read from it long after loading; a read in flight keeps
// it open past this.
void setSource(std::unique_ptr<pack::DataSource> source);
const pack::DataSource* source();

// Lets the cache see the pack's pending adds before the DataSource. Must outlive the addon.
void setPendingAssets(const pack::Pack* pack);

// Drops one cached texture, for when the bytes behind a path change.
void forget(const std::string& path);

// Paths are resolved through pack::normalizePath, so any spelling a pack uses works.
viewer::Image image(const std::string& path);
ImTextureID texture(const std::string& path);

// nullptr if the file is missing or unparseable. Shared, because the viewer draws the open
// pack's trails from it and keeps one past setSource, forget or shutdown.
//
// `deferred` tells the two kinds of nullptr apart: true means it is being read, so a later
// frame will have it; false means it was read and the miss is cached, so no later frame will.
std::shared_ptr<const pack::trl::Trail> trail(const std::string& path, bool* deferred = nullptr);

// The colour the image at `path` gives a trail on the map: viewer::trail_colour::sample, or its
// NO_COLOUR for a file that is not there. Cached; nothing, with `*deferred` set, while it is
// being read.
std::optional<ImU32> trailColour(const std::string& path, bool* deferred = nullptr);

} // namespace resources
