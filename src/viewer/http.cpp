#include "viewer/http.h"

#include <windows.h>

#include <winhttp.h>

#include <memory>

namespace viewer::http {

namespace {

struct HandleCloser {
    void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); }
};
using Handle = std::unique_ptr<void, HandleCloser>;

} // namespace

std::optional<std::string> get(const std::wstring& host, const std::wstring& path, std::chrono::milliseconds timeout) {
    const Handle session(
        WinHttpOpen(L"MarkerPackEditor", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return std::nullopt;
    const int ms = static_cast<int>(timeout.count());
    WinHttpSetTimeouts(session.get(), ms, ms, ms, ms);

    const Handle connection(WinHttpConnect(session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) return std::nullopt;
    const Handle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request) return std::nullopt;
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return std::nullopt;
    if (!WinHttpReceiveResponse(request.get(), nullptr)) return std::nullopt;

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &size,
            WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        return std::nullopt;
    }

    std::string body;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) return std::nullopt;
        if (available == 0) return body;
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), body.data() + offset, available, &read)) return std::nullopt;
        body.resize(offset + read);
    }
}

} // namespace viewer::http
