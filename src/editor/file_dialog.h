#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace file_dialog {

struct Filter {
    std::string name; // shown in the dialog, e.g. "Marker pack"
    std::string spec; // comma-separated extensions without dots, e.g. "zip,taco"
};

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

bool isPending();

// Poll once per frame from the render thread. Yields the picked path exactly once;
// empty when the user cancelled, errored, or nothing is ready yet.
std::optional<std::filesystem::path> takeResult();

// Joins the worker. Safe to call with nothing in flight.
void shutdown();

} // namespace file_dialog
