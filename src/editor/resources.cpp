#include "editor/resources.h"

#include <unordered_map>
#include <vector>

namespace resources {

namespace {

AddonAPI* api = nullptr;
std::unique_ptr<pack::DataSource> data_source;

// Assets added in the editor live here until a save puts them in the source.
const pack::Pack* pending_pack = nullptr;

// Nexus owns its texture cache and exposes no way to release one, so identifiers must not
// collide between packs. Bumping a generation on every load keeps them distinct.
//
// The cost is that textures from previously opened packs stay resident for the session.
// Acceptable for an editor; worth revisiting if it becomes a problem.
uint32_t generation = 0;

// Misses are cached too, so a missing image is not retried every frame.
std::unordered_map<std::string, Image> textures;
std::unordered_map<std::string, std::unique_ptr<pack::trl::Trail>> trails;

// How many assets may be decoded in one frame. Small enough that a burst never costs a
// visible hitch, large enough that a map's markers fill in within a fraction of a second.
constexpr int kLoadsPerFrame = 4;
int budget = kLoadsPerFrame;

} // namespace

void beginFrame() {
    budget = kLoadsPerFrame;
}

void init(AddonAPI* aApi) {
    api = aApi;
}

void shutdown() {
    data_source.reset();
    pending_pack = nullptr;
    textures.clear();
    trails.clear();
    api = nullptr;
}

void setSource(std::unique_ptr<pack::DataSource> source) {
    data_source = std::move(source);
    textures.clear();
    trails.clear();
    ++generation;
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

    // Nexus hands back the *existing* texture for an identifier it has already seen, so a
    // reload under the same name would return the old image. A new generation gives the
    // next load a name Nexus has not seen. Only bumped when something was actually cached,
    // so an edit that touches no texture — a trail point being dragged — does not push
    // every later image into a fresh identifier.
    if (textures.erase(key) > 0) ++generation;
}

Image image(const std::string& path) {
    // No source at all is normal for a pack created in the editor: everything it has is a
    // pending asset until the first save.
    if (!api || path.empty()) return {};

    const std::string key = pack::normalizePath(path);
    auto it = textures.find(key);
    if (it != textures.end()) return it->second;

    // Not cached yet. Defer to a later frame rather than blowing the budget — but do not
    // record a miss, so it is retried next frame.
    if (budget <= 0) return {};
    --budget;

    std::vector<uint8_t> bytes;
    std::string error;

    // Added in the editor and not saved yet: those bytes are the truth for this path.
    if (const pack::Pack::AddedAsset* added = pending_pack ? pending_pack->findAddedAsset(key) : nullptr) {
        bytes = added->bytes;
    } else if (!data_source || !data_source->read(key, &bytes, &error) || bytes.empty()) {
        // The file genuinely is not in the pack. Remember that so it is not retried.
        textures.emplace(key, Image{});
        return {};
    }

    const std::string identifier = "MPE_" + std::to_string(generation) + "_" + key;
    // Synchronous on purpose: the async LoadFromMemory variant would need the bytes to
    // outlive this call, and decoding one PNG per newly visible marker is cheap.
    Texture* loaded = api->Textures.GetOrCreateFromMemory(identifier.c_str(), bytes.data(), bytes.size());
    if (!loaded || !loaded->Resource) {
        // The file is there but Nexus could not hand back a texture *this time* — it may
        // not have a device yet, or it may be creating the texture asynchronously.
        //
        // Deliberately not cached: caching this miss is what made a freshly opened pack
        // show every marker as a fallback dot until the pack was reloaded, because a
        // reload is the only thing that clears the cache.
        return {};
    }

    const Image result{static_cast<ImTextureID>(loaded->Resource), static_cast<int>(loaded->Width), static_cast<int>(loaded->Height)};
    textures.emplace(key, result);
    return result;
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

    if (budget <= 0) {
        if (deferred) *deferred = true;
        return nullptr;
    }
    --budget;

    std::unique_ptr<pack::trl::Trail> parsed;

    std::vector<uint8_t> bytes;
    std::string error;

    // Edited in the editor and not saved yet — the trail editor writes its working copy
    // back as a pending asset, so those bytes are what the world should draw.
    bool have_bytes = false;
    if (const pack::Pack::AddedAsset* added = pending_pack ? pending_pack->findAddedAsset(key) : nullptr) {
        bytes = added->bytes;
        have_bytes = true;
    } else {
        have_bytes = data_source && data_source->read(key, &bytes, &error);
    }

    if (have_bytes) {
        auto candidate = std::make_unique<pack::trl::Trail>();
        if (pack::trl::read(bytes, candidate.get(), &error)) parsed = std::move(candidate);
    }

    const pack::trl::Trail* result = parsed.get();
    trails.emplace(key, std::move(parsed));
    return result;
}

} // namespace resources
