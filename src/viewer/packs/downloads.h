#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "http/http.h"
#include "viewer/packs/catalog.h"

// Catalog packs downloading into our folder, each on a thread of its own: PackHandlingUtil's
// download, up to the point where the new archive is ready to take the old one's place. That
// last step is the frame's, since the old one may be open and drawing.
namespace viewer::catalog {

class Downloads
{
public:
    // Writes the body of `url` to `file`: http::download, unless a test says otherwise.
    using Fetch = std::function<
        std::string(const std::string& url, const std::filesystem::path& file, const http::Progress& progress, const http::Cancel& cancel)>;

    explicit Downloads(Fetch fetch = defaultFetch())
        : fetch_(std::move(fetch)) {}
    ~Downloads() { stop(); }
    Downloads(const Downloads&) = delete;
    Downloads& operator=(const Downloads&) = delete;

    static Fetch defaultFetch();

    // Downloads `package` to `<folder>/<file name>.part`, unless it is downloading already.
    void start(const Package& package, const std::filesystem::path& folder);

    struct Status {
        bool running = false;
        float progress = -1.0f; // 0 to 1; negative while the size is unknown
        std::string error; // why the last attempt failed; empty when it did not
    };
    // How the package saved as `file_name` is doing; nothing when it has not been asked for.
    std::optional<Status> status(const std::string& file_name) const;

    // A download that arrived and is an archive: `part` is ready to replace `target`.
    struct Finished {
        std::string file_name;
        std::filesystem::path part;
        std::filesystem::path target;
    };
    // What finished since the last call. Each is handed over once.
    std::vector<Finished> take();

    // Says why a finished download could not be put in place. Removing what it downloaded is
    // the caller's, who was moving it on a worker.
    void fail(const Finished& finished, const std::string& error);

    // Cancels every download and waits for each.
    void stop();

private:
    struct Job {
        std::string file_name;
        std::filesystem::path part;
        std::filesystem::path target;
        std::thread thread;
        http::Cancel cancel;
        std::atomic<uint64_t> received{0};
        std::atomic<uint64_t> total{0};
        std::atomic<bool> done{false};
        std::string error; // written by the worker before `done`, by the frame after it
        bool taken = false;
    };

    Job* find(const std::string& file_name) const;

    Fetch fetch_;
    std::vector<std::unique_ptr<Job>> jobs_;
};

} // namespace viewer::catalog
