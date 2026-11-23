#include "viewer/behaviors/attributes.h"

#include <algorithm>
#include <cctype>
#include <charconv>

#include "pack/attributes.h"
#include "viewer/mumble.h"

namespace viewer::behaviors {

namespace {

bool isBlank(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); });
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && lowercase(a) == lowercase(b);
}

// AttributeParsingUtil splits every list at commas and trims nothing — except where the
// conversion itself skips whitespace, as int.TryParse does and an enum name lookup does not.
std::vector<std::string_view> splitList(std::string_view text) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t comma = text.find(',', start);
        parts.push_back(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
        if (comma == std::string_view::npos) return parts;
        start = comma + 1;
    }
}

// int.TryParse with Int32's default style: whitespace around, one sign, digits.
std::optional<int> parseDotNetInt(std::string_view text) {
    text = trimmed(text);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::optional<float> parseDotNetFloat(std::string_view text) {
    text = trimmed(text);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    float value = 0.0f;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

// Script.GetValueFromString, which only ever sees a non-empty value: SplitFunc gives up on
// the whole call when one is empty.
ScriptArg scriptArg(std::string_view value) {
    if (value.front() != '\'' && value.front() != '"') {
        if (value == "nil") return std::monostate{};
        if (value.find('.') != std::string_view::npos) {
            if (const std::optional<float> number = parseDotNetFloat(value)) return *number;
            return std::string(value); // as it is, quotes and all: Pathing returns it early
        }
        if (const std::optional<int> number = parseDotNetInt(value)) return *number;
        if (equalsIgnoringCase(value, "true")) return true;
        if (equalsIgnoringCase(value, "false")) return false;
    }
    while (!value.empty() && (value.front() == '\'' || value.front() == '"')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == '\'' || value.back() == '"')) value.remove_suffix(1);
    return std::string(value);
}

// The bool attributes' reading in Pathing, InternalGetValueAsBool: a positive integer, or
// "true" in any case. Not pack::parseBool, which takes "yes" and negative numbers too.
bool pathingBool(std::string_view text) {
    int value = 0;
    return (pack::parseInt(text, &value) && value > 0) || equalsIgnoringCase(text, "true");
}

class Reader
{
public:
    Reader(const pack::Pack& pack, const pack::Pathable& pathable)
        : pack_(pack)
        , pathable_(pathable) {}

    const std::string* value(std::string_view key) const { return pack_.effectiveValue(pathable_, key); }

    bool any(std::initializer_list<std::string_view> keys) const {
        return std::any_of(keys.begin(), keys.end(), [&](std::string_view key) { return value(key) != nullptr; });
    }

    float number(std::string_view key, float fallback) const {
        const std::string* text = value(key);
        float out = fallback;
        if (text) pack::parseFloat(*text, &out);
        return out;
    }

    // GetValueAsEnums: each part looked up by name, an unknown one standing for 0.
    std::optional<std::vector<int>> enums(std::string_view key, Gw2Enum type) const {
        const std::string* text = value(key);
        if (!text) return std::nullopt;
        std::vector<int> values;
        for (std::string_view part : splitList(*text)) values.push_back(enumValue(type, part));
        return values;
    }

    std::optional<std::vector<int>> ints(std::string_view key) const {
        const std::string* text = value(key);
        if (!text) return std::nullopt;
        std::vector<int> values;
        for (std::string_view part : splitList(*text)) values.push_back(parseDotNetInt(part).value_or(0));
        return values;
    }

    // ShowHideModifier and ToggleModifier: a category by its namespace, nothing when blank.
    std::string category(std::string_view key) const {
        const std::string* text = value(key);
        return text && !isBlank(*text) ? lowercase(*text) : std::string();
    }

    std::optional<ScriptCall> script(std::string_view key) const {
        const std::string* text = value(key);
        return text ? parseScriptCall(*text) : std::nullopt;
    }

private:
    const pack::Pack& pack_;
    const pack::Pathable& pathable_;
};

void readFilters(const Reader& read, Behaviors* out) {
    if (const std::string* text = read.value("festival")) {
        std::vector<std::string> names;
        for (std::string_view part : splitList(*text)) names.push_back(lowercase(trimmed(part)));
        out->festivals = std::move(names);
    }
    out->mounts = read.enums("mount", Gw2Enum::Mount);
    out->professions = read.enums("profession", Gw2Enum::Profession);
    out->races = read.enums("race", Gw2Enum::Race);
    out->specializations = read.ints("specialization");
    out->map_types = read.enums("maptype", Gw2Enum::MapType);

    const std::string* expression = read.value("schedule");
    const float minutes = read.number("schedule-duration", 0.0f);
    if (expression && read.value("schedule-duration") && minutes > 0.0f) {
        out->schedule = CronSchedule::parse(*expression);
        out->schedule_duration = static_cast<UtcMillis>(static_cast<double>(minutes) * 60'000.0);
    }
}

void readModifiers(const Reader& read, Behaviors* out) {
    if (const std::string* text = read.value("behavior")) pack::parseInt(*text, &out->mode);
    if (const std::string* text = read.value("invertbehavior")) out->invert = pathingBool(*text);
    if (const std::string* text = read.value("autotrigger")) out->auto_trigger = pathingBool(*text);

    if (const std::string* text = read.value("info")) out->info = *text;

    if (read.any({"bounce", "bounce-delay", "bounce-height", "bounce-duration"})) {
        Bounce bounce;
        if (const std::string* mode = read.value("bounce"))
            bounce.mode = equalsIgnoringCase(*mode, "rise") ? BounceMode::Rise : BounceMode::Bounce;
        bounce.delay = read.number("bounce-delay", bounce.delay);
        bounce.height = read.number("bounce-height", bounce.height);
        bounce.duration = read.number("bounce-duration", bounce.duration);
        out->bounce = bounce;
    }

    if (read.any({"copy", "copy-message"})) {
        Copy copy;
        if (const std::string* value = read.value("copy")) copy.value = *value;
        if (const std::string* message = read.value("copy-message")) copy.message = *message;
        out->copy = std::move(copy);
    }

    // TacO named it ToggleCategory, and that spelling wins when both are there.
    out->toggle = read.category(read.value("togglecategory") ? "togglecategory" : "toggle");
    out->show = read.category("show");
    out->hide = read.category("hide");

    if (const std::string* text = read.value("resetguid")) {
        std::vector<Guid> guids;
        for (std::string_view part : splitList(*text)) guids.push_back(guidFromText(part));
        out->reset_guids = std::move(guids);
    }
}

void readScript(const Reader& read, bool is_marker, Behaviors* out) {
    out->scripted = read.any({"script-tick", "script-focus", "script-trigger", "script-filter", "script-once"});
    if (!out->scripted) return;
    out->tick = read.script("script-tick");
    out->filter = read.script("script-filter");
    out->once = read.script("script-once");
    if (!is_marker) return;
    out->focus = read.script("script-focus");
    out->trigger = read.script("script-trigger");
}

// Every attribute a behavior reads, by key. "invertbehavior" and "autotrigger" do nothing
// alone and so are not among what makes a marker worth reading.
constexpr std::string_view kBehaviorKeys[] = {
    "festival",     "mount",          "profession",     "race",         "specialization", "maptype",         "schedule",
    "behavior",     "info",           "bounce",         "bounce-delay", "bounce-height",  "bounce-duration", "copy",
    "copy-message", "toggle",         "togglecategory", "show",         "hide",           "resetguid",       "script-tick",
    "script-focus", "script-trigger", "script-filter",  "script-once",
};

bool namesBehavior(const pack::AttributeCollection& attributes) {
    return std::any_of(attributes.all().begin(), attributes.all().end(), [](const pack::Attribute& attribute) {
        return std::find(std::begin(kBehaviorKeys), std::end(kBehaviorKeys), attribute.key) != std::end(kBehaviorKeys);
    });
}

// One walk up the chain looking at what is there, rather than one per attribute looking for
// it: most markers have no behavior, and the bake reads 56k of them.
bool mayHaveBehaviors(const pack::Pack& pack, const pack::Pathable& pathable) {
    if (namesBehavior(pathable.attributes)) return true;
    for (size_t category = pathable.category; category != pack::kNoIndex; category = pack.categories[category].parent) {
        if (namesBehavior(pack.categories[category].attributes)) return true;
    }
    return false;
}

bool hasAny(const Behaviors& b) {
    return b.festivals || b.mounts || b.professions || b.races || b.specializations || b.map_types || b.schedule || b.mode > 0 || b.info ||
           b.bounce || b.copy || !b.toggle.empty() || !b.show.empty() || !b.hide.empty() || b.reset_guids || b.scripted;
}

} // namespace

std::optional<ScriptCall> parseScriptCall(std::string_view text) {
    if (isBlank(text)) return std::nullopt;
    ScriptCall call;
    const size_t open = text.find('(');
    if (open == std::string_view::npos) {
        call.name = std::string(text);
        return call;
    }
    call.name = std::string(text.substr(0, open));

    // Split('(')[1]: what follows the first "(", up to a second one if there is one.
    std::string_view arguments = text.substr(open + 1);
    arguments = arguments.substr(0, arguments.find('('));
    while (!arguments.empty() && (arguments.front() == ' ' || arguments.front() == ')')) arguments.remove_prefix(1);
    while (!arguments.empty() && (arguments.back() == ' ' || arguments.back() == ')')) arguments.remove_suffix(1);
    if (arguments.empty()) return call;

    for (std::string_view part : splitList(arguments)) {
        // value[0] of an empty part throws in Pathing, which drops the whole attribute.
        if (part.empty()) return std::nullopt;
        call.args.push_back(scriptArg(part));
    }
    return call;
}

std::string formatMessage(std::string_view message, std::string_view value) {
    std::string out;
    for (size_t i = 0; i < message.size(); ++i) {
        const char c = message[i];
        if (c == '}') {
            if (i + 1 < message.size() && message[i + 1] == '}') {
                out.push_back('}');
                ++i;
                continue;
            }
            return std::string(message); // a lone "}" is a FormatException
        }
        if (c != '{') {
            out.push_back(c);
            continue;
        }
        if (i + 1 < message.size() && message[i + 1] == '{') {
            out.push_back('{');
            ++i;
            continue;
        }
        // {index[,alignment][:format]}: one argument, so only index 0 formats.
        const size_t close = message.find('}', i);
        if (close == std::string_view::npos) return std::string(message);
        const std::string_view item = message.substr(i + 1, close - i - 1);
        const std::string_view index = trimmed(item.substr(0, item.find_first_of(",:")));
        if (index != "0") return std::string(message);
        out.append(value);
        i = close;
    }
    return out;
}

std::shared_ptr<const Behaviors> bakeBehaviors(const pack::Pack& pack, const pack::Pathable& pathable, bool is_marker) {
    if (!mayHaveBehaviors(pack, pathable)) return nullptr;
    const Reader read(pack, pathable);
    auto behaviors = std::make_shared<Behaviors>();
    readFilters(read, behaviors.get());
    if (is_marker) readModifiers(read, behaviors.get());
    readScript(read, is_marker, behaviors.get());
    if (!hasAny(*behaviors)) return nullptr;
    return behaviors;
}

} // namespace viewer::behaviors
