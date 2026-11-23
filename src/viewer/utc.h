#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Wall-clock time, UTC: what timed behaviors run on and what the catalog dates packs by.
namespace viewer {

// Milliseconds since the Unix epoch, UTC.
using UtcMillis = int64_t;

UtcMillis utcNow();

// "yyyy-MM-ddTHH:mm:ss", .NET's "s" format: how Pathing writes its timers, and how the
// catalog's LastUpdate reads.
std::string formatUtc(UtcMillis time);

// That format back, to the second; anything after the seconds is ignored. Nothing for a date
// that does not exist.
std::optional<UtcMillis> parseUtc(const std::string& text);

} // namespace viewer
