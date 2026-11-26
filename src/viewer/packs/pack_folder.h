#pragma once

#include <Nexus.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <string>
#include <utility>
#include <vector>

#include "util/job_queue.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/catalog.h"
#include "viewer/packs/downloads.h"
#include "viewer/packs/registry.h"

namespace viewer {

// Our packs folder, and the installed packs kept in line with it: every pack in it loaded at
// load, a pack read again once something writes it, downloads put in place, packs deleted, and
// the folder listed again whenever the packs on disk change. After open() the disk is read and
// written only on the worker.
class PackFolder
{
public:
    // Lists `folder`, creating it, and starts loading every pack in it. Blish HUD's packs are
    // drawn once imported into ours, never from its folder. An empty path finds nothing.
    void open(std::filesystem::path folder, util::JobQueue* jobs, AddonAPI* api);

    // Stops the loader and drops what the worker has yet to answer.
    void stop();

    // Once a frame: installs what the loader has read, reads again the packs written since,
    // puts finished downloads in place, and applies a new listing of the folder.
    void update(runtime::Installed* installed, catalog::Downloads* downloads);

    // The packs on disk changed, by the catalog's hand or ours: the folder is listed again once
    // the loader is idle.
    void rescan();

    // Drops every installed pack and reads the folder again.
    void reload(runtime::Installed* installed);

    // Deletes one of our packs from disk, on the worker, then lists the packs again: the list is
    // made after the delete, the worker running both in order.
    void remove(runtime::Installed* installed, const std::filesystem::path& path);

    const std::filesystem::path& folder() const { return folder_; }
    bool loading() const { return loader_.busy(); }
    // Every pack the last listing found, loaded or not.
    size_t found() const { return discovered_.size(); }
    // The packs on disk as the catalog sees them.
    const std::vector<catalog::LocalPack>& local() const { return local_; }
    // How many packs are installed, or how loading goes.
    std::string status(const runtime::Installed& installed) const;

    // Every pack in `folder`, and each as the catalog sees them. Reads the disk.
    struct Listing {
        std::vector<std::filesystem::path> packs;
        std::vector<catalog::LocalPack> local;
    };
    static Listing list(const std::filesystem::path& folder);

private:
    // A pack as it was read: a check on the worker hands back the ones written since.
    struct ReadAt {
        std::filesystem::path path;
        std::filesystem::file_time_type written;
    };
    using Failed = std::vector<std::pair<catalog::Downloads::Finished, std::string>>;

    void installLoaded(runtime::Installed* installed);
    void reloadWritten(runtime::Installed* installed);
    void placeDownloads(runtime::Installed* installed, catalog::Downloads* downloads);
    void applyListing(runtime::Installed* installed);
    bool isDiscovered(const std::filesystem::path& path) const;

    std::filesystem::path folder_;
    util::JobQueue* jobs_ = nullptr;
    AddonAPI* api_ = nullptr;

    registry::Loader loader_;
    // Every path the last listing found, so a pack the editor lets go of is read again only if it
    // was one of ours to begin with.
    std::vector<std::filesystem::path> discovered_;
    std::vector<catalog::LocalPack> local_;

    // A listing is asked for once the loader is done, since the pack it reads may be about to go.
    bool rescan_pending_ = false;
    std::future<Listing> listing_;
    // When packs were last checked for a write since they were read, and the check while it runs.
    std::chrono::steady_clock::time_point last_change_check_{};
    std::future<std::vector<ReadAt>> change_check_;
    // Downloads being moved into place on the worker: those that could not be, and why.
    std::future<Failed> placing_;
};

} // namespace viewer
