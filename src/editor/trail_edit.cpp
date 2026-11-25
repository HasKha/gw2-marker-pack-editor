#include "editor/trail_edit.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "editor/pathable_edit.h"
#include "editor/resources.h"
#include "editor/undo.h"
#include "util/path_text.h"

namespace trail_edit {

namespace {

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

size_t open_pathable = pack::kNoIndex;
std::string open_path;
pack::trl::Trail trail;

// Flat index -> (segment, point within it). Rebuilt whenever the shape changes, so the
// window and the gizmo can address a point by one number.
std::vector<std::pair<size_t, size_t>> flat;

// Called on every change to the trail's shape. Pruning the point selection here is what
// lets everything downstream treat a selected point index as one that exists: this is the
// only place that knows how many there are.
void reindex(editor::State& state) {
    flat.clear();
    for (size_t s = 0; s < trail.segments.size(); ++s) {
        for (size_t p = 0; p < trail.segments[s].points.size(); ++p) flat.emplace_back(s, p);
    }
    state.points.prune(flat.size());
}

// Reads the bytes the pack currently holds for the open trail: the pending edit if there is
// one, otherwise the file as it ships.
bool readFromPack(const editor::State& state, pack::trl::Trail* out) {
    std::string error;

    if (const pack::Pack::AddedAsset* pending = state.pack.findAddedAsset(open_path)) {
        return pack::trl::read(pending->bytes, out, &error);
    }

    const pack::DataSource* source = resources::source();
    std::vector<uint8_t> bytes;
    if (!source || !source->read(open_path, &bytes, &error)) return false;
    return pack::trl::read(bytes, out, &error);
}

// Pushes the working copy back into the pack. Nothing else writes trail bytes: an edit is
// only real once it is here, and the world redraws from it on the next frame.
void commit(editor::State& state) {
    if (open_path.empty()) return;

    // Before the write, so the record holds what was there.
    undo::asset(state, open_path);

    state.pack.addAsset(open_path, pack::trl::write(trail));
    // The cache holds the parsed trail the viewer draws; drop it so the next frame reads
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

size_t segmentOf(size_t index) {
    return index < flat.size() ? flat[index].first : 0;
}

size_t segmentCount() {
    return trail.segments.size();
}

bool open(editor::State& state, size_t pathable_index) {
    const std::string data = trailDataOf(state, pathable_index);
    if (data.empty()) {
        close(state);
        return false;
    }
    if (open_pathable == pathable_index && open_path == data) return true;

    // A read still on the worker comes back on a later frame; one that failed is cached as a
    // miss and never will. Holding the previous trail open over that second case is what
    // made two differently named trails look like they shared a point list.
    bool deferred = false;
    const std::shared_ptr<const pack::trl::Trail> parsed = resources::trail(data, &deferred);
    if (!parsed) {
        if (!deferred) close(state);
        return false;
    }

    open_pathable = pathable_index;
    open_path = data;
    trail = *parsed;
    // Before reindex, so a switch to a different trail drops the old trail's points rather
    // than reinterpreting their indices against the new one.
    state.points.openOn(pathable_index);
    reindex(state);
    return true;
}

void close(editor::State& state) {
    open_pathable = pack::kNoIndex;
    open_path.clear();
    trail = pack::trl::Trail{};
    flat.clear();
    state.points.close();
}

void followSelection(editor::State& state) {
    const editor::NodeRef primary = state.selection.primary();

    // The .trl is read on a worker, so the first attempt after picking a trail can
    // legitimately come back empty — which is fine, the next frame tries again.
    if (primary.kind == editor::NodeKind::Pathable && primary.index < state.pack.pathables.size() &&
        state.pack.pathables[primary.index].type == pack::PathableType::Trail) {
        open(state, primary.index);
        return;
    }

    close(state);
}

void setMapId(editor::State& state, int32_t map_id) {
    if (!isOpen() || trail.map_id == map_id) return;

    undo::Step step(state, "Set trail map id");
    trail.map_id = map_id;
    commit(state);

    // The header above is authoritative and is what both Blish and our viewer go by. The
    // `MapID` attribute is not read, but a pack that ships the two disagreeing is a pack
    // saying two different things, so it follows along rather than being left to rot.
    if (open_pathable >= state.pack.pathables.size()) return;
    pack::Pathable& pathable = state.pack.pathables[open_pathable];
    undo::attributes(state, editor::NodeRef{editor::NodeKind::Pathable, open_pathable});
    if (pathable_edit::setMapId(state, &pathable.attributes, pathable.category, map_id)) state.touch();
}

void setPoint(editor::State& state, size_t index, const glm::vec3& position) {
    if (index >= flat.size()) return;

    glm::vec3& target = trail.segments[flat[index].first].points[flat[index].second];
    if (target == position) return;

    // Merged across frames, so dragging a point is one step. A drag driven by the gizmo
    // opens its own step first and this one nests inside it.
    undo::Step step(state, "Move trail point", "trailpoint:" + std::to_string(index));
    target = position;
    commit(state);
}

void erasePoints(editor::State& state, std::vector<size_t> indices) {
    if (!isOpen() || indices.empty()) return;

    undo::Step step(state, "Delete trail points");

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

    reindex(state);
    // Every index after the first deletion has shifted, so pruning is not enough: the
    // survivors would name whatever slid into their place. The trail itself is a different
    // selection and is untouched, so it stays selected and the editor stays open on it.
    state.points.indices.clear();
    commit(state);
}

size_t insertPoint(editor::State& state, size_t after, const glm::vec3& position) {
    if (!isOpen()) return pack::kNoIndex;

    undo::Step step(state, "Add trail point");

    // Into the same run as the point it follows, so inserting in the middle of a trail does
    // not split it. With nothing to follow it goes on the end of the last run.
    size_t segment = trail.segments.empty() ? pack::kNoIndex : trail.segments.size() - 1;
    size_t at = segment == pack::kNoIndex ? 0 : trail.segments[segment].points.size();

    if (after < flat.size()) {
        segment = flat[after].first;
        at = flat[after].second + 1;
    }

    if (segment == pack::kNoIndex) {
        trail.segments.push_back(pack::trl::Segment{});
        segment = trail.segments.size() - 1;
        at = 0;
    }

    std::vector<glm::vec3>& points = trail.segments[segment].points;
    points.insert(points.begin() + static_cast<long long>(at), position);

    reindex(state);
    commit(state);

    // Back to a flat index for the caller, which is what everything outside speaks.
    for (size_t i = 0; i < flat.size(); ++i) {
        if (flat[i].first == segment && flat[i].second == at) return i;
    }
    return pack::kNoIndex;
}

void addArc(editor::State& state, const glm::vec3& centre, float radius, int segments, float angle_start, float angle_end) {
    if (!isOpen() || segments < 1) return;

    undo::Step undo_step(state, "Add circle");

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
    reindex(state);
    commit(state);
}

bool reload(editor::State& state) {
    if (!isOpen()) return false;

    // Read before dropping anything. The source is the only thing a reload can mean, and
    // for a trail created in the editor there is nothing there — its pending bytes are the
    // only copy of it. Dropping those first and *then* failing the read would leave the
    // pack with no trail at all while the working copy still held the points.
    //
    // Straight from the source rather than through the cache: a cached read comes back on a
    // later frame, and a menu action that silently does nothing while it is out is worse than
    // one that costs one extra read.
    const pack::DataSource* source = resources::source();
    std::vector<uint8_t> bytes;
    std::string error;
    if (!source || !source->read(open_path, &bytes, &error)) return false;

    pack::trl::Trail parsed;
    if (!pack::trl::read(bytes, &parsed, &error)) return false;

    undo::Step step(state, "Reload trail");
    undo::asset(state, open_path);

    state.pack.dropAddedAsset(open_path);
    resources::forget(open_path);

    trail = std::move(parsed);
    reindex(state);
    state.touch();
    return true;
}

bool refresh(editor::State& state) {
    if (!isOpen()) return false;

    pack::trl::Trail parsed;
    if (!readFromPack(state, &parsed)) return false;

    trail = std::move(parsed);
    reindex(state);
    resources::forget(open_path);
    return true;
}

bool exportToFile(const std::filesystem::path& file, std::string* error) {
    if (!isOpen()) return false;

    const std::vector<uint8_t> bytes = pack::trl::write(trail);

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (out) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        if (error) *error = "Could not write " + util::pathText(file);
        return false;
    }
    return true;
}

} // namespace trail_edit
