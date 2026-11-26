#pragma once

#include <string>
#include <vector>

#include "viewer/packs/blish_import.h"

// The wizard that brings Blish HUD's packs and Pathing's choices into our folder: shown on the
// first load when Blish HUD has anything, and from a button in the options.
namespace viewer::ui::import_window {

// Pathing's category choices, script storage and timers always come with the packs.
struct Choices {
    std::vector<char> packs; // per Found::packs, whether to copy it
};

enum class Answer { None, Import, Close };

// Draws the window while `*open`. `status` is how the last import went; while `busy` one runs
// and nothing can be asked. What was asked this frame.
Answer draw(const blish_import::Found& found, Choices* choices, const std::string& status, bool busy, bool* open);

} // namespace viewer::ui::import_window
