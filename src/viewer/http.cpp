#include "viewer/http.h"

#include <windows.h>

#include <winhttp.h>

#include <fstream>
#include <memory>
#include <vector>

namespace viewer::http {

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

// A request that has answered 200, its handles kept open to read the body from.
struct Response {
    Handle session;
    Handle connection;
    Handle request;
    uint64_t length = 0; // Content-Length, 0 when absent
};

// Sends a GET for `url`. Nothing, with `error` saying why, unless it answered 200. Redirects
// are followed, as WinHTTP does by default, except from HTTPS to HTTP.
std::optional<Response> open(std::string_view url, std::chrono::milliseconds timeout, const wchar_t* user_agent, std::string* error) {
    const std::wstring wide = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (wide.empty() || !WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)) {
        *error = "not a web address";
        return std::nullopt;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    Response response;
    response.session.reset(WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!response.session) {
        *error = "could not start WinHTTP";
        return std::nullopt;
    }
    const int ms = static_cast<int>(timeout.count());
    WinHttpSetTimeouts(response.session.get(), ms, ms, ms, ms);

    response.connection.reset(WinHttpConnect(response.session.get(), host.c_str(), parts.nPort, 0));
    response.request.reset(
        response.connection ? WinHttpOpenRequest(
                                  response.connection.get(),
                                  L"GET",
                                  path.c_str(),
                                  nullptr,
                                  WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr);
    if (!response.request ||
        !WinHttpSendRequest(response.request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(response.request.get(), nullptr)) {
        *error = "could not reach the server";
        return std::nullopt;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(
            response.request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &size,
            WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        *error = "the server answered " + std::to_string(status);
        return std::nullopt;
    }

    wchar_t length[32] = {};
    DWORD length_size = sizeof(length);
    if (WinHttpQueryHeaders(
            response.request.get(),
            WINHTTP_QUERY_CONTENT_LENGTH,
            WINHTTP_HEADER_NAME_BY_INDEX,
            length,
            &length_size,
            WINHTTP_NO_HEADER_INDEX)) {
        response.length = std::wcstoull(length, nullptr, 10);
    }
    return response;
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

std::optional<std::string> get(std::string_view url, std::chrono::milliseconds timeout) {
    std::string error;
    const std::optional<Response> response = open(url, timeout, kUserAgent, &error);
    if (!response) return std::nullopt;
    std::string body;
    if (!readBody(*response, [&body](const char* data, size_t size) {
            body.append(data, size);
            return true;
        })) {
        return std::nullopt;
    }
    return body;
}

std::string download(
    std::string_view url,
    const std::filesystem::path& file,
    const Progress& progress,
    const std::atomic<bool>& cancel,
    const wchar_t* user_agent) {
    std::string error;
    const std::optional<Response> response = open(url, std::chrono::seconds(30), user_agent, &error);
    if (!response) return error;

    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return "could not write " + file.filename().string();

    uint64_t received = 0;
    const bool whole = readBody(*response, [&](const char* data, size_t size) {
        if (cancel) return false;
        out.write(data, static_cast<std::streamsize>(size));
        received += size;
        if (progress) progress(received, response->length);
        return static_cast<bool>(out);
    });
    if (cancel) return "cancelled";
    if (!whole || !out) return "the download broke off";
    if (response->length != 0 && received != response->length) return "the download broke off";
    return {};
}

} // namespace viewer::http
