#include "editor/resources.h"

#include <future>
#include <unordered_map>
#include <vector>

#include "util/job_queue.h"
#include "viewer/trail_colour.h"
#include "viewer/viewer.h"

namespace resources {

namespace {

// The open pack's files are read here and never on the frame. Its own, apart from the
// viewer's: it runs only while the editor does.
util::JobQueue jobs;

std::shared_ptr<const pack::DataSource> data_source;

// Assets added in the editor live here until a save puts them in the source.
const pack::Pack* pending_pack = nullptr;

viewer::TextureCache textures{"Markus_"};

// Misses are cached too, so a missing trail is not read again. Each is read and parsed on the
// worker, and waits in `reading_trails` until then.
std::unordered_map<std::string, std::shared_ptr<const pack::trl::Trail>> trails;
std::unordered_map<std::string, std::future<std::shared_ptr<const pack::trl::Trail>>> reading_trails;

// What each image gives a trail on the map, missing ones included, sampled on the worker.
std::unordered_map<std::string, ImU32> trail_colours;
std::unordered_map<std::string, std::future<ImU32>> reading_colours;

// What the trail and colour reads still queued were asked for: one that finds it gone reads
// nothing, so a new source or a shutdown does not wait behind the old pack's reads. Made again
// by clearAll; the TextureCache keeps its own.
std::shared_ptr<char> wanted = std::make_shared<char>();

// What reads the bytes behind a normalized path on the worker. Added in the editor and not
// saved yet, those bytes are the truth for this path: the trail editor writes its working copy
// back as a pending asset, and an added image is not in the source until a save. They are
// copied now, since the frame goes on changing them; the source is kept open by the read.
viewer::TextureCache::Reader readerFor(const std::string& key) {
    if (const pack::Pack::AddedAsset* added = pending_pack ? pending_pack->findAddedAsset(key) : nullptr) {
        return [bytes = added->bytes](const std::string&, std::vector<uint8_t>* out) {
            *out = bytes;
            return true;
        };
    }
    return [source = data_source](const std::string& key, std::vector<uint8_t>* out) {
        std::string error;
        return source && source->read(key, out, &error);
    };
}

// The answer `ask` gives for the bytes at `key`, null for none: from `done`, or worked out from a
// pending asset now, since those bytes are in memory and a trail point being dragged rewrites
// its .trl every frame, or else from the source on the worker, taken from `reading` once
// ready. Nothing, with `*deferred` set, while it is being read.
template <class T, class Ask>
std::optional<T> lookup(
    std::unordered_map<std::string, T>& done,
    std::unordered_map<std::string, std::future<T>>& reading,
    const std::string& key,
    bool* deferred,
    const Ask& ask) {
    if (deferred) *deferred = false;
    if (const auto it = done.find(key); it != done.end()) return it->second;
    if (const pack::Pack::AddedAsset* added = pending_pack ? pending_pack->findAddedAsset(key) : nullptr) {
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
void clearAll() {
    textures.clear();
    trails.clear();
    reading_trails.clear();
    trail_colours.clear();
    reading_colours.clear();
    wanted = std::make_shared<char>();
}

} // namespace

void init(AddonAPI* api) {
    textures.setApi(api, &jobs, &viewer::textureUploads());
}

void shutdown() {
    clearAll();
    textures.setApi(nullptr, nullptr, nullptr);
    data_source.reset();
    pending_pack = nullptr;
    jobs.stop();
}

void waitForReads() {
    jobs.wait();
}

void setSource(std::unique_ptr<pack::DataSource> source) {
    data_source = std::move(source);
    clearAll();
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
    reading_trails.erase(key);
    trail_colours.erase(key);
    reading_colours.erase(key);
    textures.forget(key);
}

viewer::Image image(const std::string& path) {
    // No source at all is normal for a pack created in the editor: everything it has is a
    // pending asset until the first save.
    if (path.empty()) return {};
    const std::string key = pack::normalizePath(path);
    return textures.get(key, [&key] { return readerFor(key); });
}

ImTextureID texture(const std::string& path) {
    return image(path).texture;
}

std::shared_ptr<const pack::trl::Trail> trail(const std::string& path, bool* deferred) {
    if (deferred) *deferred = false;
    if (path.empty()) return nullptr;
    const auto parse = [](const std::vector<uint8_t>* bytes) -> std::shared_ptr<const pack::trl::Trail> {
        auto parsed = std::make_shared<pack::trl::Trail>();
        std::string error;
        return bytes && pack::trl::read(*bytes, parsed.get(), &error) ? parsed : nullptr;
    };
    return lookup(trails, reading_trails, pack::normalizePath(path), deferred, parse).value_or(nullptr);
}

std::optional<ImU32> trailColour(const std::string& path, bool* deferred) {
    const auto sample = [](const std::vector<uint8_t>* bytes) {
        return bytes ? viewer::trail_colour::sample(*bytes) : viewer::trail_colour::NO_COLOUR;
    };
    return lookup(trail_colours, reading_colours, pack::normalizePath(path), deferred, sample);
}

} // namespace resources
