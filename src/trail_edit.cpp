#include "trail_edit.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "resources.h"

namespace trail_edit {

namespace {

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

size_t open_pathable = pack::kNoIndex;
std::string open_path;
pack::trl::Trail trail;

// Flat index -> (segment, point within it). Rebuilt whenever the shape changes, so the
// window and the gizmo can address a point by one number.
std::vector<std::pair<size_t, size_t>> flat;

void reindex() {
    flat.clear();
    for (size_t s = 0; s < trail.segments.size(); ++s) {
        for (size_t p = 0; p < trail.segments[s].points.size(); ++p) flat.emplace_back(s, p);
    }
}

// Pushes the working copy back into the pack. Nothing else writes trail bytes: an edit is
// only real once it is here, and the world redraws from it on the next frame.
void commit(editor::State& state) {
    if (open_path.empty()) return;

    state.pack.addAsset(open_path, pack::trl::write(trail));
    // The cache holds the parsed trail the renderer draws; drop it so the next frame reads
    // the bytes we just wrote.
    resources::forget(open_path);
    state.touch();
}

// The `traildata` the pathable resolves to, or empty when it has none.
std::string trailDataOf(const editor::State& state, size_t pathable_index) {
    if (pathable_index >= state.pack.pathables.size()) return {};

    const pack::Pathable& pathable = state.pack.pathables[pathable_index];
    if (pathable.type != pack::PathableType::Trail) return {};

    const std::string* data = state.pack.effectiveValue(pathable, "traildata");
    return data ? pack::normalizePath(*data) : std::string();
}

} // namespace

bool isOpen() {
    return open_pathable != pack::kNoIndex && !open_path.empty();
}

size_t pathable() {
    return open_pathable;
}

const std::string& path() {
    return open_path;
}

int32_t mapId() {
    return trail.map_id;
}

size_t pointCount() {
    return flat.size();
}

glm::vec3 point(size_t index) {
    if (index >= flat.size()) return glm::vec3(0.0f);
    return trail.segments[flat[index].first].points[flat[index].second];
}

bool startsSegment(size_t index) {
    return index < flat.size() && flat[index].first > 0 && flat[index].second == 0;
}

bool open(editor::State& state, size_t pathable_index) {
    const std::string data = trailDataOf(state, pathable_index);
    if (data.empty()) return false;
    if (open_pathable == pathable_index && open_path == data) return true;

    // Budgeted, so this can legitimately come back empty for a frame or two.
    const pack::trl::Trail* parsed = resources::trail(data);
    if (!parsed) return false;

    open_pathable = pathable_index;
    open_path = data;
    trail = *parsed;
    reindex();
    return true;
}

void close() {
    open_pathable = pack::kNoIndex;
    open_path.clear();
    trail = pack::trl::Trail{};
    flat.clear();
}

void setMapId(editor::State& state, int32_t map_id) {
    if (!isOpen() || trail.map_id == map_id) return;
    trail.map_id = map_id;
    commit(state);
}

void setPoint(editor::State& state, size_t index, const glm::vec3& position) {
    if (index >= flat.size()) return;

    glm::vec3& target = trail.segments[flat[index].first].points[flat[index].second];
    if (target == position) return;

    target = position;
    commit(state);
}

void erasePoints(editor::State& state, std::vector<size_t> indices) {
    if (!isOpen() || indices.empty()) return;

    // Back to front, so an earlier erase cannot shift a later index out from under us.
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        if (*it >= flat.size()) continue;
        std::vector<glm::vec3>& points = trail.segments[flat[*it].first].points;
        points.erase(points.begin() + static_cast<long long>(flat[*it].second));
    }

    // A segment with nothing left in it would write out as a stray terminator.
    trail.segments.erase(
        std::remove_if(
            trail.segments.begin(),
            trail.segments.end(),
            [](const pack::trl::Segment& segment) { return segment.points.empty(); }),
        trail.segments.end());

    reindex();
    commit(state);
}

void addArc(editor::State& state, const glm::vec3& centre, float radius, int segments, float angle_start, float angle_end) {
    if (!isOpen() || segments < 1) return;

    // Both ends included, like make_arc's inclusive range: a 0-360 arc closes on its own
    // first point, which is what the ribbon needs to look continuous.
    pack::trl::Segment arc;
    arc.points.reserve(static_cast<size_t>(segments) + 1);

    const float step = (angle_end - angle_start) / static_cast<float>(segments);
    for (int i = 0; i <= segments; ++i) {
        const float radians = (angle_start + step * static_cast<float>(i)) * kDegreesToRadians;
        arc.points.push_back(glm::vec3(centre.x + std::cos(radians) * radius, centre.y, centre.z + std::sin(radians) * radius));
    }

    trail.segments.push_back(std::move(arc));
    reindex();
    commit(state);
}

bool reload(editor::State& state) {
    if (!isOpen()) return false;

    // Drop our pending bytes first, or the reread would just hand them back.
    state.pack.dropAddedAsset(open_path);
    resources::forget(open_path);

    // Straight from the source rather than through the cache: cached reads are budgeted a
    // few per frame, and a menu action that silently does nothing when the budget happens
    // to be spent is worse than one that costs one extra read.
    const pack::DataSource* source = resources::source();
    std::vector<uint8_t> bytes;
    std::string error;
    if (!source || !source->read(open_path, &bytes, &error)) return false;

    pack::trl::Trail parsed;
    if (!pack::trl::read(bytes, &parsed, &error)) return false;

    trail = std::move(parsed);
    reindex();
    state.touch();
    return true;
}

bool exportToFile(const std::filesystem::path& file, std::string* error) {
    if (!isOpen()) return false;

    const std::vector<uint8_t> bytes = pack::trl::write(trail);

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        if (error) *error = "Could not write " + file.string();
        return false;
    }
    return true;
}

} // namespace trail_edit
