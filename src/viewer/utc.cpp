#include "viewer/utc.h"

#include <chrono>
#include <cstdio>

namespace viewer {

using namespace std::chrono;

UtcMillis utcNow() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string formatUtc(UtcMillis time) {
    const sys_time<milliseconds> point{milliseconds{time}};
    const sys_days day = floor<days>(point);
    const year_month_day date{day};
    const hh_mm_ss<seconds> clock{floor<seconds>(point - day)};
    char text[32];
    std::snprintf(
        text,
        sizeof(text),
        "%04d-%02u-%02uT%02d:%02d:%02d",
        static_cast<int>(date.year()),
        static_cast<unsigned>(date.month()),
        static_cast<unsigned>(date.day()),
        static_cast<int>(clock.hours().count()),
        static_cast<int>(clock.minutes().count()),
        static_cast<int>(clock.seconds().count()));
    return text;
}

std::optional<UtcMillis> parseUtc(const std::string& text) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%d-%u-%uT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) return std::nullopt;
    const year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    if (!date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return std::nullopt;
    const auto time = sys_days{date} + hours{hour} + minutes{minute} + seconds{second};
    return duration_cast<milliseconds>(time.time_since_epoch()).count();
}

} // namespace viewer
