#include "viewer/utc.h"

#include <charconv>
#include <chrono>
#include <format>
#include <iterator>
#include <system_error>

namespace viewer {

using namespace std::chrono;

UtcMillis utcNow() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string formatUtc(UtcMillis time) {
    return std::format("{:%Y-%m-%dT%H:%M:%S}", floor<seconds>(sys_time<milliseconds>{milliseconds{time}}));
}

std::optional<UtcMillis> parseUtc(const std::string& text) {
    // Year, month, day, hour, minute, second, each followed by its separator but the last.
    constexpr char kSeparators[] = {'-', '-', 'T', ':', ':'};
    int fields[6] = {};
    const char* at = text.data();
    const char* const end = text.data() + text.size();
    for (size_t i = 0; i < std::size(fields); ++i) {
        const std::from_chars_result read = std::from_chars(at, end, fields[i]);
        if (read.ec != std::errc{}) return std::nullopt;
        at = read.ptr;
        if (i < std::size(kSeparators)) {
            if (at == end || *at != kSeparators[i]) return std::nullopt;
            ++at;
        }
    }
    const auto [year, month, day, hour, minute, second] = fields;
    const year_month_day date{
        std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)},
        std::chrono::day{static_cast<unsigned>(day)}};
    if (!date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return std::nullopt;
    const auto point = sys_days{date} + hours{hour} + minutes{minute} + seconds{second};
    return duration_cast<milliseconds>(point.time_since_epoch()).count();
}

} // namespace viewer
