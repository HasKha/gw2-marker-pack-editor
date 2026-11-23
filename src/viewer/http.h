#pragma once

#include <chrono>
#include <optional>
#include <string>

// HTTPS requests from a worker thread, through WinHTTP: what the viewer asks the web for —
// the festivals on today, and later the pack catalog. Never called from a frame.
namespace viewer::http {

// The body of `https://<host><path>` when it answers 200, or nothing: no network, a timeout,
// any other status. Blocks for at most about `timeout` per step of the request.
std::optional<std::string>
get(const std::wstring& host, const std::wstring& path, std::chrono::milliseconds timeout = std::chrono::seconds(5));

} // namespace viewer::http
