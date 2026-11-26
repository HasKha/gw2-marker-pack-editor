#include "viewer/desktop.h"

#include <windows.h>

#include <cstring>

namespace viewer::desktop {

namespace {

// `text` as UTF-16 with its terminator as the last character, which the clipboard keeps too.
// Empty for text Windows cannot read as UTF-8.
std::wstring utf16(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring converted;
    if (length > 0) {
        converted.resize(static_cast<size_t>(length));
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, converted.data(), length);
    }
    return converted;
}

} // namespace

bool copy(const std::string& text) {
    const std::wstring converted = utf16(text);
    if (converted.empty() || !OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    bool copied = false;
    const size_t bytes = converted.size() * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        std::memcpy(GlobalLock(memory), converted.c_str(), bytes);
        GlobalUnlock(memory);
        copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        if (!copied) GlobalFree(memory);
    }
    CloseClipboard();
    return copied;
}

void openInBrowser(const std::string& url) {
    const std::wstring converted = utf16(url);
    if (!converted.empty()) ShellExecuteW(nullptr, L"open", converted.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool cursorVisible() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info)) return true;
    return (info.flags & CURSOR_SHOWING) != 0;
}

} // namespace viewer::desktop
