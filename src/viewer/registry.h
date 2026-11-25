#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
// its packs, whether or not it exists: what the import reads, and where the editor's file
// browser starts. Empty when Windows will not say where Documents is.
std::filesystem::path blishMarkersFolder();

// What Pathing loads from one markers folder (PackInitiator.LoadAllPacks): every .zip and
// .taco under it at any depth, then the folder itself as an unpacked pack. The folder is
// only listed when it holds an .xml of its own outside any archive, since otherwise it is a
// pack with nothing in it. Sorted, so packs load — and override one another's category
// names — in the same order every time.
std::vector<std::filesystem::path> discoverPacks(const std::filesystem::path& folder);

// Whether two paths name the same pack: the same file or folder however spelled, or — when
// one is gone from disk — the same path once normalized. Never for an empty path.
bool samePack(const std::filesystem::path& a, const std::filesystem::path& b);

// Whether `path` is `folder` or under it, by their spelling once normalized. Never for an empty
// folder.
bool isInside(const std::filesystem::path& path, const std::filesystem::path& folder);

// One pack, read and baked, with its XML dropped: everything the viewer needs and none of
// the parsed documents, which for a large pack are most of its memory.
struct LoadedPack {
    std::filesystem::path path;
    std::string error; // why it did not load; empty when it did
    // writtenAt(path) as the load began: a pack written since is read again.
    std::filesystem::file_time_type written{};

    // Kept open: icons are read out of it as they come into view. Shared with the reads queued
    // on the worker, which may outlive the pack's being installed.
    std::shared_ptr<const pack::DataSource> source;

    // Every .lua file of the pack, by normalized path, read with the rest: a script loads and
    // requires on the frame, which never reads the disk. Scripts are kilobytes.
    std::unordered_map<std::string, std::string> scripts;

    BakedPathables pathables; // every trail's geometry and map id read already
    std::vector<BakedCategory> categories;

    // The pack's categories alone, with their attributes and nothing else of the model: what
    // a marker one of its scripts creates inherits from (Pack:CreateMarker).
    pack::Pack declared;
};

// When the pack at `path` was last written: an archive's own time, or for a folder the newest of
// the .xml files directly in it, which is where a save writes and where nearly every pack keeps
// its markers. The epoch for a pack that is not there.
std::filesystem::file_time_type writtenAt(const std::filesystem::path& path);

// Reads, bakes and drops the XML of the pack at `path`, and reads every trail's .trl. Slow —
// seconds for the largest packs — so it belongs on a worker.
LoadedPack loadPack(const std::filesystem::path& path);

// A category in the tree every pack shares.
struct Category {
    std::string id; // full dotted path, lowercase
    std::string name; // its own segment, as the pack that first declared it wrote it
    std::string label; // what the menu shows: the display name, else the name
    size_t parent = pack::kNoIndex;
    std::vector<size_t> children; // in the order packs first declared them

    bool is_separator = false;
    bool default_toggle = true;
    bool is_hidden = false;

    // The achievement the menu greys it out for once the account has done it, as
    // PathingCategoryNode does; the last declaration naming one wins, with its bit.
    std::optional<int> achievement_id;
    int achievement_bit = -1;
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

    // Queues `paths` behind whatever is queued already, starting the workers if they are not
    // running.
    void add(std::vector<std::filesystem::path> paths);

    // Drops everything queued or finished and not taken, stops after the packs being read, and
    // waits for them. A large pack mid-read is the longest this blocks.
    void stop();

    // The next pack in the order they were added, once it is read, or nothing. Poll once a
    // frame. In order, whichever finishes first: the order packs are installed in is the order
    // of their categories in the menu.
    std::unique_ptr<LoadedPack> take();

    // True until every pack handed to add() has been read and taken.
    bool busy() const;

    // How many packs are read at once: a pack is read on one thread, and the largest take most
    // of a second each, so a few at once is most of the gain while leaving the game its cores.
    static size_t workerCount();

private:
    void work();

    std::vector<std::thread> workers_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
    std::deque<std::pair<uint64_t, std::filesystem::path>> queued_; // by when it was added
    size_t loading_ = 0;
    std::map<uint64_t, std::unique_ptr<LoadedPack>> done_;
    uint64_t next_added_ = 0;
    uint64_t next_taken_ = 0;
};

} // namespace viewer::registry
