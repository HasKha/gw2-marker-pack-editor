#include "editor/resources.h"

#include <vector>

#include "viewer/render/trail_colour.h"
#include "viewer/viewer.h"

namespace resources {

Cache::Cache(const pack::Pack* pending)
    : pending(pending) {}

Cache::~Cache() {
    clearAll();
    textures.setApi(nullptr, nullptr, nullptr);
    pack_source.reset();
    jobs.stop();
}

void Cache::setApi(AddonAPI* api) {
    textures.setApi(api, &jobs, &viewer::textureUploads());
}

// What reads the bytes behind a normalized path on the worker. Added in the editor and not
// saved yet, those bytes are the truth for this path: the trail editor writes its working copy
// back as a pending asset, and an added image is not in the source until a save. They are
// copied now, since the frame goes on changing them; the source is kept open by the read.
viewer::TextureCache::Reader Cache::readerFor(const std::string& key) const {
    if (const pack::Pack::AddedAsset* added = pending ? pending->findAddedAsset(key) : nullptr) {
        return [bytes = added->bytes](const std::string&, std::vector<uint8_t>* out) {
            *out = bytes;
            return true;
        };
    }
    return [source = pack_source](const std::string& path, std::vector<uint8_t>* out) {
        std::string error;
        return source && source->read(path, out, &error);
    };
}

// The answer `ask` gives for the bytes at `key`, null for none: from `done`, or worked out from a
// pending asset now, since those bytes are in memory and a trail point being dragged rewrites
// its .trl every frame, or else from the source on the worker, taken from `reading` once
// ready. Nothing, with `*deferred` set, while it is being read.
template <class T, class Ask>
std::optional<T> Cache::lookup(
    std::unordered_map<std::string, T>& done,
    std::unordered_map<std::string, std::future<T>>& reading,
    const std::string& key,
    bool* deferred,
    const Ask& ask) {
    if (deferred) *deferred = false;
    if (const auto it = done.find(key); it != done.end()) return it->second;
    if (const pack::Pack::AddedAsset* added = pending ? pending->findAddedAsset(key) : nullptr) {
        return done.emplace(key, ask(&added->bytes)).first->second;
    }

    auto [it, missed] = reading.try_emplace(key);
    if (missed)
        it->second = jobs.ask([read = readerFor(key), key, ask, still = std::weak_ptr<char>(wanted)] {
            std::vector<uint8_t> bytes;
            return ask(!still.expired() && read(key, &bytes) ? &bytes : nullptr);
        });
    if (!util::ready(it->second)) {
        if (deferred) *deferred = true;
        return std::nullopt;
    }
    // A read that threw answers as a file that is not there.
    std::optional<T> taken = util::take(it->second);
    T answer = taken ? std::move(*taken) : ask(nullptr);
    reading.erase(it);
    done.emplace(key, answer);
    return answer;
}

// Drops every cached and pending read, for a new source.
void Cache::clearAll() {
    textures.clear();
    trails.clear();
    reading_trails.clear();
    trail_colours.clear();
    reading_colours.clear();
    wanted = std::make_shared<char>();
}

void Cache::waitForReads() {
    jobs.wait();
}

void Cache::setSource(std::unique_ptr<pack::DataSource> source) {
    pack_source = std::move(source);
    clearAll();
}

void Cache::forget(const std::string& path) {
    const std::string key = pack::normalizePath(path);
    trails.erase(key);
    reading_trails.erase(key);
    trail_colours.erase(key);
    reading_colours.erase(key);
    textures.forget(key);
}

viewer::Image Cache::image(const std::string& path) {
    // No source at all is normal for a pack created in the editor: everything it has is a
    // pending asset until the first save.
    if (path.empty()) return {};
    const std::string key = pack::normalizePath(path);
    return textures.get(key, [this, &key] { return readerFor(key); });
}

std::shared_ptr<const pack::trl::Trail> Cache::trail(const std::string& path, bool* deferred) {
    if (deferred) *deferred = false;
    if (path.empty()) return nullptr;
    const auto parse = [](const std::vector<uint8_t>* bytes) -> std::shared_ptr<const pack::trl::Trail> {
        auto parsed = std::make_shared<pack::trl::Trail>();
        std::string error;
        return bytes && pack::trl::read(*bytes, parsed.get(), &error) ? parsed : nullptr;
    };
    return lookup(trails, reading_trails, pack::normalizePath(path), deferred, parse).value_or(nullptr);
}

std::optional<ImU32> Cache::trailColour(const std::string& path, bool* deferred) {
    const auto sample = [](const std::vector<uint8_t>* bytes) {
        return bytes ? viewer::trail_colour::sample(*bytes) : viewer::trail_colour::NO_COLOUR;
    };
    return lookup(trail_colours, reading_colours, pack::normalizePath(path), deferred, sample);
}

} // namespace resources
