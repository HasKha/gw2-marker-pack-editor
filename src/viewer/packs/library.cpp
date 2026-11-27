#include "viewer/packs/library.h"

#include <chrono>
#include <optional>
#include <utility>

#include "http/http.h"

namespace viewer::catalog {

void Library::open(std::filesystem::path choices_file, util::JobQueue* queue) {
    state_file = std::move(choices_file);
    jobs = queue;
    auto_update.load(state_file);
    requestList();
}

void Library::stop() {
    downloads.stop();
    request.reset();
}

void Library::requestList() {
    request = std::make_unique<http::Background<std::vector<Package>>>();
    request->start([](const http::Cancel& cancel) -> std::optional<std::vector<Package>> {
        const std::optional<std::string> body = http::get(REPO_URL, cancel, std::chrono::seconds(15));
        return body ? parse(*body) : std::nullopt;
    });
}

void Library::updateStale(const std::vector<LocalPack>& local, const std::filesystem::path& folder) {
    const std::vector<Package>* list = packages();
    if (updated_stale || !list) return;
    updated_stale = true;
    for (const Package& package : *list) {
        const LocalPack* copy = findLocal(package, local);
        if (copy && state(package, copy) == State::Stale && auto_update.enabled(package.name)) downloads.start(package, folder);
    }
}

void Library::setKeepUpdated(const std::string& package, bool on) {
    auto_update.set(package, on);
    // Not saved, the choice holds until the game closes; nothing on the worker has a way to say so.
    jobs->post([file = state_file, choices = auto_update] { static_cast<void>(choices.save(file)); });
}

} // namespace viewer::catalog
