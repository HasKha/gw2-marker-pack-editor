#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// The Packs window: every pack the viewer draws, where it comes from, why one did not load, and
// with editing on, Edit.
namespace viewer::ui::pack_list {

struct Row {
    std::string name; // its file or folder name
    std::string where; // the folder it is in, as the player knows it
    std::string error; // why it did not load; empty when it did
    bool personal_copy = false; // in the edits folder: edited in place, and can be deleted
    bool in_editor = false; // open in the editor, which draws it now
};

struct Action {
    enum class Kind { Edit, Delete };
    Kind kind = Kind::Edit;
    size_t row = 0;
};

// Draws the window while `*open`. `can_edit` shows the Edit buttons; `status` is a line above
// the list: what Edit did last, or is doing. What was asked for this frame, if anything.
std::optional<Action> draw(const std::vector<Row>& rows, bool can_edit, const std::string& status, bool* open);

} // namespace viewer::ui::pack_list
