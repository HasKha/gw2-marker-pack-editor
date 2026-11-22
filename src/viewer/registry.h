#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "pack/source.h"
#include "viewer/bake.h"

// The installed packs: where they are, loading them on a worker, and the one category tree
// they share.
namespace viewer::registry {

// <Documents>/Guild Wars 2/addons/blishhud/markers, where Blish HUD's Pathing module keeps
// its packs, whether or not it exists. Empty when Windows will not say where Documents is.
std::filesystem::path blishMarkersFolder();

// What Pathing loads from one markers folder (PackInitiator.LoadAllPacks): every .zip and
// .taco under it at any depth, then the folder itself as an unpacked pack. The folder is
// only listed when it holds an .xml of its own outside any archive, since otherwise it is a
// pack with nothing in it. Sorted, so packs load — and override one another's category
// names — in the same order every time.
std::vector<std::filesystem::path> discoverPacks(const std::filesystem::path& folder);

// One pack, read and baked, with its XML dropped: everything the viewer needs and none of
// the parsed documents, which for a large pack are most of its memory.
struct LoadedPack {
    std::filesystem::path path;
    std::string error; // why it did not load; empty when it did

    // Kept open: icons are read out of it as they come into view.
    std::unique_ptr<pack::DataSource> source;

    BakedPathables pathables; // every trail's geometry and map id read already
    std::vector<BakedCategory> categories;
};

// Reads, bakes and drops the XML of the pack at `path`, and reads every trail's .trl. Slow —
// seconds for the largest packs — so it belongs on a worker.
LoadedPack loadPack(const std::filesystem::path& path);

// A category in the tree every pack shares.
struct Category {
    std::string id; // full dotted path, lowercase
    std::string label; // what the menu shows: the display name, else the name
    size_t parent = pack::kNoIndex;
    std::vector<size_t> children; // in the order packs first declared them

    bool is_separator = false;
    bool default_toggle = true;
    bool is_hidden = false;
};

// Merged by id, as Pathing's SharedPackCollection is: packs that extend another pack's
// categories rely on landing in the same node. A parent always comes before its children.
struct CategoryTree {
    std::vector<Category> nodes;
    std::vector<size_t> roots;
    std::unordered_map<std::string, size_t> by_id;

    // kNoIndex when no pack declares it.
    size_t find(std::string_view id) const;
};

// Merges one pack's categories into `tree`, by id, and returns the node each of them landed
// in. A declaration overrides only what it says — display name, separator, default toggle,
// hidden — so a later pack renames a shared category but does not reset its flags.
//
// Attributes are not merged: a marker inherits from its own pack's declarations only. That
// is a difference from Pathing, which inherits across packs, and no pack in the corpus has
// been seen relying on it.
std::vector<size_t> mergeCategories(CategoryTree* tree, const std::vector<BakedCategory>& categories);

// Loads packs one after another on a thread of its own and hands each over as it is done, so
// the first pack draws while the largest is still being read.
class Loader
{
public:
    Loader() = default;
    ~Loader();

    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    // Starts loading `paths`, in order. A load still running is stopped first.
    void start(std::vector<std::filesystem::path> paths);

    // Stops after the pack being read and waits for it. Called from unload, so a large pack
    // mid-read is the longest this can block.
    void stop();

    // A finished pack, or nothing. Poll once a frame.
    std::unique_ptr<LoadedPack> take();

    // True until every pack handed to start() has been read and taken.
    bool busy() const;

private:
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<size_t> remaining_{0};

    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<LoadedPack>> done_;
};

} // namespace viewer::registry
