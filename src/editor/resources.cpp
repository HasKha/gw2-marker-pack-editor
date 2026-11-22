#include "editor/resources.h"

#include <unordered_map>
#include <vector>

namespace resources {

namespace {

std::unique_ptr<pack::DataSource> data_source;

// Assets added in the editor live here until a save puts them in the source.
const pack::Pack* pending_pack = nullptr;

// One budget for images and trails alike: a frame's hitch is the sum of everything read in
// it, whatever kind. Small enough that a burst never costs a visible hitch, large enough
// that a map's markers fill in within a fraction of a second.
viewer::LoadBudget budget{4};

viewer::TextureCache textures{"MPE_"};

// Misses are cached too, so a missing trail is not retried every frame.
std::unordered_map<std::string, std::unique_ptr<pack::trl::Trail>> trails;

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
}

void setSource(std::unique_ptr<pack::DataSource> source) {
    data_source = std::move(source);
    textures.clear();
    trails.clear();
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

const pack::trl::Trail* trail(const std::string& path, bool* deferred) {
    if (deferred) *deferred = false;
    if (path.empty()) return nullptr;

    const std::string key = pack::normalizePath(path);
    auto it = trails.find(key);
    if (it != trails.end()) return it->second.get();

    if (!budget.take()) {
        if (deferred) *deferred = true;
        return nullptr;
    }

    std::unique_ptr<pack::trl::Trail> parsed;
    std::vector<uint8_t> bytes;
    if (readAsset(key, &bytes)) {
        auto candidate = std::make_unique<pack::trl::Trail>();
        std::string error;
        if (pack::trl::read(bytes, candidate.get(), &error)) parsed = std::move(candidate);
    }

    const pack::trl::Trail* result = parsed.get();
    trails.emplace(key, std::move(parsed));
    return result;
}

} // namespace resources
