#pragma once

#include <Nexus.h>
#include <imgui.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Images decoded into Nexus textures on demand, a few per frame.
namespace viewer {

// How many assets may be read and decoded in one frame, shared by everything that loads them, so
// arriving on a map with hundreds of new icons does not hitch.
class LoadBudget
{
public:
    explicit LoadBudget(int per_frame)
        : per_frame_(per_frame)
        , remaining_(per_frame) {}

    // Call once at the top of a frame.
    void reset() { remaining_ = per_frame_; }

    // Spends one load. False when this frame's are gone: try again next frame.
    bool take() {
        if (remaining_ <= 0) return false;
        --remaining_;
        return true;
    }

private:
    int per_frame_;
    int remaining_;
};

// A decoded image and the pixel size Nexus reports for it. `texture` is nullptr until the
// image is available, or if it does not exist or fails to decode.
struct Image {
    ImTextureID texture = nullptr;
    int width = 0;
    int height = 0;
};

class TextureCache
{
public:
    // Fills `bytes` with the file at a normalized path. False when there is no such file,
    // which is cached; a file that is there but empty counts as absent too.
    using Reader = std::function<bool(const std::string& key, std::vector<uint8_t>* bytes)>;

    // `prefix` starts every identifier handed to Nexus, which shares one texture namespace
    // between every addon loaded.
    explicit TextureCache(std::string prefix)
        : prefix_(std::move(prefix)) {}

    // Null leaves every lookup empty, which is what a test and a pack made in the editor
    // before Nexus is up both rely on.
    void setApi(AddonAPI* api) { api_ = api; }

    // The image at `key`, a normalized path, decoded through `read` if it is not cached.
    // Costs one load from `budget` on a miss, and none on a hit.
    Image get(const std::string& key, LoadBudget& budget, const Reader& read);

    // Drops one cached image. Needed when the bytes behind a path change, because a cache
    // hit would otherwise keep the old image forever.
    void forget(const std::string& key);

    // Drops everything, for a new set of files under the same paths.
    void clear();

private:
    std::string prefix_;
    AddonAPI* api_ = nullptr;

    // Nexus never releases a texture and returns the existing one for an identifier it has seen,
    // so a generation bumped whenever bytes may have changed keeps new loads under new names.
    // Superseded textures stay resident.
    uint32_t generation_ = 0;

    // Misses are cached too, so a missing image is not retried every frame.
    std::unordered_map<std::string, Image> images_;
};

} // namespace viewer
