#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "pack/model.h"
#include "pack/trl.h"

namespace trail_edit {

// The trail open in the editor: a working copy of its `.trl`, held by editor::State and read and
// written only through trail_edit.h.
struct OpenTrail {
    size_t pathable = pack::kNoIndex;
    std::string path; // the traildata path, normalized
    pack::trl::Trail trail;

    // Flat index -> (segment, point within it). Rebuilt whenever the shape changes, so the
    // window and the gizmo can address a point by one number.
    std::vector<std::pair<size_t, size_t>> flat;
};

} // namespace trail_edit
