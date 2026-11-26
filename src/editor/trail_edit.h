#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "editor/editor_state.h"
#include "pack/trl.h"

// The open trail, State::open_trail, as a working copy of its `.trl` shared by the trail editor,
// the gizmo and the overlay. Every change writes the whole trail back as a pending asset. Points
// are addressed by a flat index across segments.
namespace trail_edit {

bool isOpen(const editor::State& state);
size_t pathable(const editor::State& state); // pack::kNoIndex when nothing is open
const std::string& path(const editor::State& state); // the traildata path, normalized
int32_t mapId(const editor::State& state);

size_t pointCount(const editor::State& state);
glm::vec3 point(const editor::State& state, size_t index);

// Which run of points `index` belongs to, and how many runs there are. A `.trl` is one or
// more runs separated by (0,0,0) terminators, drawn as separate ribbons; the list shows
// the number so a break in the trail is visible rather than implied.
size_t segmentOf(const editor::State& state, size_t index);
size_t segmentCount(const editor::State& state);

// Opens the trail data of `pathable`, or keeps the current one when it is already open.
// Returns false while the `.trl` is being read on the worker — the caller should simply try
// again next frame — and when there is nothing to open.
bool open(editor::State& state, size_t pathable);
// Takes State because the point selection is scoped to the open trail and goes with it.
void close(editor::State& state);

// Keeps the open trail in step with the node selection: a trail selected is opened, anything else
// selected closes it. Call every frame from outside the trail editor's window body, which does not
// run while hidden or collapsed.
void followSelection(editor::State& state);

void setMapId(editor::State& state, int32_t map_id);
void setPoint(editor::State& state, size_t index, const glm::vec3& position);

// Indices in any order; duplicates are fine. Segments left empty are dropped.
void erasePoints(editor::State& state, std::vector<size_t> indices);

// Puts a point at `position` straight after `after`, or at the end of the trail when
// `after` is kNoIndex. Returns the new point's flat index, or kNoIndex if nothing was
// added.
size_t insertPoint(editor::State& state, size_t after, const glm::vec3& position);

// Appends an arc as a new segment, in the XZ plane at `centre.y`, the way
// gw2-markers/scripts/shared.py generates one: x + cos(angle) * radius,
// z + sin(angle) * radius, both ends included.
void addArc(editor::State& state, const glm::vec3& centre, float radius, int segments, float angle_start, float angle_end);

// Throws away the edits and reads the `.trl` back out of the pack.
bool reload(editor::State& state);

// Re-reads the working copy from whatever the pack now holds, keeping the pending edit if
// there is one. For undo, which puts different bytes behind the path without coming
// through here.
bool refresh(editor::State& state);

// Writes the working copy to a `.trl` outside the pack.
bool exportToFile(const editor::State& state, const std::filesystem::path& file, std::string* error);

} // namespace trail_edit
