#include "http/http.h"

#include <windows.h>

#include <winhttp.h>

#include <fstream>
#include <memory>
#include <vector>

#include "util/path_text.h"

namespace http {

namespace {

struct HandleCloser {
    void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
};
using Handle = std::unique_ptr<void, HandleCloser>;

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// A request that has answered, its handles kept open to read the body from. Made in place
// and never moved: its request handle is attached to a Cancel, which may close it, and only
// the Response that attached it may let go of it.
struct Response {
    Response() = default;
    Response(const Response&) = delete;
    Response& operator=(const Response&) = delete;
    ~Response() {
        if (cancel && !cancel->release()) (void)request.release();
    }

    const Cancel* cancel = nullptr; // set once the request handle is attached to it
    Handle session;
    Handle connection;
    Handle request;
    DWORD status = 0;
    uint64_t length = 0; // Content-Length, 0 when absent
};

// Sends a GET for `url`, with `headers` added, into `response`. False, with `error` saying
// why, when no answer came; any status is an answer. Redirects are followed, as WinHTTP does
// by default, except from HTTPS to HTTP.
bool open(
    std::string_view url,
    std::string_view headers,
    std::chrono::milliseconds timeout,
    const wchar_t* user_agent,
    const Cancel& cancel,
    Response* response,
    std::string* error) {
    const std::wstring wide = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (wide.empty() || !WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)) {
        *error = "not a web address";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    response->session.reset(
        WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!response->session) {
        *error = "could not start WinHTTP";
        return false;
    }
    const int ms = static_cast<int>(timeout.count());
    WinHttpSetTimeouts(response->session.get(), ms, ms, ms, ms);

    response->connection.reset(WinHttpConnect(response->session.get(), host.c_str(), parts.nPort, 0));
    response->request.reset(
        response->connection ? WinHttpOpenRequest(
                                   response->connection.get(),
                                   L"GET",
                                   path.c_str(),
                                   nullptr,
                                   WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                             : nullptr);
    if (response->request) {
        if (cancel.attach(response->request.get())) {
            response->cancel = &cancel;
        } else {
            *error = "cancelled";
            return false;
        }
    }
    const std::wstring wide_headers = widen(headers);
    if (!response->request ||
        !WinHttpSendRequest(
            response->request.get(),
            wide_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wide_headers.c_str(),
            static_cast<DWORD>(wide_headers.size()),
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(response->request.get(), nullptr)) {
        *error = cancel.cancelled() ? "cancelled" : "could not reach the server";
        return false;
    }

    DWORD size = sizeof(response->status);
    if (!WinHttpQueryHeaders(
            response->request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &response->status,
            &size,
            WINHTTP_NO_HEADER_INDEX)) {
        *error = "the server's answer could not be read";
        return false;
    }

    wchar_t length[32] = {};
    DWORD length_size = sizeof(length);
    if (WinHttpQueryHeaders(
            response->request.get(),
            WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX,
            length,
            &length_size,
            WINHTTP_NO_HEADER_INDEX)) {
        response->length = std::wcstoull(length, nullptr, 10);
    }
    return true;
}

// Reads the body, handing each piece to `sink`, which returns false to stop. False when the
// body could not be read or the sink stopped it.
template <class Sink>
bool readBody(const Response& response, Sink&& sink) {
    std::vector<char> buffer;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(response.request.get(), &available)) return false;
        if (available == 0) return true;
        buffer.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(response.request.get(), buffer.data(), available, &read)) return false;
        if (!sink(buffer.data(), static_cast<size_t>(read))) return false;
    }
}

} // namespace

void Cancel::cancel() {
    const std::lock_guard<std::mutex> lock(mutex_);
    cancelled_.store(true, std::memory_order_release);
    if (request_) WinHttpCloseHandle(request_);
    request_ = nullptr;
}

bool Cancel::attach(void* request) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled()) return false;
    request_ = request;
    return true;
}

bool Cancel::release() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const bool ours = request_ != nullptr;
    request_ = nullptr;
    return ours;
}

std::optional<Reply> request(std::string_view url, std::string_view headers, const Cancel& cancel, std::chrono::milliseconds timeout) {
    std::string error;
    Response response;
    if (!open(url, headers, timeout, kUserAgent, cancel, &response, &error)) return std::nullopt;
    Reply reply{.status = static_cast<int>(response.status)};
    if (!readBody(response, [&](const char* data, size_t size) {
            reply.body.append(data, size);
            return !cancel.cancelled();
        })) {
        return std::nullopt;
    }
    return reply;
}

std::optional<std::string> get(std::string_view url, const Cancel& cancel, std::chrono::milliseconds timeout) {
    std::optional<Reply> reply = request(url, {}, cancel, timeout);
    if (reply && reply->status == 200) return std::move(reply->body);
    return std::nullopt;
}

std::string download(
    std::string_view url,
    const std::filesystem::path& file,
    const Progress& progress,
    const Cancel& cancel,
    const wchar_t* user_agent) {
    std::string error;
    Response response;
    if (!open(url, {}, std::chrono::seconds(30), user_agent, cancel, &response, &error)) return error;
    if (response.status != 200) return "the server answered " + std::to_string(response.status);

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return "could not write " + util::pathText(file.filename());

    uint64_t received = 0;
    const bool whole = readBody(response, [&](const char* data, size_t size) {
        if (cancel.cancelled()) return false;
        out.write(data, static_cast<std::streamsize>(size));
        received += size;
        if (progress) progress(received, response.length);
        return static_cast<bool>(out);
    });
    if (cancel.cancelled()) return "cancelled";
    if (!whole || !out) return "the download broke off";
    if (response.length != 0 && received != response.length) return "the download broke off";
    return {};
}

} // namespace http
