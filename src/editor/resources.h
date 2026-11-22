#pragma once

#include <Nexus.h>
#include <imgui.h>

#include <memory>
#include <string>

#include "pack/model.h"
#include "pack/source.h"
#include "pack/trl.h"

// Images and trail data pulled out of the open pack on demand.
//
// Both are loaded lazily rather than up front: the largest pack tested ships 1,375 images
// and 4,328 .trl files, and decoding all of that on open would stall the game for the
// sake of assets the user may never look at.
namespace resources {

void init(AddonAPI* api);
void shutdown();

// Resets the per-frame decode budget. Call once at the top of the render callback.
//
// Decoding is synchronous, so arriving on a map with hundreds of unseen markers would
// otherwise decode all of them in one frame and visibly hitch. Instead a few are admitted
// per frame and the rest appear over the next handful of frames.
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

// A decoded image and the pixel size Nexus reports for it. `texture` is nullptr until the
// image is available, or if it does not exist / fails to decode.
struct Image {
    ImTextureID texture = nullptr;
    int width = 0;
    int height = 0;
};

// Paths are resolved through pack::normalizePath, so any spelling a pack uses works.
Image image(const std::string& path);
ImTextureID texture(const std::string& path);

// nullptr if the file is missing or unparseable. The returned Trail is owned by the
// cache and stays valid until setSource or shutdown.
//
// `deferred` tells the two kinds of nullptr apart: true means the frame's budget was spent
// before the read was attempted, so next frame will do better; false means it was tried and
// the miss is cached, so no later frame will.
const pack::trl::Trail* trail(const std::string& path, bool* deferred = nullptr);

} // namespace resources
