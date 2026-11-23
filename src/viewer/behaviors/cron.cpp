#include "viewer/behaviors/cron.h"

#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <string>
#include <vector>

namespace viewer::behaviors {

namespace {

constexpr int64_t kMillisPerSecond = 1000;
constexpr int kSecondsPerDay = 24 * 60 * 60;
constexpr int kSearchDays = 28 * 366;

// One field's bounds, and the names it may be written with, from value `first` on.
struct Field {
    int min;
    int max;
    std::array<const char*, 12> names{};
    int first_name = 0;
    bool allows_any_mark = false; // `?`, which Cronos takes in the two day fields only
};

constexpr Field kSecondField{0, 59};
constexpr Field kMinuteField{0, 59};
constexpr Field kHourField{0, 23};
constexpr Field kDayField{1, 31, {}, 0, true};
constexpr Field kMonthField{1, 12, {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"}, 1};
// 7 is Sunday too, folded onto 0 once read.
constexpr Field kWeekdayField{0, 7, {"sun", "mon", "tue", "wed", "thu", "fri", "sat"}, 0, true};

std::optional<int> parseValue(std::string_view text, const Field& field) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc() && end == text.data() + text.size()) {
        if (value < field.min || value > field.max) return std::nullopt;
        return value;
    }
    if (text.size() != 3) return std::nullopt;
    std::string lower(text);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (size_t i = 0; i < field.names.size() && field.names[i]; ++i) {
        if (lower == field.names[i]) return field.first_name + static_cast<int>(i);
    }
    return std::nullopt;
}

// One comma-separated field into a bit per value, or nothing when it does not parse.
std::optional<uint64_t> parseField(std::string_view text, const Field& field) {
    uint64_t bits = 0;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        std::string_view item = text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        start = comma == std::string_view::npos ? text.size() + 1 : comma + 1;

        int step = 1;
        if (const size_t slash = item.find('/'); slash != std::string_view::npos) {
            const std::string_view step_text = item.substr(slash + 1);
            const auto [end, error] = std::from_chars(step_text.data(), step_text.data() + step_text.size(), step);
            if (error != std::errc() || end != step_text.data() + step_text.size() || step <= 0) return std::nullopt;
            item = item.substr(0, slash);
        }

        int low = field.min;
        int high = field.max;
        const bool any = item == "*" || (field.allows_any_mark && item == "?");
        if (!any) {
            const size_t dash = item.find('-');
            const std::optional<int> from = parseValue(item.substr(0, dash), field);
            if (!from) return std::nullopt;
            low = *from;
            if (dash != std::string_view::npos) {
                const std::optional<int> to = parseValue(item.substr(dash + 1), field);
                if (!to || *to < low) return std::nullopt;
                high = *to;
            } else if (step == 1) {
                high = low;
            } // `a/n` runs from a to the field's end
        }
        for (int value = low; value <= high; value += step) bits |= uint64_t{1} << value;
    }
    return bits;
}

// The lowest value in `bits` that is at least `from`, or -1.
int firstAtOrAfter(uint64_t bits, int from) {
    if (from >= 64) return -1;
    const uint64_t remaining = bits & (~uint64_t{0} << from);
    return remaining ? std::countr_zero(remaining) : -1;
}

std::vector<std::string_view> splitFields(std::string_view text) {
    std::vector<std::string_view> fields;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > start) fields.push_back(text.substr(start, i - start));
    }
    return fields;
}

} // namespace

std::optional<CronSchedule> CronSchedule::parse(std::string_view text) {
    std::vector<std::string_view> fields = splitFields(text);
    if (fields.size() == 5) fields.insert(fields.begin(), "0");
    if (fields.size() != 6) return std::nullopt;

    const std::optional<uint64_t> seconds = parseField(fields[0], kSecondField);
    const std::optional<uint64_t> minutes = parseField(fields[1], kMinuteField);
    const std::optional<uint64_t> hours = parseField(fields[2], kHourField);
    const std::optional<uint64_t> days = parseField(fields[3], kDayField);
    const std::optional<uint64_t> months = parseField(fields[4], kMonthField);
    std::optional<uint64_t> weekdays = parseField(fields[5], kWeekdayField);
    if (!seconds || !minutes || !hours || !days || !months || !weekdays) return std::nullopt;
    if (*weekdays & (uint64_t{1} << 7)) *weekdays = (*weekdays | 1) & 0x7F;

    CronSchedule schedule;
    schedule.seconds_ = *seconds;
    schedule.minutes_ = *minutes;
    schedule.hours_ = static_cast<uint32_t>(*hours);
    schedule.days_ = static_cast<uint32_t>(*days);
    schedule.months_ = static_cast<uint16_t>(*months);
    schedule.weekdays_ = static_cast<uint8_t>(*weekdays);
    return schedule;
}

std::optional<int> CronSchedule::firstTimeOfDay(int start) const {
    const int start_hour = start / 3600;
    const int start_minute = start / 60 % 60;
    const int start_second = start % 60;
    for (int hour = firstAtOrAfter(hours_, start_hour); hour >= 0; hour = firstAtOrAfter(hours_, hour + 1)) {
        const bool same_hour = hour == start_hour;
        for (int minute = firstAtOrAfter(minutes_, same_hour ? start_minute : 0); minute >= 0;
             minute = firstAtOrAfter(minutes_, minute + 1)) {
            const bool same_minute = same_hour && minute == start_minute;
            const int second = firstAtOrAfter(seconds_, same_minute ? start_second : 0);
            if (second >= 0) return hour * 3600 + minute * 60 + second;
        }
    }
    return std::nullopt;
}

std::optional<UtcMillis> CronSchedule::next(UtcMillis from) const {
    using namespace std::chrono;
    // Occurrences fall on whole seconds, so one partway through a second has already passed.
    const int64_t from_second = from / kMillisPerSecond + (from % kMillisPerSecond > 0 ? 1 : 0);
    const sys_days first_day = floor<days>(sys_seconds{seconds{from_second}});
    int start = static_cast<int>(from_second - duration_cast<seconds>(first_day.time_since_epoch()).count());

    for (int offset = 0; offset < kSearchDays; ++offset, start = 0) {
        const sys_days day = first_day + days{offset};
        const year_month_day date{day};
        const unsigned weekday = std::chrono::weekday{day}.c_encoding();
        const bool matches = (months_ >> static_cast<unsigned>(date.month()) & 1) && (days_ >> static_cast<unsigned>(date.day()) & 1) &&
                             (weekdays_ >> weekday & 1);
        if (!matches) continue;
        if (const std::optional<int> time = firstTimeOfDay(start)) {
            const int64_t second = duration_cast<seconds>(day.time_since_epoch()).count() + *time;
            return second * kMillisPerSecond;
        }
    }
    return std::nullopt;
}

bool updateSchedule(const CronSchedule& schedule, UtcMillis duration, UtcMillis now, ScheduleState* state) {
    if (!state->started) {
        state->started = true;
        state->next_trigger = schedule.next(now - duration);
    }

    // A window that is over gives way to the next, however long ago it closed. With none to
    // find the search is not run again: it walks 28 years of days.
    if (state->next_trigger && now >= *state->next_trigger + duration) state->next_trigger = schedule.next(now);

    const bool open = state->next_trigger && now >= *state->next_trigger && now < *state->next_trigger + duration;
    state->filtered = !open;
    return state->filtered;
}

} // namespace viewer::behaviors
