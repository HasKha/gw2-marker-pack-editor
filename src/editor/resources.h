#pragma once

#include <Nexus.h>
#include <imgui.h>

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "pack/model.h"
#include "pack/source.h"
#include "pack/trl.h"
#include "util/job_queue.h"
#include "viewer/render/textures.h"

namespace resources {

// Images and trail data read out of the open pack on demand, since a large pack ships thousands,
// on a worker of the cache's own: a frame asks, and a later frame has the answer. Dropping the
// cache drops everything and waits for the reads in flight.
class Cache
{
public:
    // `pending` is the pack whose assets added in the editor are read before the source, since
    // they are not in it until a save; it outlives the cache. Null for none.
    explicit Cache(const pack::Pack* pending);
    ~Cache();

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    // Where images go to be drawn. Until it is set, image() and texture() find nothing.
    void setApi(AddonAPI* api);

    // Blocks until every read asked for so far has run. Never from a frame: for tests.
    void waitForReads();

    // Takes ownership of the pack's source and drops every cached asset. The source has to
    // outlive the pack because assets are read from it long after loading; a read in flight
    // keeps it open past this.
    void setSource(std::unique_ptr<pack::DataSource> source);
    const pack::DataSource* source() const { return pack_source.get(); }

    // Drops one cached asset, for when the bytes behind a path change.
    void forget(const std::string& path);

    // Paths are resolved through pack::normalizePath, so any spelling a pack uses works.
    viewer::Image image(const std::string& path);
    ImTextureID texture(const std::string& path) { return image(path).texture; }

    // nullptr if the file is missing or unparseable. Shared, because the viewer draws the open
    // pack's trails from it and keeps one past setSource, forget or the cache.
    //
    // `deferred` tells the two kinds of nullptr apart: true means it is being read, so a later
    // frame will have it; false means it was read and the miss is cached, so no later frame will.
    std::shared_ptr<const pack::trl::Trail> trail(const std::string& path, bool* deferred = nullptr);

    // The colour the image at `path` gives a trail on the map: viewer::trail_colour::sample, or
    // its NO_COLOUR for a file that is not there. Cached; nothing, with `*deferred` set, while
    // it is being read.
    std::optional<ImU32> trailColour(const std::string& path, bool* deferred = nullptr);

private:
    template <class T, class Ask>
    std::optional<T> lookup(
        std::unordered_map<std::string, T>& done,
        std::unordered_map<std::string, std::future<T>>& reading,
        const std::string& key,
        bool* deferred,
        const Ask& ask);
    viewer::TextureCache::Reader readerFor(const std::string& key) const;
    void clearAll();

    // First, so it goes last: what it still runs holds nothing of the cache.
    util::JobQueue jobs;

    std::shared_ptr<const pack::DataSource> pack_source;
    const pack::Pack* pending;

    viewer::TextureCache textures{"Markus_"};

    // Misses are cached too, so a missing trail is not read again. Each is read and parsed on
    // the worker, and waits in `reading_trails` until then.
    std::unordered_map<std::string, std::shared_ptr<const pack::trl::Trail>> trails;
    std::unordered_map<std::string, std::future<std::shared_ptr<const pack::trl::Trail>>> reading_trails;

    // What each image gives a trail on the map, missing ones included, sampled on the worker.
    std::unordered_map<std::string, ImU32> trail_colours;
    std::unordered_map<std::string, std::future<ImU32>> reading_colours;

    // What the trail and colour reads still queued were asked for: one that finds it gone reads
    // nothing, so a new source or the cache going does not wait behind the old pack's reads.
    // Made again by clearAll; the TextureCache keeps its own.
    std::shared_ptr<char> wanted = std::make_shared<char>();
};

} // namespace resources
