#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "http/background.h"
#include "util/job_queue.h"
#include "viewer/packs/catalog.h"
#include "viewer/packs/downloads.h"

namespace viewer::catalog {

// The public catalog as a session uses it: its list, asked for at load and again on request;
// the downloads into our folder; the per-pack Keep updated choices; and whether this
// session's automatic updates have been started.
class Library
{
public:
    // Reads the Keep updated choices from `state_file`, written again on `jobs` whenever one
    // changes, and asks for the list.
    void open(std::filesystem::path state_file, util::JobQueue* jobs);
    // Stops the downloads and the request for the list, waiting for both.
    void stop();

    // Asks for the list again, dropping the answer it had.
    void requestList();
    // The list once the catalog answered with one; null until then.
    const std::vector<Package>* packages() const { return request_ ? request_->get() : nullptr; }
    // The catalog answered, with nothing that is a list.
    bool failed() const { return !packages() && request_ && request_->finished(); }

    // Pathing's "Keep Updated": once a session, as soon as the list is in, each pack of `local`
    // older than the catalog's is downloaded again into `folder`.
    void updateStale(const std::vector<LocalPack>& local, const std::filesystem::path& folder);

    bool keepUpdated(const std::string& package) const { return auto_update_.enabled(package); }
    void setKeepUpdated(const std::string& package, bool on);

    Downloads& downloads() { return downloads_; }
    const Downloads& downloads() const { return downloads_; }

private:
    std::filesystem::path state_file_;
    util::JobQueue* jobs_ = nullptr;
    std::unique_ptr<http::Background<std::vector<Package>>> request_;
    Downloads downloads_;
    AutoUpdate auto_update_;
    bool updated_stale_ = false;
};

} // namespace viewer::catalog
