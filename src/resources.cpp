#include "resources.h"

#include <unordered_map>
#include <vector>

namespace resources {

namespace {

AddonAPI* api = nullptr;
std::unique_ptr<pack::DataSource> data_source;

// Nexus owns its texture cache and exposes no way to release one, so identifiers must not
// collide between packs. Bumping a generation on every load keeps them distinct.
//
// The cost is that textures from previously opened packs stay resident for the session.
// Acceptable for an editor; worth revisiting if it becomes a problem.
uint32_t generation = 0;

// nullptr entries are cached too, so a missing image is not retried every frame.
std::unordered_map<std::string, ImTextureID> textures;
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

ImTextureID texture(const std::string& path) {
    if (!api || !data_source || path.empty()) return nullptr;

    const std::string key = pack::normalizePath(path);
    auto it = textures.find(key);
    if (it != textures.end()) return it->second;

    // Not cached yet. Defer to a later frame rather than blowing the budget — but do not
    // record a miss, so it is retried next frame.
    if (budget <= 0) return nullptr;
    --budget;

    std::vector<uint8_t> bytes;
    std::string error;
    if (!data_source->read(key, &bytes, &error) || bytes.empty()) {
        // The file genuinely is not in the pack. Remember that so it is not retried.
        textures.emplace(key, nullptr);
        return nullptr;
    }

    const std::string identifier = "MPE_" + std::to_string(generation) + "_" + key;
    // Synchronous on purpose: the async LoadFromMemory variant would need the bytes to
    // outlive this call, and decoding one PNG per newly visible marker is cheap.
    Texture* loaded = api->Textures.GetOrCreateFromMemory(identifier.c_str(), bytes.data(), bytes.size());
    ImTextureID result = loaded ? static_cast<ImTextureID>(loaded->Resource) : nullptr;

    if (!result) {
        // The file is there but Nexus could not hand back a texture *this time* — it may
        // not have a device yet, or it may be creating the texture asynchronously.
        //
        // Deliberately not cached: caching this nullptr is what made a freshly opened pack
        // show every marker as a fallback dot until the pack was reloaded, because a
        // reload is the only thing that clears the cache.
        return nullptr;
    }

    textures.emplace(key, result);
    return result;
}

const pack::trl::Trail* trail(const std::string& path) {
    if (!data_source || path.empty()) return nullptr;

    const std::string key = pack::normalizePath(path);
    auto it = trails.find(key);
    if (it != trails.end()) return it->second.get();

    if (budget <= 0) return nullptr;
    --budget;

    std::unique_ptr<pack::trl::Trail> parsed;

    std::vector<uint8_t> bytes;
    std::string error;
    if (data_source->read(key, &bytes, &error)) {
        auto candidate = std::make_unique<pack::trl::Trail>();
        if (pack::trl::read(bytes, candidate.get(), &error)) parsed = std::move(candidate);
    }

    const pack::trl::Trail* result = parsed.get();
    trails.emplace(key, std::move(parsed));
    return result;
}

} // namespace resources
