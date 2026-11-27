#pragma once

#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "http/background.h"
#include "util/job_queue.h"
#include "viewer/behaviors/states.h"

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

// The import as a session runs it: what Pathing has, read on the worker before the window opens;
// the copy of the chosen packs on a thread of its own, which reads Pathing's timers too so that
// the frame only has to bring its states in; and how the last one went.
class Importer
{
public:
    struct Done {
        Copied copied;
        Found states; // what of Pathing's states was chosen
        behaviors::BehaviorStates timers; // states.timers, read
    };

    // Reads `markers`, Blish HUD's markers folder, now: at load, before there is a frame to stall.
    void find(const std::filesystem::path& markers);
    // Reads it again on `jobs`, unless a read is already on its way.
    void findOnWorker(const std::filesystem::path& markers, util::JobQueue* jobs);
    // Whether the read findOnWorker asked for came in, once: found() is then its, and the status
    // is cleared.
    bool takeFound();
    const Found& found() const { return last_found; }

    // Copies `packs` into `folder` and reads the states found() has.
    void start(std::vector<std::filesystem::path> packs, const std::filesystem::path& folder);
    bool running() const { return job != nullptr; }
    // What the copy did, once it is done, for the caller to bring its states in. The status says
    // how it went, and that it did not finish when it gave no answer.
    std::optional<Done> finish();
    const std::string& status() const { return status_text; }

    // Waits for the copy.
    void stop() { job.reset(); }

private:
    std::future<Found> search;
    Found last_found;
    std::unique_ptr<http::Background<Done>> job;
    std::string status_text;
};

} // namespace viewer::blish_import
