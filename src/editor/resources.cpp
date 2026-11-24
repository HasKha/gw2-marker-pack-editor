#include "editor/resources.h"

#include <unordered_map>
#include <vector>

#include "viewer/trail_colour.h"

namespace resources {

namespace {

std::unique_ptr<pack::DataSource> data_source;

// Assets added in the editor live here until a save puts them in the source.
const pack::Pack* pending_pack = nullptr;

// One budget for images and trails alike: a frame's hitch is the sum of everything read in
// it, whatever kind. Small enough that a burst never costs a visible hitch, large enough
// that a map's markers fill in within a fraction of a second.
viewer::LoadBudget budget{4};

viewer::TextureCache textures{"Markus_"};

// Misses are cached too, so a missing trail is not retried every frame.
std::unordered_map<std::string, std::shared_ptr<const pack::trl::Trail>> trails;

// What each image gives a trail on the map, missing ones included.
std::unordered_map<std::string, ImU32> trail_colours;

// The bytes behind a normalized path. Added in the editor and not saved yet, those bytes are
// the truth for this path: the trail editor writes its working copy back as a pending asset,
// and an added image is not in the source until a save. False when neither has the file.
bool readAsset(const std::string& key, std::vector<uint8_t>* bytes) {
    if (const pack::Pack::AddedAsset* added = pending_pack ? pending_pack->findAddedAsset(key) : nullptr) {
        *bytes = added->bytes;
        return true;
    }
    std::string error;
    return data_source && data_source->read(key, bytes, &error);
}

} // namespace

void beginFrame() {
    budget.reset();
}

void init(AddonAPI* api) {
    textures.setApi(api);
}

void shutdown() {
    data_source.reset();
    pending_pack = nullptr;
    textures.clear();
    textures.setApi(nullptr);
    trails.clear();
    trail_colours.clear();
}

void setSource(std::unique_ptr<pack::DataSource> source) {
    data_source = std::move(source);
    textures.clear();
    trails.clear();
    trail_colours.clear();
}

const pack::DataSource* source() {
    return data_source.get();
}

void setPendingAssets(const pack::Pack* pack) {
    pending_pack = pack;
}

void forget(const std::string& path) {
    const std::string key = pack::normalizePath(path);
    trails.erase(key);
    trail_colours.erase(key);
    textures.forget(key);
}

viewer::Image image(const std::string& path) {
    // No source at all is normal for a pack created in the editor: everything it has is a
    // pending asset until the first save.
    if (path.empty()) return {};
    return textures.get(pack::normalizePath(path), budget, readAsset);
}

ImTextureID texture(const std::string& path) {
    return image(path).texture;
}

std::shared_ptr<const pack::trl::Trail> trail(const std::string& path, bool* deferred) {
    if (deferred) *deferred = false;
    if (path.empty()) return nullptr;

    const std::string key = pack::normalizePath(path);
    auto it = trails.find(key);
    if (it != trails.end()) return it->second;

    if (!budget.take()) {
        if (deferred) *deferred = true;
        return nullptr;
    }

    std::shared_ptr<const pack::trl::Trail> parsed;
    std::vector<uint8_t> bytes;
    if (readAsset(key, &bytes)) {
        auto candidate = std::make_shared<pack::trl::Trail>();
        std::string error;
        if (pack::trl::read(bytes, candidate.get(), &error)) parsed = std::move(candidate);
    }

    trails.emplace(key, parsed);
    return parsed;
}

std::optional<ImU32> trailColour(const std::string& path, bool* deferred) {
    if (deferred) *deferred = false;
    const std::string key = pack::normalizePath(path);
    if (const auto it = trail_colours.find(key); it != trail_colours.end()) return it->second;

    if (!budget.take()) {
        if (deferred) *deferred = true;
        return std::nullopt;
    }
    std::vector<uint8_t> bytes;
    const ImU32 colour = readAsset(key, &bytes) ? viewer::trail_colour::sample(bytes) : viewer::trail_colour::NO_COLOUR;
    trail_colours.emplace(key, colour);
    return colour;
}

} // namespace resources
