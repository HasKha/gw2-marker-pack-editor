#include "viewer/textures.h"

namespace viewer {

Image TextureCache::get(const std::string& key, LoadBudget& budget, const Reader& read) {
    if (!api_ || key.empty()) return {};

    if (auto it = images_.find(key); it != images_.end()) return it->second;

    // Not cached yet. Defer to a later frame rather than overrun the budget — but do not
    // record a miss, so it is retried next frame.
    if (!budget.take()) return {};

    std::vector<uint8_t> bytes;
    if (!read(key, &bytes) || bytes.empty()) {
        // The file genuinely is not there. Remember that so it is not retried.
        images_.emplace(key, Image{});
        return {};
    }

    const std::string identifier = prefix_ + std::to_string(generation_) + "_" + key;
    // Synchronous on purpose: the async LoadFromMemory variant would need the bytes to
    // outlive this call, and decoding one PNG per newly visible marker is cheap.
    Texture* loaded = api_->Textures.GetOrCreateFromMemory(identifier.c_str(), bytes.data(), bytes.size());
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
    images_.emplace(key, result);
    return result;
}

void TextureCache::forget(const std::string& key) {
    // Only bumped when something was actually cached, so forgetting a path that never held
    // a texture — a trail point being dragged rewrites its .trl — does not push every later
    // image into a fresh identifier.
    if (images_.erase(key) > 0) ++generation_;
}

void TextureCache::clear() {
    images_.clear();
    ++generation_;
}

} // namespace viewer
