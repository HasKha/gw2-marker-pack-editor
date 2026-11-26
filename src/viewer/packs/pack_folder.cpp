#include "viewer/packs/pack_folder.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <system_error>

#include "util/path_text.h"
#include "viewer/viewer.h"

namespace viewer {

PackFolder::Listing PackFolder::list(const std::filesystem::path& folder) {
    Listing found;
    if (!folder.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        found.packs = registry::discoverPacks(folder);
    }
    found.local = catalog::localPacks(found.packs);
    return found;
}

void PackFolder::open(std::filesystem::path folder, util::JobQueue* jobs, AddonAPI* api) {
    folder_ = std::move(folder);
    jobs_ = jobs;
    api_ = api;
    Listing found = list(folder_);
    discovered_ = std::move(found.packs);
    local_ = std::move(found.local);
    loader_.add(discovered_);
}

void PackFolder::stop() {
    loader_.stop();
    listing_ = {};
    change_check_ = {};
    placing_ = {};
}

void PackFolder::update(runtime::Installed* installed, catalog::Downloads* downloads) {
    installLoaded(installed);
    reloadWritten(installed);
    placeDownloads(installed, downloads);
    applyListing(installed);
}

void PackFolder::rescan() {
    rescan_pending_ = true;
}

void PackFolder::reload(runtime::Installed* installed) {
    loader_.stop();
    runtime::clear(installed);
    rescan();
}

void PackFolder::remove(runtime::Installed* installed, const std::filesystem::path& path) {
    runtime::uninstall(installed, path);
    jobs_->post([path, nexus = api_] {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) nexus->Log(ELogLevel_WARNING, ADDON_NAME, ("could not delete " + util::pathText(path) + ": " + ec.message()).c_str());
    });
    rescan();
}

std::string PackFolder::status(const runtime::Installed& installed) const {
    if (!loader_.busy()) return std::to_string(installed.packs.size()) + " packs";
    return "Loading packs: " + std::to_string(installed.packs.size()) + " of " + std::to_string(discovered_.size());
}

// Takes what the loader has finished. A pack read again replaces its copy only now, so it is
// drawn all the while.
void PackFolder::installLoaded(runtime::Installed* installed) {
    while (std::unique_ptr<registry::LoadedPack> loaded = loader_.take()) {
        if (!loaded->error.empty()) api_->Log(ELogLevel_WARNING, ADDON_NAME, (util::pathText(loaded->path) + ": " + loaded->error).c_str());
        runtime::uninstall(installed, loaded->path);
        runtime::install(installed, std::move(loaded));
    }
}

// Reads again every pack written since it was read: saved by the editor, or by anything else.
// Looked at on the worker every few seconds, and not while the loader is reading.
void PackFolder::reloadWritten(runtime::Installed* installed) {
    if (util::ready(change_check_)) {
        // Only a pack still installed as the check found it: one removed or read again while
        // it ran is not the pack it looked at.
        std::vector<std::filesystem::path> changed;
        for (const ReadAt& pack : util::take(change_check_).value_or(std::vector<ReadAt>{})) {
            const bool same =
                std::any_of(installed->packs.begin(), installed->packs.end(), [&](const std::unique_ptr<runtime::InstalledPack>& now) {
                    return now->loaded->path == pack.path && now->loaded->written == pack.written;
                });
            if (same) changed.push_back(pack.path);
        }
        if (!changed.empty() && !loader_.busy()) loader_.add(std::move(changed));
    }

    constexpr auto kInterval = std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();
    if (change_check_.valid() || now - last_change_check_ < kInterval || loader_.busy()) return;
    last_change_check_ = now;
    std::vector<ReadAt> read;
    for (const std::unique_ptr<runtime::InstalledPack>& pack : installed->packs)
        read.push_back(ReadAt{pack->loaded->path, pack->loaded->written});
    change_check_ = jobs_->ask([read = std::move(read)] {
        std::vector<ReadAt> changed;
        for (const ReadAt& pack : read) {
            if (registry::writtenAt(pack.path) != pack.written) changed.push_back(pack);
        }
        return changed;
    });
}

// Puts each download that arrived in place of its old copy, on the worker, then lists the packs
// again: the list is made after the moves, the worker running both in order. Not while the
// loader is busy: the pack it is reading may be the one being replaced.
void PackFolder::placeDownloads(runtime::Installed* installed, catalog::Downloads* downloads) {
    if (util::ready(placing_)) {
        for (const auto& [finished, error] : util::take(placing_).value_or(Failed{})) downloads->fail(finished, error);
    }
    if (placing_.valid() || loader_.busy()) return;
    std::vector<catalog::Downloads::Finished> finished = downloads->take();
    if (finished.empty()) return;

    // An installed pack keeps its archive open. Its reads queued on the worker go before the
    // move, and each lets go of the archive as it is done, so it is closed by then.
    for (const catalog::Downloads::Finished& download : finished) runtime::uninstall(installed, download.target);
    placing_ = jobs_->ask([finished = std::move(finished)] {
        Failed failed;
        for (const catalog::Downloads::Finished& download : finished) {
            if (!MoveFileExW(download.part.c_str(), download.target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                std::error_code ec;
                std::filesystem::remove(download.part, ec);
                failed.emplace_back(download, "could not replace " + util::pathText(download.target.filename()));
            }
        }
        return failed;
    });
    rescan();
}

// Brings the installed packs in line with the folder once the worker has listed it: what is gone
// is dropped, what is new is loaded, and nothing else is read again. Listed only while the
// loader is idle, since it may be reading a pack that is about to go. A list made while the loader
// started, or while the packs changed again, is made again: one from before a delete or a
// download put in place would load the very pack being deleted or replaced.
void PackFolder::applyListing(runtime::Installed* installed) {
    if (util::ready(listing_)) {
        std::optional<Listing> found = util::take(listing_);
        if (rescan_pending_ || loader_.busy()) {
            rescan_pending_ = true;
        } else if (found) {
            discovered_ = std::move(found->packs);
            local_ = std::move(found->local);

            std::vector<std::filesystem::path> gone;
            for (const std::unique_ptr<runtime::InstalledPack>& pack : installed->packs) {
                if (!isDiscovered(pack->loaded->path)) gone.push_back(pack->loaded->path);
            }
            for (const std::filesystem::path& path : gone) runtime::uninstall(installed, path);

            std::vector<std::filesystem::path> added;
            for (const std::filesystem::path& path : discovered_) {
                if (!runtime::isInstalled(*installed, path)) added.push_back(path);
            }
            loader_.add(std::move(added));
        }
    }
    if (rescan_pending_ && !listing_.valid() && !loader_.busy()) {
        rescan_pending_ = false;
        listing_ = jobs_->ask([folder = folder_] { return list(folder); });
    }
}

bool PackFolder::isDiscovered(const std::filesystem::path& path) const {
    return std::any_of(discovered_.begin(), discovered_.end(), [&](const std::filesystem::path& known) {
        return registry::samePack(known, path);
    });
}

} // namespace viewer
