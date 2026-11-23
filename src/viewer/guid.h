#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// A marker's or trail's identity as Pathing holds it: a .NET Guid, 16 bytes.
//
// Scripts look markers up by it, compare it and key tables with it, and behaviors (hidden
// until a timer, toggled) remember markers by it, so what a given `guid` attribute turns into
// has to be Pathing's to the byte.
namespace viewer {

struct Guid {
    std::array<uint8_t, 16> bytes{};

    bool operator==(const Guid&) const = default;
    bool empty() const;
};

// AttributeParsingUtil.InternalGetValueAsGuid: base64 when the text looks like it — a length
// that is a multiple of four and a trailing "==" — and otherwise the MD5 of its UTF-8 bytes,
// which is how a made-up guid="my-marker" still gets a stable identity. Anything that still
// is not 16 bytes is the empty Guid.
Guid guidFromText(std::string_view text);

// A fresh random one, as Pathing gives a pathable with no `guid` (Guid.NewGuid()).
Guid newGuid();

// Convert.ToBase64String: what Guid:ToBase64() hands a script.
std::string toBase64(const Guid& guid);

// Guid.ToString(): "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", lowercase, with .NET's byte order —
// the first three groups are little-endian integers, the last two are bytes as they are.
std::string toString(const Guid& guid);

} // namespace viewer
