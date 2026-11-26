#include "editor/resources.h"

#include <vector>

#include "viewer/render/trail_colour.h"
#include "viewer/viewer.h"

namespace resources {

Cache::Cache(const pack::Pack* pending)
    : pending_(pending) {}

Cache::~Cache() {
    clearAll();
    textures_.setApi(nullptr, nullptr, nullptr);
    source_.reset();
    jobs_.stop();
}

void Cache::setApi(AddonAPI* api) {
    textures_.setApi(api, &jobs_, &viewer::textureUploads());
}

// What reads the bytes behind a normalized path on the worker. Added in the editor and not
// saved yet, those bytes are the truth for this path: the trail editor writes its working copy
// back as a pending asset, and an added image is not in the source until a save. They are
// copied now, since the frame goes on changing them; the source is kept open by the read.
viewer::TextureCache::Reader Cache::readerFor(const std::string& key) const {
    if (const pack::Pack::AddedAsset* added = pending_ ? pending_->findAddedAsset(key) : nullptr) {
        return [bytes = added->bytes](const std::string&, std::vector<uint8_t>* out) {
            *out = bytes;
            return true;
        };
    }
    return [source = source_](const std::string& path, std::vector<uint8_t>* out) {
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
    if (const pack::Pack::AddedAsset* added = pending_ ? pending_->findAddedAsset(key) : nullptr) {
        return done.emplace(key, ask(&added->bytes)).first->second;
    }

    auto [it, missed] = reading.try_emplace(key);
    if (missed)
        it->second = jobs_.ask([read = readerFor(key), key, ask, still = std::weak_ptr<char>(wanted_)] {
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
    textures_.clear();
    trails_.clear();
    reading_trails_.clear();
    trail_colours_.clear();
    reading_colours_.clear();
    wanted_ = std::make_shared<char>();
}

void Cache::waitForReads() {
    jobs_.wait();
}

void Cache::setSource(std::unique_ptr<pack::DataSource> source) {
    source_ = std::move(source);
    clearAll();
}

void Cache::forget(const std::string& path) {
    const std::string key = pack::normalizePath(path);
    trails_.erase(key);
    reading_trails_.erase(key);
    trail_colours_.erase(key);
    reading_colours_.erase(key);
    textures_.forget(key);
}

viewer::Image Cache::image(const std::string& path) {
    // No source at all is normal for a pack created in the editor: everything it has is a
    // pending asset until the first save.
    if (path.empty()) return {};
    const std::string key = pack::normalizePath(path);
    return textures_.get(key, [this, &key] { return readerFor(key); });
}

std::shared_ptr<const pack::trl::Trail> Cache::trail(const std::string& path, bool* deferred) {
    if (deferred) *deferred = false;
    if (path.empty()) return nullptr;
    const auto parse = [](const std::vector<uint8_t>* bytes) -> std::shared_ptr<const pack::trl::Trail> {
        auto parsed = std::make_shared<pack::trl::Trail>();
        std::string error;
        return bytes && pack::trl::read(*bytes, parsed.get(), &error) ? parsed : nullptr;
    };
    return lookup(trails_, reading_trails_, pack::normalizePath(path), deferred, parse).value_or(nullptr);
}

std::optional<ImU32> Cache::trailColour(const std::string& path, bool* deferred) {
    const auto sample = [](const std::vector<uint8_t>* bytes) {
        return bytes ? viewer::trail_colour::sample(*bytes) : viewer::trail_colour::NO_COLOUR;
    };
    return lookup(trail_colours_, reading_colours_, pack::normalizePath(path), deferred, sample);
}

} // namespace resources
