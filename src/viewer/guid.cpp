#include "viewer/guid.h"

#include <windows.h>

#include <bcrypt.h>

#include <random>

#include "pack/attributes.h"

namespace viewer {

bool Guid::empty() const {
    for (uint8_t byte : bytes) {
        if (byte != 0) return false;
    }
    return true;
}

Guid guidFromText(std::string_view text) {
    Guid guid;
    if (text.size() % 4 == 0 && text.ends_with("==")) {
        // Convert.FromBase64String would throw on a bad digit, which Pathing catches and turns
        // into the empty Guid; parseGuid failing leaves the bytes zero the same way.
        pack::parseGuid(text, &guid.bytes);
        return guid;
    }

    // Windows' own MD5, through the pseudo-handle BCrypt keeps for it (Windows 10 and later).
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

Guid newGuid() {
    static std::random_device device;
    static std::mt19937_64 engine(device());
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
    // .NET's layout: int32, int16, int16 stored little-endian, then eight bytes in order.
    static constexpr int kOrder[16] = {3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        const uint8_t byte = guid.bytes[kOrder[i]];
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0xF]);
    }
    return out;
}

} // namespace viewer
