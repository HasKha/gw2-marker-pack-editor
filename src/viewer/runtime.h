#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "viewer/registry.h"
#include "viewer/store.h"
#include "viewer/textures.h"

// The packs installed this session, and which of their markers and trails are live: on the
// current map, under categories that are on.
namespace viewer::runtime {

struct InstalledPack {
    InstalledPack(std::unique_ptr<registry::LoadedPack> loaded, std::string texture_prefix)
        : loaded(std::move(loaded))
        , textures(std::move(texture_prefix)) {}

    std::unique_ptr<registry::LoadedPack> loaded;
    std::vector<size_t> node_of; // a category of the pack -> its node in the shared tree
    TextureCache textures;
};

struct Installed {
    // In the order they were installed. One that failed to load is kept, with its error and
    // nothing else, so whatever lists the packs can say why.
    std::vector<std::unique_ptr<InstalledPack>> packs;
    registry::CategoryTree tree;

    // Moves whenever a pack comes or goes, so whatever was derived from the packs knows to
    // derive it again.
    uint64_t revision = 0;

    // Every install gets its own texture prefix, never reused. A pack reinstalled after the
    // editor saved it has new bytes under old paths, and Nexus would hand back the old
    // textures for the old identifiers.
    uint32_t next_serial = 0;
};

// Adds a loaded pack and merges its categories into the tree.
void install(Installed* installed, std::unique_ptr<registry::LoadedPack> loaded);

// Removes the pack loaded from `path`, if there is one, closing its source. The tree is merged
// again from what is left: a merged node cannot be taken apart one pack at a time, since a
// later pack may have renamed it. False when no pack came from `path`.
bool uninstall(Installed* installed, const std::filesystem::path& path);

bool isInstalled(const Installed& installed, const std::filesystem::path& path);

// Drops every pack, keeping the counters: a revision or a texture prefix is never reused, or
// something derived from the old packs would pass for current.
void clear(Installed* installed);

// Per node of `tree`, whether what is under it shows: it and every ancestor are on.
std::vector<char> visibleNodes(const registry::CategoryTree& tree, const store::CategoryStates& states);

// What of the installed packs is on one map, as indices into each pack's baked pathables.
struct LiveMap {
    int map_id = kMapUnknown;
    uint64_t revision = 0; // the Installed::revision it was built from

    struct Ref {
        size_t pack = 0;
        size_t index = 0;
    };
    std::vector<Ref> markers;
    std::vector<Ref> trails;
};

// Scans every pack for `map_id`. Done on a map change or when the packs change, not per frame:
// it walks every marker of every pack.
LiveMap liveMap(const Installed& installed, int map_id);

} // namespace viewer::runtime
