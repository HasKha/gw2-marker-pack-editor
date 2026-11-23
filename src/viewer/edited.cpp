#include "viewer/edited.h"

namespace viewer::edited {

namespace {

bool shown(const std::vector<char>& shown, size_t pathable) {
    return pathable >= shown.size() || shown[pathable];
}

} // namespace

bool EditedLive::update(const Report& report, int map_id, const TrailLookup& trail) {
    if (!report.pack) {
        const bool was_open = open_;
        clear();
        return was_open;
    }

    bool changed = false;
    if (!open_ || report.revision != revision_) {
        baked_ = bake(*report.pack);
        open_ = true;
        revision_ = report.revision;
        changed = true;
    }
    if (changed || unresolved_) changed |= resolveTrails(trail);

    static const std::vector<char> kShowAll;
    const std::vector<char>& now_shown = report.shown ? *report.shown : kShowAll;
    if (now_shown != shown_) {
        shown_ = now_shown;
        changed = true;
    }

    if (!changed && live_.map_id == map_id) return false;
    rebuild(map_id);
    return true;
}

void EditedLive::clear() {
    open_ = false;
    revision_ = 0;
    baked_ = BakedPathables{};
    shown_.clear();
    unresolved_ = false;
    live_ = runtime::LiveMap{};
}

bool EditedLive::resolveTrails(const TrailLookup& trail) {
    unresolved_ = false;
    bool arrived = false;
    for (BakedTrail& baked : baked_.trails) {
        if (baked.geometry) continue;
        bool deferred = false;
        std::shared_ptr<const pack::trl::Trail> geometry = trail ? trail(baked.data, &deferred) : nullptr;
        if (geometry) {
            // The file's header decides the map, not the XML — see BakedTrail::map_id.
            baked.map_id = geometry->map_id;
            baked.geometry = std::move(geometry);
            arrived = true;
        } else if (deferred) {
            unresolved_ = true;
        }
    }
    return arrived;
}

void EditedLive::rebuild(int map_id) {
    live_ = runtime::LiveMap{};
    live_.map_id = map_id;
    for (const BakedMarker& marker : baked_.markers) {
        if (marker.map_id == map_id && shown(shown_, marker.pathable)) live_.markers.push_back({0, pack::kNoIndex, marker, 0});
    }
    for (const BakedTrail& baked : baked_.trails) {
        if (baked.map_id == map_id && shown(shown_, baked.pathable)) live_.trails.push_back({0, pack::kNoIndex, baked, 0});
    }
}

} // namespace viewer::edited
