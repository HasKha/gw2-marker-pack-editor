#include "viewer/runtime.h"

#include <algorithm>

namespace viewer::runtime {

namespace {

void mergeInto(Installed* installed, InstalledPack* pack) {
    if (pack->loaded->error.empty()) pack->node_of = registry::mergeCategories(&installed->tree, pack->loaded->categories);
}

} // namespace

void install(Installed* installed, std::unique_ptr<registry::LoadedPack> loaded) {
    const std::string prefix = "MPV" + std::to_string(installed->next_serial++) + "_";
    auto pack = std::make_unique<InstalledPack>(std::move(loaded), prefix);
    mergeInto(installed, pack.get());
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
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        const registry::Category& node = tree.nodes[i];
        const bool parent_visible = node.parent == pack::kNoIndex || visible[node.parent];
        visible[i] = parent_visible && states.enabled(node.id, node.default_toggle);
    }
    return visible;
}

LiveMap liveMap(const Installed& installed, int map_id) {
    LiveMap live;
    live.map_id = map_id;
    live.revision = installed.revision;

    for (size_t p = 0; p < installed.packs.size(); ++p) {
        const BakedPathables& pathables = installed.packs[p]->loaded->pathables;
        for (size_t i = 0; i < pathables.markers.size(); ++i) {
            if (pathables.markers[i].map_id == map_id) live.markers.push_back({p, i});
        }
        for (size_t i = 0; i < pathables.trails.size(); ++i) {
            if (pathables.trails[i].map_id == map_id) live.trails.push_back({p, i});
        }
    }
    return live;
}

} // namespace viewer::runtime
