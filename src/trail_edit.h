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

// True when `index` is the first point of a segment after the first. The list draws a
// break there: a `.trl` is one or more runs of points, not one polyline.
bool startsSegment(size_t index);

// Opens the trail data of `pathable`, or keeps the current one when it is already open.
// Returns false while the `.trl` has not been read yet — reads are budgeted per frame, so
// the caller should simply try again next frame — and when there is nothing to open.
bool open(editor::State& state, size_t pathable);
void close();

void setMapId(editor::State& state, int32_t map_id);
void setPoint(editor::State& state, size_t index, const glm::vec3& position);

// Indices in any order; duplicates are fine. Segments left empty are dropped.
void erasePoints(editor::State& state, std::vector<size_t> indices);

// Appends an arc as a new segment, in the XZ plane at `centre.y`, the way
// gw2-markers/scripts/shared.py generates one: x + cos(angle) * radius,
// z + sin(angle) * radius, both ends included.
void addArc(editor::State& state, const glm::vec3& centre, float radius, int segments, float angle_start, float angle_end);

// Throws away the edits and reads the `.trl` back out of the pack.
bool reload(editor::State& state);

// Writes the working copy to a `.trl` outside the pack.
bool exportToFile(const std::filesystem::path& file, std::string* error);

} // namespace trail_edit
