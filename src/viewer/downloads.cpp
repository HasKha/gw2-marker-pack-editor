#include "viewer/downloads.h"

#include "pack/source.h"
#include "util/path_text.h"

namespace viewer::catalog {

Downloads::Fetch Downloads::defaultFetch() {
    return [](const std::string& url, const std::filesystem::path& file, const http::Progress& progress, const http::Cancel& cancel) {
        return http::download(url, file, progress, cancel, kDownloadUserAgent);
    };
}

Downloads::Job* Downloads::find(const std::string& file_name) const {
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (job->file_name == file_name) return job.get();
    }
    return nullptr;
}

void Downloads::start(const Package& package, const std::filesystem::path& folder) {
    if (Job* previous = find(package.file_name)) {
        if (!previous->done) return;
        previous->thread.join();
        std::erase_if(jobs_, [previous](const std::unique_ptr<Job>& job) { return job.get() == previous; });
    }

    auto job = std::make_unique<Job>();
    job->file_name = package.file_name;
    job->target = folder / util::pathFromText(package.file_name);
    job->part = folder / util::pathFromText(package.file_name + ".part");
    Job* raw = job.get();
    raw->thread = std::thread([this, raw, url = package.download, folder] {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        const http::Progress progress = [raw](uint64_t received, uint64_t total) {
            raw->received = received;
            raw->total = total;
        };
        std::string error = fetch_(url, raw->part, progress, raw->cancel);
        if (error.empty()) {
            // PackHandlingUtil's test: an archive with something in it. The source is closed
            // again at once, so the file can be moved.
            std::string open_error;
            const std::unique_ptr<pack::DataSource> source = pack::openZip(raw->part, &open_error);
            if (!source || source->entries().empty()) error = "the download is not a marker pack";
        }
        if (!error.empty()) std::filesystem::remove(raw->part, ec);
        raw->error = std::move(error);
        raw->done.store(true, std::memory_order_release);
    });
    jobs_.push_back(std::move(job));
}

std::optional<Downloads::Status> Downloads::status(const std::string& file_name) const {
    const Job* job = find(file_name);
    if (!job) return std::nullopt;
    Status status;
    status.running = !job->done.load(std::memory_order_acquire);
    if (status.running) {
        const uint64_t total = job->total;
        status.progress = total > 0 ? static_cast<float>(static_cast<double>(job->received) / static_cast<double>(total)) : -1.0f;
    } else {
        status.progress = job->error.empty() ? 1.0f : 0.0f;
        status.error = job->error;
    }
    return status;
}

std::vector<Downloads::Finished> Downloads::take() {
    std::vector<Finished> finished;
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (job->taken || !job->done.load(std::memory_order_acquire)) continue;
        job->taken = true;
        if (job->error.empty()) finished.push_back(Finished{job->file_name, job->part, job->target});
    }
    return finished;
}

void Downloads::fail(const Finished& finished, const std::string& error) {
    if (Job* job = find(finished.file_name)) job->error = error;
}

void Downloads::stop() {
    for (const std::unique_ptr<Job>& job : jobs_) job->cancel.cancel();
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (job->thread.joinable()) job->thread.join();
    }
    // What arrived but was never put in place goes with them.
    std::error_code ec;
    for (const std::unique_ptr<Job>& job : jobs_) {
        if (!job->taken) std::filesystem::remove(job->part, ec);
    }
    jobs_.clear();
}

} // namespace viewer::catalog
