#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// HTTP and HTTPS requests from a worker thread, through WinHTTP: what the viewer asks the web
// for — the festivals on today, the maps' rectangles, the pack catalog and its packs. Never
// called from a frame.
namespace viewer::http {

// What we call ourselves, unless a request says otherwise.
constexpr const wchar_t* kUserAgent = L"MarkerPackEditor";

// The body of `url` when it answers 200, or nothing: no network, a timeout, any other status,
// a URL WinHTTP cannot read. Blocks for at most about `timeout` per step of the request.
std::optional<std::string> get(std::string_view url, std::chrono::milliseconds timeout = std::chrono::seconds(5));

// Bytes so far, and the whole size, 0 when the server does not say.
using Progress = std::function<void(uint64_t received, uint64_t total)>;

// Writes the body of `url` to `file` as it arrives, calling `progress` after every read, and
// stops as soon as `cancel` is set. Empty on success, else what went wrong; `file` may then
// hold part of the body, which the caller removes.
std::string download(
    std::string_view url,
    const std::filesystem::path& file,
    const Progress& progress,
    const std::atomic<bool>& cancel,
    const wchar_t* user_agent = kUserAgent);

} // namespace viewer::http
