#include "viewer/packs/library.h"

#include <chrono>
#include <optional>
#include <utility>

#include "http/http.h"

namespace viewer::catalog {

void Library::open(std::filesystem::path state_file, util::JobQueue* jobs) {
    state_file_ = std::move(state_file);
    jobs_ = jobs;
    auto_update_.load(state_file_);
    requestList();
}

void Library::stop() {
    downloads_.stop();
    request_.reset();
}

void Library::requestList() {
    request_ = std::make_unique<http::Background<std::vector<Package>>>();
    request_->start([](const http::Cancel& cancel) -> std::optional<std::vector<Package>> {
        const std::optional<std::string> body = http::get(REPO_URL, cancel, std::chrono::seconds(15));
        return body ? parse(*body) : std::nullopt;
    });
}

void Library::updateStale(const std::vector<LocalPack>& local, const std::filesystem::path& folder) {
    const std::vector<Package>* list = packages();
    if (updated_stale_ || !list) return;
    updated_stale_ = true;
    for (const Package& package : *list) {
        const LocalPack* copy = findLocal(package, local);
        if (copy && state(package, copy) == State::Stale && auto_update_.enabled(package.name)) downloads_.start(package, folder);
    }
}

void Library::setKeepUpdated(const std::string& package, bool on) {
    auto_update_.set(package, on);
    // Not saved, the choice holds until the game closes; nothing on the worker has a way to say so.
    jobs_->post([file = state_file_, choices = auto_update_] { static_cast<void>(choices.save(file)); });
}

} // namespace viewer::catalog
