#include "viewer/guid.h"

#include <windows.h>

#include <bcrypt.h>

#include <cstring>
#include <random>

#include "pack/attributes.h"

namespace viewer {

bool Guid::empty() const {
    for (uint8_t byte : bytes) {
        if (byte != 0) return false;
    }
    return true;
}

namespace {

// .NET's layout: int32, int16, int16 stored little-endian, then eight bytes in order. The
// byte each character pair of the "D" form stands for.
constexpr int kStringOrder[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

size_t GuidHash::operator()(const Guid& guid) const {
    uint64_t low = 0;
    uint64_t high = 0;
    std::memcpy(&low, guid.bytes.data(), 8);
    std::memcpy(&high, guid.bytes.data() + 8, 8);
    return static_cast<size_t>(low ^ (high * 0x9E3779B97F4A7C15ull));
}

Guid guidFromText(std::string_view text) {
    if (text.size() % 4 == 0 && text.ends_with("==")) {
        // Convert.FromBase64String would throw on a bad digit, which Pathing catches and turns
        // into the empty Guid; parseGuid failing leaves the bytes zero the same way.
        Guid guid;
        pack::parseGuid(text, &guid.bytes);
        return guid;
    }
    return md5Guid(text);
}

Guid md5Guid(std::string_view text) {
    // Windows' own MD5, through the pseudo-handle BCrypt keeps for it (Windows 10 and later).
    Guid guid;
    uint8_t digest[16] = {};
    const NTSTATUS status = BCryptHash(
        BCRYPT_MD5_ALG_HANDLE,
        nullptr,
        0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
        static_cast<ULONG>(text.size()),
        digest,
        sizeof(digest));
    if (BCRYPT_SUCCESS(status)) std::copy(std::begin(digest), std::end(digest), guid.bytes.begin());
    return guid;
}

Guid xorGuid(const Guid& a, const Guid& b) {
    Guid out;
    for (size_t i = 0; i < out.bytes.size(); ++i) out.bytes[i] = a.bytes[i] ^ b.bytes[i];
    return out;
}

Guid newGuid() {
    // Per thread: packs are baked on several at once.
    thread_local std::mt19937_64 engine(std::random_device{}());
    Guid guid;
    for (size_t i = 0; i < guid.bytes.size(); i += 8) {
        const uint64_t bits = engine();
        for (size_t b = 0; b < 8; ++b) guid.bytes[i + b] = static_cast<uint8_t>(bits >> (b * 8));
    }
    return guid;
}

std::string toBase64(const Guid& guid) {
    return pack::guidToBase64(guid.bytes);
}

std::string toString(const Guid& guid) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        const uint8_t byte = guid.bytes[kStringOrder[i]];
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xF]);
    }
    return out;
}

std::optional<Guid> guidFromString(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    Guid guid;
    size_t at = 0;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            if (text[at] != '-') return std::nullopt;
            ++at;
        }
        const int high = hexValue(text[at]);
        const int low = hexValue(text[at + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        guid.bytes[kStringOrder[i]] = static_cast<uint8_t>(high << 4 | low);
        at += 2;
    }
    return guid;
}

} // namespace viewer
