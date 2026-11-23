#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// What Blish HUD's Pathing has, brought into our own folder once rather than read where it is:
// its packs, the categories its player turned on or off, what scripts stored, and the markers a
// behavior hid until a time. Drawing from Blish HUD's folder would draw every marker twice while
// Blish HUD runs, and would follow its packs around.
namespace viewer::blish_import {

// What Pathing keeps beside the packs, under `<markers>/data` (Utility/DataDirUtil.cs).
struct Found {
    std::vector<std::filesystem::path> packs; // every .zip and .taco, at any depth

    // The categories turned on or off by hand, by id, lowercase: category_preferences.txt, or the
    // two files Pathing wrote before it (State/CategoryStates.cs).
    std::vector<std::pair<std::string, bool>> categories;

    // Scripts' Storage, kv.json: one flat object of strings (State/KvStates.cs).
    std::vector<std::pair<std::string, std::string>> storage;

    // timers.txt, which our own timers file is the same format as; empty when there is none.
    std::filesystem::path timers;

    bool empty() const { return packs.empty() && categories.empty() && storage.empty() && timers.empty(); }
};

// Reads `markers`, Blish HUD's markers folder. A folder that is not there finds nothing.
Found find(const std::filesystem::path& markers);

// Pathing's category choices from its states folder: "+id" on and "-id" off, one a line, from
// category_preferences.txt; failing that, categories.txt lists what is off and
// invcategories.txt what is on.
std::vector<std::pair<std::string, bool>> readCategoryChoices(const std::filesystem::path& states_folder);

struct Copied {
    size_t copied = 0;
    size_t kept = 0; // ours was as new as Blish HUD's, or newer
    std::vector<std::string> errors; // one a pack that could not be copied
};

// Copies each of `packs` into `folder`, by file name, unless ours is at least as new. Each goes
// by way of `<name>.part`, so a copy cut short never passes for a pack. Slow: a worker's job.
Copied copyPacks(const std::vector<std::filesystem::path>& packs, const std::filesystem::path& folder);

} // namespace viewer::blish_import
