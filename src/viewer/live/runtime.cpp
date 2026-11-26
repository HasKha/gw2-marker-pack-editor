#include "viewer/live/runtime.h"

#include <algorithm>
#include <utility>

#include "pack/source.h"

namespace viewer::runtime {

namespace {

void mergeInto(Installed* installed, InstalledPack* pack) {
    if (pack->loaded->error.empty()) pack->node_of = registry::mergeCategories(&installed->tree, pack->loaded->categories);
}

} // namespace

void install(Installed* installed, std::unique_ptr<registry::LoadedPack> loaded) {
    const std::string prefix = "MPV" + std::to_string(installed->next_serial++) + "_";
    auto pack = std::make_unique<InstalledPack>(std::move(loaded), prefix);
    pack->textures.setApi(installed->api, installed->jobs, installed->uploads);
    mergeInto(installed, pack.get());
    const BakedPathables& pathables = pack->loaded->pathables;
    for (size_t i = 0; i < pathables.markers.size(); ++i) pack->on_map[pathables.markers[i].map_id].markers.push_back(i);
    for (size_t i = 0; i < pathables.trails.size(); ++i) pack->on_map[pathables.trails[i].map_id].trails.push_back(i);
    installed->packs.push_back(std::move(pack));
    ++installed->revision;
}

bool uninstall(Installed* installed, const std::filesystem::path& path) {
    const auto it = std::find_if(installed->packs.begin(), installed->packs.end(), [&](const std::unique_ptr<InstalledPack>& pack) {
        return registry::samePack(pack->loaded->path, path);
    });
    if (it == installed->packs.end()) return false;
    installed->packs.erase(it);

    installed->tree = registry::CategoryTree{};
    for (const std::unique_ptr<InstalledPack>& pack : installed->packs) mergeInto(installed, pack.get());
    ++installed->revision;
    return true;
}

bool isInstalled(const Installed& installed, const std::filesystem::path& path) {
    return std::any_of(installed.packs.begin(), installed.packs.end(), [&](const std::unique_ptr<InstalledPack>& pack) {
        return registry::samePack(pack->loaded->path, path);
    });
}

void clear(Installed* installed) {
    installed->packs.clear();
    installed->tree = registry::CategoryTree{};
    ++installed->revision;
}

std::vector<char> visibleNodes(const registry::CategoryTree& tree, const store::CategoryStates& states) {
    // Parents come before their children, so one pass in order sees every parent decided.
    std::vector<char> visible(tree.nodes.size(), 0);
    if (!states.enabled(ALL_MARKERS, true)) return visible;
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        const registry::Category& node = tree.nodes[i];
        const bool parent_visible = node.parent == pack::kNoIndex || visible[node.parent];
        visible[i] = parent_visible && states.enabled(node.id, node.default_toggle);
    }
    return visible;
}

std::vector<char> nodesOnMap(const registry::CategoryTree& tree, const LiveMap& live) {
    std::vector<char> on_map(tree.nodes.size(), 0);
    const auto mark = [&](size_t node) {
        // Up to the first ancestor already marked: everything above it is too.
        while (node < on_map.size() && !on_map[node]) {
            on_map[node] = 1;
            node = tree.nodes[node].parent;
        }
    };
    for (const LiveMarker& marker : live.markers) {
        if (!marker.removed) mark(marker.node);
    }
    for (const LiveTrail& trail : live.trails) {
        if (!trail.removed) mark(trail.node);
    }
    return on_map;
}

void startFadeIns(LiveMap* live, const std::vector<char>& visible, double seconds) {
    const auto start = [&](size_t node, double* in_view_since) {
        if (!nodeVisible(node, visible)) {
            *in_view_since = kNotInView;
        } else if (*in_view_since == kNotInView) {
            *in_view_since = seconds;
        }
    };
    for (LiveMarker& marker : live->markers) start(marker.node, &marker.in_view_since);
    for (LiveTrail& trail : live->trails) start(trail.node, &trail.in_view_since);
}

void setTexture(LiveMarker* marker, size_t pack, std::string path) {
    marker->texture_pack = pack;
    marker->marker.icon = std::move(path);
    marker->image = nullptr;
}

void setTexture(LiveTrail* trail, size_t pack, std::string path) {
    trail->texture_pack = pack;
    trail->trail.texture = std::move(path);
    trail->image = nullptr;
}

ImTextureID packImage(Installed& installed, size_t pack, const std::string& path) {
    InstalledPack& installed_pack = *installed.packs[pack];
    const auto reader = [&installed_pack] {
        return TextureCache::Reader([source = installed_pack.loaded->source](const std::string& key, std::vector<uint8_t>* bytes) {
            std::string error;
            return source && source->read(key, bytes, &error);
        });
    };
    return installed_pack.textures.get(pack::normalizePath(path), reader).texture;
}

size_t nodeOf(const Installed& installed, size_t pack_index, size_t category) {
    const std::vector<size_t>& node_of = installed.packs[pack_index]->node_of;
    return category < node_of.size() ? node_of[category] : pack::kNoIndex;
}

LiveMap liveMap(const Installed& installed, int map_id) {
    LiveMap live;
    live.map_id = map_id;
    live.revision = installed.revision;

    for (size_t p = 0; p < installed.packs.size(); ++p) {
        const InstalledPack& pack = *installed.packs[p];
        if (const auto found = pack.on_map.find(map_id); found != pack.on_map.end()) {
            const BakedPathables& pathables = pack.loaded->pathables;
            for (const size_t i : found->second.markers) {
                const BakedMarker& marker = pathables.markers[i];
                live.markers.push_back({p, nodeOf(installed, p, marker.category), marker, p});
            }
            for (const size_t i : found->second.trails) {
                const BakedTrail& trail = pathables.trails[i];
                live.trails.push_back({p, nodeOf(installed, p, trail.category), trail, p});
            }
        }
    }
    return live;
}

} // namespace viewer::runtime
