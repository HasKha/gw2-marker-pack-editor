#pragma once

#include <nfd.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace file_dialog {

struct Filter {
    std::string name; // shown in the dialog, e.g. "Marker pack"
    std::string spec; // comma-separated extensions without dots, e.g. "zip,taco"
};

// The native file dialogs, one at a time. Dropping it waits for the one still open.
class FileDialog
{
public:
    FileDialog() = default;
    ~FileDialog();

    FileDialog(const FileDialog&) = delete;
    FileDialog& operator=(const FileDialog&) = delete;

    // All three requests run the native dialog on a worker thread and return immediately.
    // The render thread must never block inside the Present callback, and the Windows
    // IFileDialog pumps its own message loop, so it cannot run there.
    //
    // Only one request may be in flight; further requests are ignored while one is pending.
    // NFD does not expose a dialog title on Windows, so there is nothing to pass for one.
    //
    // `default_folder` has to be a folder that exists. NFD ignores one that does not, and
    // Windows then reopens wherever it last was — a location shared with every other addon,
    // and possibly an Explorer search result rather than a folder. A folder that does exist
    // wins, but only because of NFD_OVERRIDE_RECENT_WITH_DEFAULT; see cmake/nfd.cmake.
    void requestOpenFile(const std::filesystem::path& default_folder, std::vector<Filter> filters);
    void requestOpenFolder(const std::filesystem::path& default_folder);
    void requestSaveFile(const std::filesystem::path& default_folder, std::string_view default_name, std::vector<Filter> filters);

    bool isPending() const { return pending_.load(); }

    // Poll once per frame from the render thread. Yields the picked path exactly once;
    // empty when the user cancelled, errored, or nothing is ready yet.
    std::optional<std::filesystem::path> takeResult();

private:
    // The dialog call itself, already bound to its arguments. Returns the picked path in
    // out_path (caller frees) or a non-OK result.
    using DialogFn = std::function<nfdresult_t(nfdu8char_t**)>;

    void spawn(DialogFn fn);

    // Guards result_ and worker_. pending_ is separately atomic so isPending() never blocks
    // the render thread behind a dialog that is being torn down.
    std::mutex mutex_;
    std::thread worker_;
    std::optional<std::filesystem::path> result_;
    std::atomic<bool> pending_{false};
};

} // namespace file_dialog
