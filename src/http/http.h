#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

// HTTP and HTTPS requests from a worker thread, through WinHTTP: the festivals on today, the
// maps' rectangles, the pack catalog and its packs. Every call blocks, so none is made from a
// frame. Knows nothing of the viewer, the packs or the game; only util/ is below it.
namespace http {

// What we call ourselves, unless a request says otherwise.
constexpr const wchar_t* kUserAgent = L"MarkerPackEditor";

// Ends a request from another thread at once, even one waiting on the server for its whole
// timeout: cancel() closes the request handle the worker is blocked in, which is how WinHTTP
// lets a synchronous call be cancelled (WinHttpCloseHandle). Without it, whoever waits for the
// worker — the addon unloading, the game closing — waits out every timeout of the request.
class Cancel
{
public:
    Cancel() = default;
    Cancel(const Cancel&) = delete;
    Cancel& operator=(const Cancel&) = delete;

    // From any thread, any number of times; never undone.
    void cancel();
    bool cancelled() const { return cancelled_.load(std::memory_order_acquire); }

    // For http.cpp. `request` is the handle a request is about to block in, closed by cancel()
    // if it comes first; false, with the handle still the caller's, when it has come already.
    bool attach(void* request) const;
    // Done with the attached handle: false when cancel() closed it, which leaves it no longer
    // the caller's to close.
    bool release() const;

private:
    mutable std::mutex mutex_;
    mutable void* request_ = nullptr;
    std::atomic<bool> cancelled_{false};
};

// A server's answer, whatever its status.
struct Reply {
    int status = 0;
    std::string body;
};

// What `url` answers, with `headers` sent along — lines as HTTP writes them, each ending in
// CRLF — or nothing when no answer came: no network, a timeout, a URL WinHTTP cannot read,
// `cancel` set. Blocks for at most about `timeout` per step of the request, or until `cancel`
// is set.
std::optional<Reply> request(std::string_view url, std::string_view headers, const Cancel& cancel, std::chrono::milliseconds timeout);

// The body of `url` when it answers 200, or nothing: as request(), and any other status too.
std::optional<std::string> get(std::string_view url, const Cancel& cancel, std::chrono::milliseconds timeout = std::chrono::seconds(5));

// Bytes so far, and the whole size, 0 when the server does not say.
using Progress = std::function<void(uint64_t received, uint64_t total)>;

// Writes the body of `url` to `file` as it arrives, calling `progress` after every read, and
// stops as soon as `cancel` is set. Empty on success, else what went wrong; `file` may then
// hold part of the body, which the caller removes.
std::string download(
    std::string_view url,
    const std::filesystem::path& file,
    const Progress& progress,
    const Cancel& cancel,
    const wchar_t* user_agent = kUserAgent);

} // namespace http
