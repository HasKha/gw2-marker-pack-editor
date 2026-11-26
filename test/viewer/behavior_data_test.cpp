// What behaviors are made of, each as a function of its inputs: the cron schedule, the
// states of hidden markers and their file, and the attributes read into a marker's
// behaviors, with Pathing's parsing kept to its quirks.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#include "corpus.h"
#include "pack/source.h"
#include "pack/xml_reader.h"
#include "viewer/behaviors/attributes.h"
#include "viewer/behaviors/cron.h"
#include "viewer/behaviors/festivals.h"
#include "viewer/behaviors/states.h"
#include "viewer/game/mumble.h"

using namespace viewer::behaviors;
using viewer::UtcMillis;

namespace {

// Milliseconds since the epoch of a UTC date and time.
UtcMillis utc(int year, unsigned month, unsigned day, int hour = 0, int minute = 0, int second = 0) {
    using namespace std::chrono;
    const auto time = sys_days{std::chrono::year{year} / month / day} + hours{hour} + minutes{minute} + seconds{second};
    return duration_cast<milliseconds>(time.time_since_epoch()).count();
}

constexpr UtcMillis kMinute = 60'000;

struct Loaded {
    std::unique_ptr<pack::DataSource> source;
    pack::Pack model;
};

// One POI with `attributes`, under a category carrying `category_attributes`.
Loaded loadMarker(const corpus::TempDir& temp, const std::string& attributes, const std::string& category_attributes = "") {
    std::ofstream(temp / "markers.xml") << "<OverlayData>\n<MarkerCategory name=\"c\" " << category_attributes
                                        << "/>\n<POIs><POI type=\"c\" xpos=\"1\" ypos=\"2\" zpos=\"3\" " << attributes
                                        << "/></POIs>\n</OverlayData>\n";
    Loaded out;
    std::string error;
    out.source = pack::open(temp.path(), &error);
    REQUIRE(out.source != nullptr);
    out.model = pack::loadPack(*out.source);
    return out;
}

std::shared_ptr<const Behaviors> behaviorsOf(const std::string& name, const std::string& attributes, const std::string& category = "") {
    const corpus::TempDir temp(name);
    const Loaded loaded = loadMarker(temp, attributes, category);
    return bakeBehaviors(loaded.model, loaded.model.pathables.at(0), true);
}

} // namespace

TEST_CASE("a cron expression's next occurrence is found in UTC", "[behaviors][cron]") {
    // Hero's and LadyElyssa's shape: 08:00 on one weekday. 2026-11-22 is a Sunday.
    const auto saturday = CronSchedule::parse("0 8 * * 6");
    REQUIRE(saturday);
    CHECK(saturday->next(utc(2026, 11, 22, 12)) == utc(2026, 11, 28, 8));
    CHECK(saturday->next(utc(2026, 11, 28, 8)) == utc(2026, 11, 28, 8)); // inclusive
    CHECK(saturday->next(utc(2026, 11, 28, 8) + 1) == utc(2026, 12, 5, 8)); // a moment past is past

    // 7 is Sunday as well as 0.
    CHECK(CronSchedule::parse("0 8 * * 7")->next(utc(2026, 11, 22, 9)) == utc(2026, 11, 29, 8));
    CHECK(CronSchedule::parse("0 8 * * SUN")->next(utc(2026, 11, 22, 9)) == utc(2026, 11, 29, 8));

    // Steps, ranges and lists; six fields put seconds first.
    CHECK(CronSchedule::parse("*/15 * * * *")->next(utc(2026, 1, 1, 10, 7)) == utc(2026, 1, 1, 10, 15));
    CHECK(CronSchedule::parse("0 9-17/4 * * *")->next(utc(2026, 1, 1, 10)) == utc(2026, 1, 1, 13));
    CHECK(CronSchedule::parse("30 0 0 1,15 * ?")->next(utc(2026, 1, 2)) == utc(2026, 1, 15, 0, 0, 30));

    // Day of month and day of week must both match, as in Cronos: the 13th on a Friday.
    CHECK(CronSchedule::parse("0 0 13 * 5")->next(utc(2026, 1, 1)) == utc(2026, 2, 13));

    // Leap day, and a date that never comes.
    CHECK(CronSchedule::parse("0 0 29 2 *")->next(utc(2026, 3, 1)) == utc(2028, 2, 29));
    CHECK_FALSE(CronSchedule::parse("0 0 30 2 *")->next(utc(2026, 1, 1)));
}

TEST_CASE("a cron expression Pathing would not read is no schedule", "[behaviors][cron]") {
    CHECK_FALSE(CronSchedule::parse(""));
    CHECK_FALSE(CronSchedule::parse("0 8 * *"));
    CHECK_FALSE(CronSchedule::parse("0 25 * * *"));
    CHECK_FALSE(CronSchedule::parse("0 8 * * 8"));
    CHECK_FALSE(CronSchedule::parse("0 8 L * *")); // Cronos has it; no pack uses it
    CHECK_FALSE(CronSchedule::parse("0 8 * * 5-2"));
    CHECK_FALSE(CronSchedule::parse("? 8 * * *")); // `?` in the day fields only
}

TEST_CASE("a schedule shows its pathable for the duration after each occurrence", "[behaviors][cron]") {
    const CronSchedule hourly = *CronSchedule::parse("0 * * * *");
    const UtcMillis duration = 10 * kMinute;

    // Loaded inside a window: shown at once, as the constructor looks back one duration.
    ScheduleState state;
    CHECK_FALSE(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 5), &state));
    CHECK_FALSE(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 9), &state));
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 11), &state));
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 59), &state));
    CHECK_FALSE(updateSchedule(hourly, duration, utc(2026, 1, 1, 11, 0), &state));
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 11, 10), &state)); // the window is half-open

    // Loaded outside one: hidden until the next.
    ScheduleState later;
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 30), &later));
    CHECK_FALSE(updateSchedule(hourly, duration, utc(2026, 1, 1, 11, 1), &later));
}

TEST_CASE("a schedule moves on past a window no frame saw open", "[behaviors][cron]") {
    const CronSchedule hourly = *CronSchedule::parse("0 * * * *");
    const UtcMillis duration = 10 * kMinute;

    // A frame on the very millisecond the window closes, then the next one after it.
    ScheduleState edge;
    CHECK_FALSE(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 5), &edge));
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 10), &edge));
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 10) + 16, &edge));
    CHECK_FALSE(updateSchedule(hourly, duration, utc(2026, 1, 1, 11, 1), &edge));

    // Not updated at all through a window, as a marker under a category that is off is not.
    ScheduleState skipped;
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 10, 30), &skipped));
    CHECK(updateSchedule(hourly, duration, utc(2026, 1, 1, 11, 30), &skipped));
    CHECK_FALSE(updateSchedule(hourly, duration, utc(2026, 1, 1, 12, 2), &skipped));

    // An expression that never matches stays hidden.
    ScheduleState never;
    CHECK(updateSchedule(*CronSchedule::parse("0 0 30 2 *"), duration, utc(2026, 1, 1), &never));
    CHECK(updateSchedule(*CronSchedule::parse("0 0 30 2 *"), duration, utc(2026, 1, 2), &never));
}

TEST_CASE("the daily and weekly resets fall where Pathing puts them", "[behaviors][states]") {
    CHECK(nextDailyReset(utc(2026, 11, 22, 23, 59)) == utc(2026, 11, 23));
    CHECK(nextDailyReset(utc(2026, 11, 23)) == utc(2026, 11, 24));

    // Monday 07:30 UTC. 2026-11-23 is a Monday.
    CHECK(nextWeeklyReset(utc(2026, 11, 22, 12)) == utc(2026, 11, 23, 7, 30));
    CHECK(nextWeeklyReset(utc(2026, 11, 23, 7)) == utc(2026, 11, 23, 7, 30));
    CHECK(nextWeeklyReset(utc(2026, 11, 23, 8)) == utc(2026, 11, 30, 7, 30));
    CHECK(nextWeeklyReset(utc(2026, 11, 25)) == utc(2026, 11, 30, 7, 30));
}

TEST_CASE("hidden markers wait for their own event", "[behaviors][states]") {
    const viewer::Guid a = viewer::guidFromText("a");
    const viewer::Guid b = viewer::guidFromText("b");
    const UtcMillis now = utc(2026, 1, 1);
    const uint64_t here = instanceKey(15, 3);
    const uint64_t elsewhere = instanceKey(15, 4);
    BehaviorStates states;

    const uint64_t loaded = states.revision();
    states.hide(Mode::ReappearOnMapChange, a, here);
    CHECK(states.hidden(Mode::ReappearOnMapChange, a, here, now));
    CHECK_FALSE(states.hidden(Mode::ReappearOnMapChange, b, here, now));
    states.mapChanged();
    CHECK_FALSE(states.hidden(Mode::ReappearOnMapChange, a, here, now));

    states.hide(Mode::OncePerInstance, a, here);
    CHECK(states.hidden(Mode::OncePerInstance, a, here, now));
    CHECK_FALSE(states.hidden(Mode::OncePerInstance, a, elsewhere, now));
    CHECK(states.revision() == loaded); // only timers are saved

    // Every timed mode shares one set.
    states.hideUntil(b, now + kMinute);
    CHECK(states.revision() != loaded);
    CHECK(states.hidden(Mode::ReappearOnDailyReset, b, here, now));
    CHECK(states.hidden(Mode::ReappearAfterTimer, b, here, now));
    CHECK_FALSE(states.hidden(Mode::ReappearAfterTimer, b, here, now + kMinute));
    CHECK_FALSE(states.hidden(Mode::AlwaysVisible, b, here, now));
    CHECK_FALSE(states.hidden(Mode::ReappearOnMapReset, b, here, now));

    states.hide(Mode::OnlyVisibleBeforeActivation, a, here);
    CHECK(states.hidden(Mode::OnlyVisibleBeforeActivation, a, here, utc(9000, 1, 1)));
    states.clear(a);
    CHECK_FALSE(states.hidden(Mode::OnlyVisibleBeforeActivation, a, here, now));
    CHECK(states.hidden(Mode::OncePerInstance, a, here, now)); // clear takes the first place only, as Pathing's

    states.expire(now + kMinute);
    CHECK_FALSE(states.hidden(Mode::ReappearOnDailyReset, b, here, now));
}

TEST_CASE("timers survive a session in Pathing's file layout", "[behaviors][states]") {
    const corpus::TempDir temp("behavior_timers");
    const viewer::Guid guid = viewer::guidFromText("AAECAwQFBgcICQoLDA0ODw==");
    const std::filesystem::path file = temp / TIMERS_FILE_NAME;

    BehaviorStates states;
    states.hideUntil(guid, utc(2026, 11, 23));
    REQUIRE(states.save(file));

    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    CHECK(line == "03020100-0504-0706-0809-0a0b0c0d0e0f,t2026-11-23T00:00:00");

    // A line from Pathing before 1.1.5, without the "t", and one that is not a timer.
    std::ofstream(file, std::ios::app) << "00000000-0000-0000-0000-000000000001,2030-01-01T00:00:00\r\nnot a timer\n";
    BehaviorStates loaded;
    loaded.load(file);
    CHECK(loaded.hidden(Mode::ReappearOnDailyReset, guid, 0, utc(2026, 11, 22)));
    CHECK_FALSE(loaded.hidden(Mode::ReappearOnDailyReset, guid, 0, utc(2026, 11, 23)));
    CHECK(loaded.hidden(Mode::ReappearAfterTimer, *viewer::guidFromString("00000000-0000-0000-0000-000000000001"), 0, utc(2029, 1, 1)));
}

TEST_CASE("guids read back from their string form", "[behaviors][guid]") {
    const viewer::Guid guid = viewer::guidFromText("AAECAwQFBgcICQoLDA0ODw==");
    CHECK(viewer::guidFromString(viewer::toString(guid)) == guid);
    CHECK_FALSE(viewer::guidFromString("03020100-0504-0706-0809-0a0b0c0d0e0"));
    CHECK_FALSE(viewer::guidFromString("03020100x0504-0706-0809-0a0b0c0d0e0f"));
    CHECK(viewer::xorGuid(guid, guid).empty());
    // A name is hashed whatever it looks like, unlike a guid attribute.
    CHECK(viewer::md5Guid("AAECAwQFBgcICQoLDA0ODw==") != guid);
}

TEST_CASE("script attributes split as Script.SplitFunc splits them", "[behaviors][attributes]") {
    const auto call = parseScriptCall("Teh_Bounce(aqJyflDuJ0a//DEeNbiEQw==, 2, 1.5, true, nil, 'x', \"y\")");
    REQUIRE(call);
    CHECK(call->name == "Teh_Bounce");
    REQUIRE(call->args.size() == 7);
    CHECK(std::get<std::string>(call->args[0]) == "aqJyflDuJ0a//DEeNbiEQw==");
    CHECK(std::get<int>(call->args[1]) == 2);
    CHECK(std::get<float>(call->args[2]) == 1.5f);
    CHECK(std::get<std::string>(call->args[3]) == " true"); // parts are not trimmed, so " true" is no bool
    CHECK(std::get<std::string>(call->args[4]) == " nil");
    CHECK(std::get<std::string>(call->args[5]) == " 'x"); // a leading space keeps the quote
    CHECK(std::get<std::string>(call->args[6]) == " \"y");

    const auto bare = parseScriptCall("combatFade");
    REQUIRE(bare);
    CHECK(bare->name == "combatFade");
    CHECK(bare->args.empty());
    CHECK(parseScriptCall("f()")->args.empty());
    CHECK(std::holds_alternative<bool>(parseScriptCall("f(true)")->args[0]));
    CHECK(std::holds_alternative<std::monostate>(parseScriptCall("f(nil)")->args[0]));
    CHECK(std::get<std::string>(parseScriptCall("f(a.b')")->args[0]) == "a.b'"); // an unquoted dot that is no float: as it is
    CHECK(std::get<std::string>(parseScriptCall("f('a.b')")->args[0]) == "a.b");

    CHECK_FALSE(parseScriptCall("  "));
    CHECK_FALSE(parseScriptCall("f(a,,b)")); // an empty part throws in Pathing, losing the call
}

TEST_CASE("messages format their one value as string.Format does", "[behaviors][attributes]") {
    CHECK(formatMessage(kDefaultCopyMessage, "[&BJMLAAA=]") == "'[&BJMLAAA=]' copied to clipboard.");
    CHECK(formatMessage("Waypoint copied", "x") == "Waypoint copied");
    CHECK(formatMessage("{{0}} is {0}", "x") == "{0} is x");
    CHECK(formatMessage("{0,5}", "x") == "x");
    CHECK(formatMessage("{1} and {0}", "x") == "{1} and {0}"); // no second argument: shown as it is
    CHECK(formatMessage("a } b", "x") == "a } b");
}

TEST_CASE("a marker without behaviors carries none", "[behaviors][attributes]") {
    CHECK(behaviorsOf("behavior_none", "") == nullptr);
    CHECK(behaviorsOf("behavior_zero", "behavior=\"0\" autotrigger=\"1\"") == nullptr);
}

TEST_CASE("filters read their lists as Pathing does", "[behaviors][attributes]") {
    const auto behaviors = behaviorsOf(
        "behavior_filters",
        "profession=\"Thief,mesmer, guardian\" specialization=\"59, 7\" mount=\"skyscale\" festival=\"Halloween, wintersday\"");
    REQUIRE(behaviors);
    // An enum name is not trimmed, so " guardian" is no profession: 0.
    CHECK(*behaviors->professions == std::vector<int>{5, 7, 0});
    CHECK(*behaviors->specializations == std::vector<int>{59, 7});
    CHECK(*behaviors->mounts == std::vector<int>{viewer::enumValue(viewer::Gw2Enum::Mount, "Skyscale")});
    CHECK(*behaviors->festivals == std::vector<std::string>{"halloween", "wintersday"});
    CHECK_FALSE(behaviors->races);
    CHECK_FALSE(behaviors->map_types);
}

TEST_CASE("behaviors inherit through the category", "[behaviors][attributes]") {
    const auto behaviors = behaviorsOf("behavior_inherit", "", "behavior=\"3\" copy=\"[&BJMLAAA=]\" info=\"Hello\"");
    REQUIRE(behaviors);
    CHECK(behaviors->mode == 3);
    REQUIRE(behaviors->copy);
    CHECK(behaviors->copy->value == "[&BJMLAAA=]");
    CHECK(behaviors->copy->message == kDefaultCopyMessage);
    CHECK(behaviors->info == "Hello");
}

TEST_CASE("a schedule needs a positive duration, in minutes", "[behaviors][attributes]") {
    const auto scheduled = behaviorsOf("behavior_schedule", "schedule=\"0 8 * * 6\" schedule-duration=\"80\"");
    REQUIRE(scheduled);
    CHECK(scheduled->schedule);
    CHECK(scheduled->schedule_duration == 80 * kMinute);
    CHECK(behaviorsOf("behavior_schedule_none", "schedule=\"0 8 * * 6\"") == nullptr);
    CHECK(behaviorsOf("behavior_schedule_zero", "schedule=\"0 8 * * 6\" schedule-duration=\"0\"") == nullptr);
}

TEST_CASE("modifiers take their defaults from Pathing", "[behaviors][attributes]") {
    const auto behaviors = behaviorsOf(
        "behavior_modifiers",
        "bounce-height=\"3\" copy-message=\"m\" togglecategory=\"Tw.Guides\" toggle=\"other\" show=\"A.B\" hide=\" \" "
        "resetguid=\"AAECAwQFBgcICQoLDA0ODw==,made-up\" autotrigger=\"true\" invertbehavior=\"yes\" resetlength=\"300\"");
    REQUIRE(behaviors);
    REQUIRE(behaviors->bounce);
    CHECK(behaviors->bounce->mode == BounceMode::Bounce);
    CHECK(behaviors->bounce->height == 3.0f);
    CHECK(behaviors->bounce->duration == 1.0f);
    REQUIRE(behaviors->copy);
    CHECK(behaviors->copy->value.empty());
    CHECK(behaviors->toggle == "tw.guides"); // TacO's spelling wins
    CHECK(behaviors->show == "a.b");
    CHECK(behaviors->hide.empty()); // blank is none
    REQUIRE(behaviors->reset_guids);
    CHECK(behaviors->reset_guids->at(0) == viewer::guidFromText("AAECAwQFBgcICQoLDA0ODw=="));
    CHECK(behaviors->reset_guids->at(1) == viewer::md5Guid("made-up"));
    CHECK(behaviors->auto_trigger);
    CHECK_FALSE(behaviors->invert); // "yes" is not Pathing's true

    const auto rise = behaviorsOf("behavior_rise", "bounce=\"Rise\"");
    CHECK(rise->bounce->mode == BounceMode::Rise);
}

TEST_CASE("a trail takes filters and scripts, and no modifier", "[behaviors][attributes]") {
    const corpus::TempDir temp("behavior_trail");
    std::ofstream(temp / "markers.xml") << R"xml(<OverlayData><MarkerCategory name="c"/><POIs>
        <Trail type="c" trailData="t.trl" mount="skyscale" copy="x" behavior="1" script-tick="Tick()" script-focus="Focus"/>
    </POIs></OverlayData>)xml";
    Loaded loaded;
    std::string error;
    loaded.source = pack::open(temp.path(), &error);
    loaded.model = pack::loadPack(*loaded.source);
    const auto behaviors = bakeBehaviors(loaded.model, loaded.model.pathables.at(0), false);
    REQUIRE(behaviors);
    CHECK(behaviors->mounts);
    CHECK_FALSE(behaviors->copy);
    CHECK(behaviors->mode == 0);
    CHECK(behaviors->tick->name == "Tick");
    CHECK_FALSE(behaviors->focus);
}

TEST_CASE("the festivals on are read from the API and from Blish HUD's list", "[behaviors][festivals]") {
    const auto daily = festivalsFromDailyGroup(R"({"id":"18DB115A-8637-4290-A636-821362A3C4A8","categories":[97, 98, 233, 330]})");
    REQUIRE(daily);
    CHECK(*daily == std::vector<std::string>{"wintersday", "dragonbash"});
    CHECK_FALSE(festivalsFromDailyGroup(R"({"text":"no such id"})"));
    CHECK_FALSE(festivalsFromDailyGroup("not json"));

    const auto listed = festivalsFromList(R"(["Halloween", "lunarnewyear"])");
    REQUIRE(listed);
    CHECK(*listed == std::vector<std::string>{"halloween", "lunarnewyear"});
    CHECK_FALSE(festivalsFromList("{}"));
    CHECK(std::string(festivalDisplayName("festivalofthefourwinds")) == "Festival of the Four Winds");
    CHECK(std::string(festivalDisplayName("carnival")) == "Unknown");
}
