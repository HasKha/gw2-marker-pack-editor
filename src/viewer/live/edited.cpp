#include "viewer/live/edited.h"

#include <unordered_map>

namespace viewer::edited {

namespace {

bool shown(const std::vector<char>& shown, size_t pathable) {
    return pathable >= shown.size() || shown[pathable];
}

} // namespace

bool EditedLive::update(const Report& report, int map_id, const TrailLookup& trail, const ColourLookup& colour) {
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
    if (changed || unresolved_) changed |= resolveTrails(trail, colour);

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

bool EditedLive::resolveTrails(const TrailLookup& trail, const ColourLookup& colour) {
    unresolved_ = false;
    bool arrived = false;
    for (BakedTrail& baked : baked_.trails) {
        if (baked.sample_pending && colour) {
            bool deferred = false;
            if (const std::optional<ImU32> sampled = colour(baked.texture, &deferred)) {
                baked.sample_colour = *sampled;
                baked.sample_pending = false;
                arrived = true;
            } else if (deferred) {
                unresolved_ = true;
            }
        }

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
    // An edit rebuilds the whole map, a gizmo drag every frame: what was already in view keeps
    // the time it came into view, by its pathable, whose index no edit moves. Only what is new
    // to the map, or shown again in the tree, fades in.
    std::unordered_map<size_t, double> since;
    if (live_.map_id == map_id) {
        for (const runtime::LiveMarker& marker : live_.markers) since[marker.marker.pathable] = marker.in_view_since;
        for (const runtime::LiveTrail& trail : live_.trails) since[trail.trail.pathable] = trail.in_view_since;
    }
    const auto sinceOf = [&](size_t pathable) {
        const auto found = since.find(pathable);
        return found != since.end() ? found->second : runtime::kNotInView;
    };

    live_ = runtime::LiveMap{};
    live_.map_id = map_id;
    live_.revision = ++builds_;
    for (const BakedMarker& marker : baked_.markers) {
        if (marker.map_id == map_id && shown(shown_, marker.pathable)) {
            live_.markers.push_back({0, pack::kNoIndex, marker, 0});
            live_.markers.back().in_view_since = sinceOf(marker.pathable);
        }
    }
    for (const BakedTrail& baked : baked_.trails) {
        if (baked.map_id == map_id && shown(shown_, baked.pathable)) {
            live_.trails.push_back({0, pack::kNoIndex, baked, 0});
            live_.trails.back().in_view_since = sinceOf(baked.pathable);
        }
    }
}

} // namespace viewer::edited
