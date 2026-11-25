#pragma once

#include <Nexus.h>
#include <imgui.h>

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "util/job_queue.h"

// Images made into Nexus textures on demand, read and decoded on a worker.
namespace viewer {

// A decoded image and the pixel size Nexus reports for it. `texture` is nullptr until the
// image is available, or if it does not exist or fails to decode.
struct Image {
    ImTextureID texture = nullptr;
    int width = 0;
    int height = 0;
};

// The pixels of the textures handed to Nexus and not made yet, for every cache that hands to it.
// Nexus makes everything handed to it at its next Present in one go, copying each image's pixels
// to the GPU there, on the render thread (Graphics/Textures/TxLoader.cpp, Advance): what is out
// at once is what one Present uploads. Counted in pixels, not images, since a pack's images run
// from a 32×32 icon to Hero's 2080×2080 shapes, 17 MB each. The frame's alone.
class TextureUploads
{
public:
    // One 2048×2048 image's worth, 16 MB of RGBA, or a thousand 64×64 icons. Not measured: if a
    // Present that makes this much still shows as a stutter, this is the number to lower.
    static constexpr int64_t MAX_PIXELS = int64_t{2048} * 2048;

    // Whether an image of `pixels` may be handed now. Always when nothing is out, so an image
    // larger than the cap goes alone rather than never.
    bool allows(int64_t pixels) const { return out_.empty() || pixels_ + pixels <= MAX_PIXELS; }

    // The image Nexus will know as `identifier`, of `pixels`, was handed to `api`'s Nexus.
    void handed(AddonAPI* api, std::string identifier, int64_t pixels);

    // Once a frame, before any lookup: lets go of what Nexus has made since, and of what it has
    // sat on for so long that it never will.
    void nextFrame();

private:
    struct Out {
        AddonAPI* api;
        std::string identifier;
        int64_t pixels;
        int frames; // frames since it was handed
    };
    std::vector<Out> out_;
    int64_t pixels_ = 0;
};

class TextureCache
{
public:
    // Fills `bytes` with the file at a normalized path. False when there is no such file,
    // which is cached; a file that is there but empty counts as absent too. Runs on the
    // worker, so it holds what it reads from and reads nothing the frame changes.
    using Reader = std::function<bool(const std::string& key, std::vector<uint8_t>* bytes)>;

    // `prefix` starts every identifier handed to Nexus, which shares one texture namespace
    // between every addon loaded.
    explicit TextureCache(std::string prefix)
        : prefix_(std::move(prefix)) {}

    // Where images come from, where they are read, and what bounds how much is handed to Nexus at
    // once, which outlive the cache. Any null leaves every lookup empty, which is what a test
    // that draws nothing and a pack made in the editor before Nexus is up both rely on.
    void setApi(AddonAPI* api, util::JobQueue* jobs, TextureUploads* uploads) {
        api_ = api;
        jobs_ = jobs;
        uploads_ = uploads;
    }

    // The image at `key`, a normalized path: empty until the worker has read it through the
    // Reader `reader_for` makes, which is only asked for on a miss, `uploads` has room for it,
    // and Nexus has made it.
    Image get(const std::string& key, const std::function<Reader()>& reader_for);

    // Drops one cached image. Needed when the bytes behind a path change, because a cache
    // hit would otherwise keep the old image forever.
    void forget(const std::string& key);

    // Drops everything, for a new set of files under the same paths.
    void clear();

private:
    std::string prefix_;
    AddonAPI* api_ = nullptr;
    util::JobQueue* jobs_ = nullptr;
    TextureUploads* uploads_ = nullptr;

    // Nexus never releases a texture and returns the existing one for an identifier it has seen,
    // so a generation bumped whenever bytes may have changed keeps new loads under new names.
    // Superseded textures stay resident.
    uint32_t generation_ = 0;

    // What the reads still queued were asked for: one that finds it gone reads nothing. Made
    // again by clear(), and dropped with the cache.
    std::shared_ptr<char> wanted_ = std::make_shared<char>();

    // A file read on the worker, with the pixels it decodes to.
    struct Read {
        std::vector<uint8_t> bytes;
        int64_t pixels = 0;
    };

    struct Entry {
        // Until the worker has read it: nothing for a file that is not there or is no image.
        std::future<std::optional<Read>> reading;
        // Then held until `uploads` has room, and handed to Nexus.
        Read read;
        bool handed = false;
        // Then asked of Nexus every lookup until it has made the texture.
        std::string identifier;
        Image image;
        bool done = false; // `image` is final, a miss included
    };
    std::unordered_map<std::string, Entry> images_;
};

} // namespace viewer
