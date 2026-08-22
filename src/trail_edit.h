#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "editor_state.h"
#include "pack/trl.h"

// The trail currently being edited, as a mutable working copy of its `.trl`.
//
// It lives here rather than in the trail editor window because the gizmo edits the same
// points: the window lists them, the gizmo drags them, and both go through this one model.
//
// Every change writes the whole trail straight back into the pack as a pending asset — the
// same mechanism the resources window uses — so the world redraws immediately and a pack
// save picks the trail up with everything else. Points are addressed by a flat index across
// segments, which is the order the file has them in and the order the editor lists them.
namespace trail_edit {

bool isOpen();
size_t pathable(); // pack::kNoIndex when nothing is open
const std::string& path(); // the traildata path, normalized
int32_t mapId();

size_t pointCount();
glm::vec3 point(size_t index);

// Which run of points `index` belongs to, and how many runs there are. A `.trl` is one or
// more runs separated by (0,0,0) terminators, drawn as separate ribbons; the list shows
// the number so a break in the trail is visible rather than implied.
size_t segmentOf(size_t index);
size_t segmentCount();

// Opens the trail data of `pathable`, or keeps the current one when it is already open.
// Returns false while the `.trl` has not been read yet — reads are budgeted per frame, so
// the caller should simply try again next frame — and when there is nothing to open.
bool open(editor::State& state, size_t pathable);
// Takes State because the point selection is scoped to the open trail and goes with it.
void close(editor::State& state);

// Keeps the open trail in step with the node selection, both ways: a trail selected is a
// trail opened, and selecting anything else closes it. Without the second half the handles
// in the world would outlive the reason they were drawn, and the gizmo would stay on points
// of a trail nobody has selected.
//
// Points do not enter into it — they are their own selection, so picking one leaves the
// trail selected and this alone.
//
// Must be called every frame from somewhere that always runs. It cannot live in the trail
// editor window: a window that is hidden, or merely collapsed, draws nothing.
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
bool exportToFile(const std::filesystem::path& file, std::string* error);

} // namespace trail_edit
