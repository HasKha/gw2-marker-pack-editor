#include <windows.h>

#include <cmath>
#include <ctime>
#include <optional>
#include <string>

#include "viewer/script/api.h"

// Pathing's `os`, Scripting/Lib/Std/LuaLibraryOS.cs, which is NeoLua's with its quirks kept —
// and scripts are written against the quirks:
//
// - **Time is counted from 1970-01-01 local, not UTC.** os.time() is the local wall clock as
//   if it were UTC, and os.time{...} reads its fields as local wall-clock time. So
//   os.time(os.date("!*t")) is the real Unix time, which Hero's schedules rely on.
// - **os.date("*t").wday counts from 0 for Sunday** (.NET's DayOfWeek); Lua's counts from 1.
// - **isdst is "the time is local"**, not daylight saving: true unless the format began "!".
// - os.time{...} with an impossible field (month 13, day 31 of April) returns nil and the
//   reason, where Lua would normalize it.
// - %-formats are its own table, with unknown ones written back as they were, and names in
//   English: Pathing uses the player's culture, and a pack comparing os.date("%A") with
//   "Monday" works only in English, so English is what every player gets here.
namespace viewer::script {

namespace {

// Wall-clock fields, as .NET's DateTime holds them.
struct Fields {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    int day_of_week = 4; // 0 = Sunday
    int day_of_year = 1; // 1-based
};

Fields fieldsOf(const std::tm& tm) {
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_wday, tm.tm_yday + 1};
}

// The wall-clock fields `seconds` after 1970-01-01 00:00, with no time zone involved.
Fields wallClock(int64_t seconds) {
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
    gmtime_s(&tm, &t);
    return fieldsOf(tm);
}

int64_t secondsOf(const Fields& fields) {
    std::tm tm{};
    tm.tm_year = fields.year - 1900;
    tm.tm_mon = fields.month - 1;
    tm.tm_mday = fields.day;
    tm.tm_hour = fields.hour;
    tm.tm_min = fields.minute;
    tm.tm_sec = fields.second;
    return static_cast<int64_t>(_mkgmtime(&tm));
}

// DateTime.Now, as seconds on the local-epoch scale.
int64_t localNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    return static_cast<int64_t>(_mkgmtime(&local));
}

// DateTime.ToUniversalTime() of local wall-clock fields.
Fields toUniversal(const Fields& local) {
    std::tm tm{};
    tm.tm_year = local.year - 1900;
    tm.tm_mon = local.month - 1;
    tm.tm_mday = local.day;
    tm.tm_hour = local.hour;
    tm.tm_min = local.minute;
    tm.tm_sec = local.second;
    tm.tm_isdst = -1;
    const std::time_t real = std::mktime(&tm);
    std::tm utc{};
    gmtime_s(&utc, &real);
    return fieldsOf(utc);
}

int daysInMonth(int year, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : kDays[month - 1];
}

std::string twoDigits(int value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}

constexpr const char* kDayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
constexpr const char* kMonthNames[] =
    {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

int hour12(int hour) {
    return hour % 12 == 0 ? 12 : hour % 12;
}

// Calendar.GetWeekOfYear under en-US's FirstDay rule: week 1 holds 1 January.
int weekOfYear(const Fields& fields, int first_day) {
    const int jan1 = ((fields.day_of_week - (fields.day_of_year - 1)) % 7 + 7) % 7;
    const int offset = (jan1 - first_day + 7) % 7;
    return (fields.day_of_year - 1 + offset) / 7 + 1;
}

std::string utcOffset() {
    TIME_ZONE_INFORMATION zone{};
    const DWORD which = GetTimeZoneInformation(&zone);
    const long bias = zone.Bias + (which == TIME_ZONE_ID_DAYLIGHT ? zone.DaylightBias : 0); // minutes, UTC = local + bias
    const long offset = -bias;
    const long magnitude = std::abs(offset);
    return std::string(offset < 0 ? "-" : "+") + twoDigits(static_cast<int>(magnitude / 60)) + ":" +
           twoDigits(static_cast<int>(magnitude % 60));
}

// DateTime.ToString() under en-US.
std::string defaultText(const Fields& f) {
    return std::to_string(f.month) + "/" + std::to_string(f.day) + "/" + std::to_string(f.year) + " " + std::to_string(hour12(f.hour)) +
           ":" + twoDigits(f.minute) + ":" + twoDigits(f.second) + (f.hour < 12 ? " AM" : " PM");
}

std::optional<std::string> specifier(char c, const Fields& f) {
    switch (c) {
    case 'a': return std::string(kDayNames[f.day_of_week]).substr(0, 3);
    case 'A': return kDayNames[f.day_of_week];
    case 'b': return std::string(kMonthNames[f.month - 1]).substr(0, 3);
    case 'B': return kMonthNames[f.month - 1];
    case 'c':
        return std::string(kDayNames[f.day_of_week]).substr(0, 3) + " " + std::string(kMonthNames[f.month - 1]).substr(0, 3) + " " +
               twoDigits(f.day) + " " + twoDigits(f.hour) + ":" + twoDigits(f.minute) + ":" + twoDigits(f.second) + " " +
               std::to_string(f.year);
    case 'd': return twoDigits(f.day);
    case 'e': return (f.day < 10 ? " " : "") + std::to_string(f.day);
    case 'H': return twoDigits(f.hour);
    case 'I': return twoDigits(hour12(f.hour));
    case 'j': {
        std::string day = std::to_string(f.day_of_year);
        return std::string(3 - day.size(), '0') + day;
    }
    case 'm': return twoDigits(f.month);
    case 'M': return twoDigits(f.minute);
    case 'p': return f.hour < 12 ? "AM" : "PM";
    case 'S': return twoDigits(f.second);
    case 'U': return twoDigits(weekOfYear(f, 0));
    case 'W': return twoDigits(weekOfYear(f, 1));
    case 'w': return std::to_string(f.day_of_week);
    case 'x': return std::to_string(f.month) + "/" + std::to_string(f.day) + "/" + std::to_string(f.year);
    case 'X': return std::to_string(hour12(f.hour)) + ":" + twoDigits(f.minute) + ":" + twoDigits(f.second) + (f.hour < 12 ? " AM" : " PM");
    case 'y': return twoDigits(f.year % 100);
    case 'Y': return std::to_string(f.year);
    case 'Z': return utcOffset();
    case '%': return "%";
    default: return std::nullopt;
    }
}

// ToStrFTime, down to its handling of a '%' as the last character: written as it is.
std::string formatTime(const Fields& fields, const std::string& pattern) {
    if (pattern.empty()) return defaultText(fields);
    std::string out;
    for (size_t n = 0; n < pattern.size(); ++n) {
        const char c = pattern[n];
        if (n + 1 >= pattern.size() || c != '%') {
            out += c;
            continue;
        }
        const char next = pattern[++n];
        if (const std::optional<std::string> text = specifier(next, fields)) {
            out += *text;
        } else {
            out += '%';
            out += next;
        }
    }
    return out;
}

// LuaLibraryOS.datetime of a table: the fields, all integers, 1970 at the earliest. Nothing,
// and the reason, for what DateTime's constructor would refuse.
std::optional<Fields> fieldsFromTable(lua_State* L, const sol::table& table, std::string* error) {
    Fields fields;
    const auto read = [&](const char* key, int fallback, int* out) {
        const sol::object value = table[key];
        if (value.get_type() == sol::type::lua_nil) {
            *out = fallback;
            return true;
        }
        // (int)table["year"] in .NET: an Int32 or nothing, so a float year is refused.
        if (value.get_type() != sol::type::number || !isInteger(L, value)) {
            *error = "Specified cast is not valid.";
            return false;
        }
        *out = value.as<int>();
        return true;
    };
    if (!read("year", 1970, &fields.year) || !read("month", 1, &fields.month) || !read("day", 1, &fields.day) ||
        !read("hour", 0, &fields.hour) || !read("min", 0, &fields.minute) || !read("sec", 0, &fields.second)) {
        return std::nullopt;
    }
    if (fields.year < 1970) fields.year = 1970;

    const bool valid = fields.year <= 9999 && fields.month >= 1 && fields.month <= 12 && fields.day >= 1 &&
                       fields.day <= daysInMonth(fields.year, fields.month) && fields.hour >= 0 && fields.hour < 24 && fields.minute >= 0 &&
                       fields.minute < 60 && fields.second >= 0 && fields.second < 60;
    if (!valid) {
        *error = "Year, Month, and Day parameters describe an un-representable DateTime.";
        return std::nullopt;
    }
    return fields;
}

// The number a time value stands for: a number, or a table read as os.time reads it.
std::optional<int64_t> timeValue(lua_State* L, const sol::object& value) {
    if (value.get_type() == sol::type::number) return static_cast<int64_t>(std::llround(value.as<double>()));
    if (value.get_type() == sol::type::table) {
        std::string error;
        if (const std::optional<Fields> fields = fieldsFromTable(L, value.as<sol::table>(), &error)) return secondsOf(*fields);
    }
    return std::nullopt;
}

} // namespace

sol::table makeOsLibrary(sol::state_view lua) {
    sol::table os = lua.create_table();

    // Process.TotalProcessorTime: user and kernel time together.
    os["clock"] = []() {
        FILETIME created, exited, kernel, user;
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
        const auto ticks = [](const FILETIME& time) { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        return static_cast<double>(ticks(kernel) + ticks(user)) / 1e7;
    };

    os["time"] = [](sol::this_state state, const sol::object& table) -> sol::variadic_results {
        sol::variadic_results results;
        if (table.get_type() != sol::type::table) {
            results.push_back(sol::make_object(state, localNow()));
            return results;
        }
        std::string error;
        if (const std::optional<Fields> fields = fieldsFromTable(state, table.as<sol::table>(), &error)) {
            results.push_back(sol::make_object(state, secondsOf(*fields)));
        } else {
            results.push_back(sol::make_object(state, sol::lua_nil));
            results.push_back(sol::make_object(state, error));
        }
        return results;
    };

    os["date"] = [](sol::this_state state, const sol::object& format_value, const sol::object& time) -> sol::object {
        std::string format = format_value.get_type() == sol::type::string ? format_value.as<std::string>() : std::string();
        const bool utc = !format.empty() && format.front() == '!';
        if (utc) format.erase(0, 1);

        Fields fields;
        if (time.get_type() == sol::type::lua_nil || time.get_type() == sol::type::none) {
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
            utc ? gmtime_s(&tm, &now) : localtime_s(&tm, &now);
            fields = fieldsOf(tm);
        } else {
            const std::optional<int64_t> seconds = timeValue(state, time);
            if (!seconds) raise(state, "bad argument #2 to 'date' (number expected)");
            fields = wallClock(*seconds);
            if (utc) fields = toUniversal(fields);
        }

        if (format == "*t") {
            sol::table out = sol::state_view(state).create_table();
            out["year"] = fields.year;
            out["month"] = fields.month;
            out["day"] = fields.day;
            out["hour"] = fields.hour;
            out["min"] = fields.minute;
            out["sec"] = fields.second;
            out["wday"] = fields.day_of_week;
            out["yday"] = fields.day_of_year;
            out["isdst"] = !utc;
            return out;
        }
        return sol::make_object(state, formatTime(fields, format));
    };

    os["difftime"] = [](sol::this_state state, const sol::object& t2, const sol::object& t1) {
        const std::optional<int64_t> later = timeValue(state, t2);
        const std::optional<int64_t> earlier = timeValue(state, t1);
        if (!later || !earlier) raise(state, "bad argument to 'difftime' (number expected)");
        return *later - *earlier;
    };

    return os;
}

} // namespace viewer::script
