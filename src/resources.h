#pragma once

#include <Nexus.h>
#include <imgui.h>

#include <memory>
#include <string>

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

// nullptr until the image is available, or if it does not exist / fails to decode.
// Paths are resolved through pack::normalizePath, so any spelling a pack uses works.
ImTextureID texture(const std::string& path);

// nullptr if the file is missing or unparseable. The returned Trail is owned by the
// cache and stays valid until setSource or shutdown.
const pack::trl::Trail* trail(const std::string& path);

} // namespace resources
