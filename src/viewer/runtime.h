#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "viewer/behaviors/entity.h"
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

    // What every pack's images are decoded through. Without it a pack's TextureCache hands back
    // no image at all, and the world draws nothing without one. Null in tests that draw nothing.
    AddonAPI* api = nullptr;

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

// The id the menu's All Markers box is kept under, as a category's would be: no category has an
// empty id. Off hides every pack, as Pathing's root category does.
constexpr const char* ALL_MARKERS = "";

// Per node of `tree`, whether what is under it shows: it and every ancestor are on, and All
// Markers is.
std::vector<char> visibleNodes(const registry::CategoryTree& tree, const store::CategoryStates& states);

// Whether a pathable under `node` shows, by `visible` from visibleNodes. One whose type names
// no category has nothing to turn off, so it shows.
inline bool nodeVisible(size_t node, const std::vector<char>& visible) {
    return node >= visible.size() || visible[node];
}

// LiveMarker::in_view_since until its first frame in view, and again whenever its category is
// turned off. A scene built without startFadeIns sees it as long in view, so fully faded in.
constexpr double kNotInView = -std::numeric_limits<double>::infinity();

// A marker on the live map: a copy of its pack's, which scripts are free to change.
struct LiveMarker {
    size_t pack = 0; // into Installed::packs: the pack it, or the script that made it, came from
    size_t node = pack::kNoIndex; // its category in the shared tree; kNoIndex when it has none
    BakedMarker marker;

    // Whose files `marker.icon` is read from: `pack`'s, unless a script handed it another
    // pack's texture.
    size_t texture_pack = 0;

    // By a script. Left in place rather than erased, so that every index a script's handle
    // holds stays valid until the map, and with it the script state, is thrown away.
    bool removed = false;

    behaviors::EntityState behavior; // what its behaviors remember, fresh with every live map

    // The frame's seconds when it last came into view, which it fades in from: Pathing's
    // AnimatedFadeOpacity. See startFadeIns.
    double in_view_since = kNotInView;
};

struct LiveTrail {
    size_t pack = 0;
    size_t node = pack::kNoIndex;
    BakedTrail trail; // its geometry shared with the pack's, not copied
    size_t texture_pack = 0; // as LiveMarker's
    bool removed = false;
    behaviors::EntityState behavior;
    double in_view_since = kNotInView; // as LiveMarker's
};

// What of the installed packs is on one map, copied out of each pack's baked pathables, and
// whatever scripts made of it since: Pathing's PackState.Entities.
struct LiveMap {
    int map_id = kMapUnknown;
    // What it was built from: the Installed::revision, or for the edited pack's, how many times
    // it was built. Whatever keeps something of it — the map's trail lines — goes by this.
    uint64_t revision = 0;

    std::vector<LiveMarker> markers;
    std::vector<LiveTrail> trails;
};

// The node `category` of pack `pack_index` landed in, or kNoIndex.
size_t nodeOf(const Installed& installed, size_t pack_index, size_t category);

// Per node of `tree`, whether anything under it is on `live`'s map: Pathing's smart categories,
// which leave the rest out of the menu (CategoryContextMenuStrip.EnsureActiveCategoriesPopulated).
std::vector<char> nodesOnMap(const registry::CategoryTree& tree, const LiveMap& live);

// Copies out every pack's markers and trails on `map_id`. Done on a map change or when the
// packs change, not per frame: it walks every marker of every pack.
LiveMap liveMap(const Installed& installed, int map_id);

// Starts the fade-in of every pathable that came into view since the last call, at `seconds`:
// one new on the live map, made by a script, or whose category was turned back on. Pathing's
// PathingEntity starts it at the first update after it is made or while its category was
// inactive; being filtered or hidden by a script starts none.
void startFadeIns(LiveMap* live, const std::vector<char>& visible, double seconds);

} // namespace viewer::runtime
