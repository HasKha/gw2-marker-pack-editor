// Behaviors running over a live map, a small pack per rule: filters against the player,
// focus as the player walks into range, and what interacting does — each checked against
// what Pathing's Behavior/ and StandardMarker_Focus.cs do.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "corpus.h"
#include "viewer/behaviors/update.h"
#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

using namespace viewer::behaviors;
using viewer::UtcMillis;

namespace {

constexpr int kMap = 15;
constexpr UtcMillis kNoon = 1795348800000; // 2026-11-22T12:00:00Z, a Sunday

struct Call {
    std::string name;
    Entity entity;
    Hook hook;
    bool flag;
};

// A pack of POIs on kMap, the i-th at x = 100 i so the player stands by one at a time, under
// category "c" — or "c.target" for those whose attributes say so — and everything behaviors
// act on.
struct Behaving {
    Behaving(const std::string& name, const std::vector<std::string>& pois, const std::string& categories = "")
        : temp(name) {
        std::string xml =
            "<OverlayData>\n<MarkerCategory name=\"c\"><MarkerCategory name=\"target\"/></MarkerCategory>\n" + categories + "\n<POIs>\n";
        for (size_t i = 0; i < pois.size(); ++i) {
            xml += "<POI MapID=\"15\" xpos=\"" + std::to_string(100 * i) + "\" ypos=\"0\" zpos=\"0\" type=\"c\" " + pois[i] + "/>\n";
        }
        xml += "</POIs>\n</OverlayData>\n";
        corpus::writeFile(temp / "pack" / "markers.xml", xml);
        viewer::runtime::install(&installed, std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(temp / "pack")));
        live = viewer::runtime::liveMap(installed, kMap);
        mumble.available = true;
        mumble.map_id = kMap;
        mumble.name = "Ada";
        standAt(1000);
    }

    Host host() {
        Host out;
        out.states = &states;
        out.categories = &categories;
        out.tree = &installed.tree;
        out.screen = &screen;
        out.copy = [this](const std::string& text, const std::string& message) {
            copies.push_back(text + " | " + message);
            return true;
        };
        if (scripted) {
            out.script = [this](const ScriptCall& call, Entity entity, Hook hook, bool flag) -> std::optional<bool> {
                calls.push_back({call.name, entity, hook, flag});
                if (hook == Hook::Filter) return script_filters;
                return std::nullopt;
            };
        }
        out.options = options;
        return out;
    }

    Frame frame() const { return Frame{&mumble, now, seconds, &festivals, &account}; }

    void tick() {
        const Host h = host();
        update(&live, viewer::runtime::visibleNodes(installed.tree, categories), h, frame());
    }

    void press() {
        const Host h = host();
        interact(&live, h, frame());
    }

    // The player at POI `index`, metres from nothing else.
    void standAt(size_t index) { mumble.player_position = glm::vec3(100.0f * static_cast<float>(index), 0.0f, 0.0f); }

    const EntityState& state(size_t marker) const { return live.markers.at(marker).behavior; }
    bool filtered(size_t marker) const { return state(marker).filtered; }

    corpus::TempDir temp;
    viewer::runtime::Installed installed;
    viewer::runtime::LiveMap live;
    BehaviorStates states;
    viewer::store::CategoryStates categories;
    Screen screen;
    viewer::MumbleState mumble;
    std::vector<std::string> festivals;
    viewer::account::Progress account;
    Options options;
    UtcMillis now = kNoon;
    double seconds = 100.0;

    bool scripted = false;
    bool script_filters = false;
    std::vector<Call> calls;
    std::vector<std::string> copies;
};

} // namespace

TEST_CASE("character filters hide what is not the player's", "[behaviors]") {
    Behaving world(
        "behave_character",
        {"profession=\"Thief,Mesmer\"", "race=\"Norn\" mount=\"Skyscale\"", "specialization=\"59\" maptype=\"Public\""});
    world.mumble.profession = viewer::enumValue(viewer::Gw2Enum::Profession, "Thief");
    world.tick();
    CHECK_FALSE(world.filtered(0));
    CHECK(world.filtered(1));
    CHECK(world.filtered(2));

    world.mumble.profession = viewer::enumValue(viewer::Gw2Enum::Profession, "Guardian");
    world.mumble.race = viewer::enumValue(viewer::Gw2Enum::Race, "Norn");
    world.mumble.mount = viewer::enumValue(viewer::Gw2Enum::Mount, "Skyscale");
    world.mumble.specialization = 59;
    world.mumble.map_type = viewer::enumValue(viewer::Gw2Enum::MapType, "Public");
    world.tick();
    CHECK(world.filtered(0));
    CHECK_FALSE(world.filtered(1));
    CHECK_FALSE(world.filtered(2));

    // With markers not allowed to hide themselves, nothing is.
    world.options.auto_hide = false;
    world.tick();
    CHECK_FALSE(world.filtered(0));
}

TEST_CASE("a festival filter shows only while the festival is on", "[behaviors]") {
    Behaving world("behave_festival", {"festival=\"halloween,wintersday\""});
    world.tick();
    CHECK(world.filtered(0)); // nothing is on until the answer is in
    world.festivals = {"wintersday"};
    world.tick();
    CHECK_FALSE(world.filtered(0));
    world.festivals = {"dragonbash"};
    world.tick();
    CHECK(world.filtered(0));
    CHECK(filterReason(world.live.markers[0]) == "Hidden because the Halloween, Wintersday festival isn't active.");
}

TEST_CASE("a raid filter hides a marker until its raids are known complete", "[behaviors]") {
    // Hero's pack draws a boss done this week as a coloured icon carrying `raid`, over a grey
    // one without. With no completions to go by, Pathing hides every marker naming a raid —
    // even one naming none, since its split of "" is one empty name.
    Behaving world("behave_raid", {"raid=\"Vale_Guardian, spirit_woods\"", "raid=\"\"", ""});
    world.tick();
    CHECK(world.filtered(0));
    CHECK(world.filtered(1));
    CHECK_FALSE(world.filtered(2));
    CHECK(filterReason(world.live.markers[0]) == "Hidden until you've completed this week: vale_guardian, spirit_woods.");

    // With the account's kills from the API, a marker shows once every raid it names is done.
    world.account.raids = std::set<std::string>{"vale_guardian"};
    world.tick();
    CHECK(world.filtered(0));
    world.account.raids->insert("spirit_woods");
    world.tick();
    CHECK_FALSE(world.filtered(0));
    CHECK(world.filtered(1)); // "" is never killed
}

TEST_CASE("an achievement filter hides what the account has done, and what was interacted with", "[behaviors]") {
    Behaving world(
        "behave_achievement",
        {"achievementid=\"100\"", "achievementid=\"200\" achievementbit=\"2\"", "achievementid=\"200\" achievementbit=\"5\"", ""});
    world.tick();
    for (size_t i = 0; i < 3; ++i) CHECK_FALSE(world.filtered(i)); // nothing known of the account

    world.account.achievements[100] = {true, {}};
    world.account.achievements[200] = {false, {2}};
    world.tick();
    CHECK(world.filtered(0));
    CHECK(world.filtered(1));
    CHECK_FALSE(world.filtered(2));
    CHECK(filterReason(world.live.markers[0]) == "Hidden because you've completed the achievement associated with this marker.");

    // Focused, it offers to hide itself; interacting hides it until the map is loaded again.
    world.standAt(2);
    world.tick();
    REQUIRE(world.screen.prompt);
    CHECK(world.screen.prompt->text == "Hide achievement marker until map change {0}");
    world.press();
    world.tick();
    CHECK(world.filtered(2));
    CHECK_FALSE(world.screen.prompt);
}

TEST_CASE("walking into range focuses a marker, and out of it lets go", "[behaviors]") {
    Behaving world("behave_focus", {"info=\"Hello\" copy=\"x\"", "triggerrange=\"10\" info=\"Far\""});
    world.tick();
    CHECK_FALSE(world.state(0).focused);
    CHECK(world.screen.info.empty());

    world.standAt(0);
    world.tick();
    CHECK(world.state(0).focused);
    CHECK(world.screen.info == std::vector<std::string>{"Hello"});
    REQUIRE(world.screen.prompt);
    CHECK(world.screen.prompt->marker == 0);
    CHECK(world.screen.prompt->text == "Copy 'x' to clipboard {0}");

    // Distance leaves the height offset out, as Position does: 1.5 up is still standing on it.
    world.mumble.player_position = glm::vec3(0.0f, 0.0f, 1.9f);
    world.tick();
    CHECK(world.state(0).focused);

    world.mumble.player_position = glm::vec3(95.0f, 0.0f, 0.0f); // 5 from the second, inside its 10
    world.tick();
    CHECK_FALSE(world.state(0).focused);
    CHECK(world.state(1).focused);
    CHECK(world.screen.info == std::vector<std::string>{"Far"});
    CHECK_FALSE(world.screen.prompt);
}

TEST_CASE("info closed with its X stays closed until the info changes", "[behaviors]") {
    Behaving world("behave_info_closed", {"info=\"Near\"", "triggerrange=\"10\" info=\"Far\""});
    world.standAt(0);
    world.tick();
    world.screen.info_closed = true;
    world.tick();
    CHECK(world.screen.info_closed);

    // Out of the first's range and into the second's: Pathing's InfoWindow shows again on either.
    world.mumble.player_position = glm::vec3(95.0f, 0.0f, 0.0f);
    world.tick();
    CHECK(world.screen.info == std::vector<std::string>{"Far"});
    CHECK_FALSE(world.screen.info_closed);
}

TEST_CASE("the behavior attribute hides a marker it was interacted with", "[behaviors]") {
    Behaving world(
        "behave_modes",
        {"behavior=\"1\"", "behavior=\"3\"", "behavior=\"4\" resetlength=\"60\"", "behavior=\"6\"", "behavior=\"101\""});
    for (size_t i = 0; i < 5; ++i) {
        world.standAt(i);
        world.tick();
        world.press();
        world.tick();
        CHECK(world.filtered(i));
    }
    CHECK(filterReason(world.live.markers[0]) == "Hidden until the next map change because you interacted with it.");

    world.states.mapChanged();
    world.mumble.shard_id = 7;
    world.now += 61'000;
    world.tick();
    CHECK_FALSE(world.filtered(0)); // a map change
    CHECK(world.filtered(1)); // forever
    CHECK_FALSE(world.filtered(2)); // its 60 seconds are up
    CHECK_FALSE(world.filtered(3)); // another instance
    CHECK(world.filtered(4)); // until Monday

    world.now = kNoon + 20 * 3600 * 1000; // Monday 08:00
    world.tick();
    CHECK_FALSE(world.filtered(4));
}

TEST_CASE("once daily per character is per character", "[behaviors]") {
    Behaving world("behave_character_daily", {"behavior=\"7\""});
    world.standAt(0);
    world.tick();
    REQUIRE(world.screen.prompt);
    CHECK(world.screen.prompt->text == "Hide marker for this character until daily reset {0}");
    world.press();
    world.tick();
    CHECK(world.filtered(0));
    world.mumble.name = "Grace";
    world.tick();
    CHECK_FALSE(world.filtered(0));
}

TEST_CASE("invertbehavior shows a marker only once it is hidden", "[behaviors]") {
    Behaving world(
        "behave_invert",
        {"behavior=\"1\" invertbehavior=\"1\" GUID=\"AAECAwQFBgcICQoLDA0ODw==\"", "resetguid=\"AAECAwQFBgcICQoLDA0ODw==\""});
    world.tick();
    CHECK(world.filtered(0));
    world.states.hide(Mode::ReappearOnMapChange, world.live.markers[0].marker.guid, 0);
    world.tick();
    CHECK_FALSE(world.filtered(0));

    // A resetguid marker shows it again — here, hides it.
    world.standAt(1);
    world.tick();
    REQUIRE(world.screen.prompt);
    CHECK(world.screen.prompt->text == "Will unhide 1 marker {0}");
    world.press();
    world.tick();
    CHECK(world.filtered(0));
}

TEST_CASE("copy follows the clipboard consent and debounces what triggers itself", "[behaviors]") {
    Behaving world("behave_copy", {"copy=\"[&amp;BJMLAAA=]\" copy-message=\"Copied {0}\"", "copy=\"auto\" autotrigger=\"1\""});
    world.standAt(0);
    world.tick();
    world.press();
    CHECK(world.copies == std::vector<std::string>{"[&BJMLAAA=] | Copied [&BJMLAAA=]"});

    // An auto-triggered copy: once on arrival, again only after eight seconds.
    world.copies.clear();
    world.standAt(1);
    world.tick();
    CHECK(world.copies == std::vector<std::string>{"auto | 'auto' copied to clipboard."});
    world.standAt(0);
    world.tick();
    world.standAt(1);
    world.seconds += 5.0;
    world.tick();
    CHECK(world.copies.size() == 1);
    world.standAt(0);
    world.tick();
    world.standAt(1);
    world.seconds += 5.0;
    world.tick();
    CHECK(world.copies.size() == 2);

    // Only when interacted with: the key still copies, arriving does not.
    world.copies.clear();
    world.options.clipboard = ClipboardConsent::OnlyWhenInteractedWith;
    world.standAt(0);
    world.tick();
    world.standAt(1);
    world.seconds += 10.0;
    world.tick();
    CHECK(world.copies.empty());
    world.press();
    CHECK(world.copies.size() == 1);

    world.options.clipboard = ClipboardConsent::Never;
    world.press();
    CHECK(world.copies.size() == 1);
}

TEST_CASE("toggle, show and hide change categories", "[behaviors]") {
    Behaving world(
        "behave_categories",
        {"toggle=\"C.Target\"", "hide=\"c.target\"", "show=\"c.target\"", "type=\"c.target\" info=\"Inside\""});
    // The fourth POI's type wins over the fixture's, as the last attribute does.
    const auto on = [&] { return world.categories.enabled("c.target", true); };
    world.standAt(0);
    world.tick();
    world.press();
    CHECK_FALSE(on());
    world.press();
    CHECK(on());
    world.standAt(1);
    world.tick();
    world.press();
    CHECK_FALSE(on());
    world.standAt(2);
    world.tick();
    world.press();
    CHECK(on());
}

TEST_CASE("a category turned off lets go of its markers", "[behaviors]") {
    Behaving world("behave_inactive", {"type=\"c.target\" info=\"Inside\""});
    world.standAt(0);
    world.tick();
    CHECK(world.state(0).focused);
    world.categories.set("c.target", false, true);
    world.tick();
    CHECK_FALSE(world.state(0).focused);
    CHECK(world.screen.info.empty());
}

TEST_CASE("hidden markers shown faded can be unhidden", "[behaviors]") {
    Behaving world("behave_ghost", {"behavior=\"3\" info=\"Hello\""});
    world.standAt(0);
    world.tick();
    world.press();
    world.tick();
    REQUIRE(world.filtered(0));

    // Off: a hidden marker is not focused into anything.
    world.standAt(1);
    world.tick();
    world.standAt(0);
    world.tick();
    CHECK(world.screen.info.empty());
    CHECK_FALSE(world.screen.prompt);

    world.options.show_hidden = true;
    world.standAt(1);
    world.tick();
    world.standAt(0);
    world.tick();
    REQUIRE(world.screen.prompt);
    CHECK(world.screen.prompt->unhides);
    CHECK(
        world.screen.prompt->text ==
        "Hidden permanently because you interacted with it.\n\nPress {0} or click this gear to unhide the marker.");

    world.press();
    world.tick();
    CHECK_FALSE(world.filtered(0));
    CHECK_FALSE(world.states.hidden(Mode::OnlyVisibleBeforeActivation, world.live.markers[0].marker.guid, 0, world.now));
    // Its filters are gone with the unhiding: interacting again hides nothing.
    world.press();
    world.tick();
    CHECK_FALSE(world.filtered(0));
}

TEST_CASE("a bounce rises while focused and falls back after", "[behaviors]") {
    Behaving world(
        "behave_bounce",
        {"bounce-height=\"2\" bounce-duration=\"1\"", "bounce=\"rise\" bounce-height=\"3\" bounce-delay=\"1\""});
    const auto offset = [&](size_t marker) { return bounceOffset(world.live.markers[marker], world.seconds); };
    CHECK(offset(0) == 0.0f);

    world.standAt(0);
    world.tick();
    world.seconds += 0.5;
    CHECK(offset(0) == 1.0f); // halfway up a quad ease
    world.seconds += 0.5;
    CHECK(offset(0) == 2.0f);
    world.seconds += 1.0;
    CHECK(offset(0) == 0.0f); // and down again
    world.seconds += 0.5;
    CHECK(offset(0) == 1.0f);

    world.standAt(5);
    world.tick();
    CHECK(offset(0) == 1.0f); // falls from where it was
    world.seconds += 1.0;
    CHECK(offset(0) == 0.0f);

    world.standAt(1);
    world.tick();
    world.seconds += 1.0;
    CHECK(offset(1) == 0.0f); // its delay
    world.seconds += 1.0;
    CHECK(offset(1) == 3.0f);
    world.seconds += 5.0;
    CHECK(offset(1) == 3.0f); // a rise stays up

    // Animation off: nothing moves.
    world.options.animate = false;
    world.standAt(5);
    world.tick();
    world.seconds += 2.0;
    world.standAt(0);
    world.tick();
    world.seconds += 1.0;
    CHECK(offset(0) == 0.0f);
}

TEST_CASE("script behaviors call their functions as Pathing's Script does", "[behaviors]") {
    Behaving world(
        "behave_script",
        {"script-once=\"Once\" script-tick=\"Tick(1)\" script-filter=\"Filter\" script-focus=\"Focus\" script-trigger=\"Trigger\""});
    world.scripted = true;
    world.tick();
    world.tick();
    REQUIRE(world.calls.size() == 5); // once, tick, filter; tick, filter
    CHECK(world.calls[0].name == "Once");
    CHECK(world.calls[1].hook == Hook::Tick);
    CHECK(world.calls[2].hook == Hook::Filter);
    CHECK(world.calls[3].hook == Hook::Tick);
    CHECK_FALSE(world.filtered(0));

    world.script_filters = true;
    world.tick();
    CHECK(world.filtered(0));
    CHECK(filterReason(world.live.markers[0]) == "Hidden by a script.");

    world.script_filters = false;
    world.calls.clear();
    world.standAt(0);
    world.tick();
    REQUIRE(world.calls.size() == 3);
    CHECK(world.calls[2].hook == Hook::Focus);
    CHECK(world.calls[2].flag);
    REQUIRE(world.screen.prompt);
    CHECK(world.screen.prompt->text == "Will trigger a script {0}");

    world.calls.clear();
    world.press();
    REQUIRE(world.calls.size() == 1);
    CHECK(world.calls[0].hook == Hook::Trigger);
    CHECK_FALSE(world.calls[0].flag); // pressed, not auto-triggered

    world.calls.clear();
    world.standAt(5);
    world.tick();
    CHECK(world.calls.back().hook == Hook::Focus);
    CHECK_FALSE(world.calls.back().flag);
}

TEST_CASE("a schedule filters outside its window", "[behaviors]") {
    // 12:00 every day for 30 minutes; it is 12:00 now.
    Behaving world("behave_schedule", {"schedule=\"0 12 * * *\" schedule-duration=\"30\""});
    world.tick();
    CHECK_FALSE(world.filtered(0));
    world.now += 31 * 60'000;
    world.tick();
    CHECK(world.filtered(0));
}

TEST_CASE("humanize writes the largest units that are not zero", "[behaviors]") {
    CHECK(humanize(300, 4) == "5 minutes");
    CHECK(humanize(90061, 4) == "1 day, 1 hour, 1 minute, 1 second");
    CHECK(humanize(90061, 2) == "1 day, 1 hour");
    CHECK(humanize(3600 * 24 * 8, 4) == "1 week, 1 day");
}

TEST_CASE("the engine calls script-* functions by name, ignoring case", "[behaviors][script]") {
    Behaving world("behave_engine", {"GUID=\"AAECAwQFBgcICQoLDA0ODw==\""});
    viewer::store::KeyValues storage;
    viewer::script::Host host;
    host.installed = &world.installed;
    host.live = &world.live;
    host.category_states = &world.categories;
    host.storage = &storage;
    host.mumble = &world.mumble;
    viewer::script::Engine engine(host);
    REQUIRE(engine.run(0, "test.lua", R"(
        function IsMine(marker, wanted, label) return marker.Guid:ToBase64() == wanted and label == 'x' end
        function Count(marker, focused) count = (count or 0) + 1; seen = focused end
        function Ticked(marker, time) return time.TotalGameTime.TotalSeconds end
        function Broken() error_here() end
    )"));

    const auto call = [&](const std::string& text, Hook hook, bool flag = false) {
        return engine.callBehavior(*parseScriptCall(text), Entity{false, 0}, hook, flag);
    };
    CHECK(call("IsMine(AAECAwQFBgcICQoLDA0ODw==,'x')", Hook::Filter) == true);
    CHECK(call("ismine(nope,'x')", Hook::Filter) == false);
    CHECK_FALSE(call("Count", Hook::Focus, true)); // returned nothing
    CHECK(call("Ticked", Hook::Tick) == false); // a GameTime of 0 seconds
    engine.tick(12.0, 0.5);
    CHECK(call("Ticked", Hook::Tick) == true);
    CHECK_FALSE(call("Missing", Hook::Filter));

    const size_t errors = engine.console.errors();
    CHECK_FALSE(call("Broken", Hook::Trigger));
    CHECK(engine.console.errors() == errors + 1);
}

TEST_CASE("scripts reach a marker's behaviors through GetBehavior", "[behaviors][script]") {
    Behaving world(
        "behave_get_behavior",
        {"GUID=\"AAECAwQFBgcICQoLDA0ODw==\" info=\"Before\" bounce-height=\"2\" mount=\"Skyscale\" behavior=\"4\" resetlength=\"30\""});
    viewer::store::KeyValues storage;
    viewer::script::Host host;
    host.installed = &world.installed;
    host.live = &world.live;
    host.category_states = &world.categories;
    host.storage = &storage;
    host.mumble = &world.mumble;
    viewer::script::Engine engine(host);
    const auto eval = [&](const std::string& expression) {
        engine.console.clear();
        REQUIRE(engine.run(0, "test.lua", "local m = World:MarkerByGuid('AAECAwQFBgcICQoLDA0ODw==')\nprint(" + expression + ")"));
        return engine.console.messages().back().text;
    };

    CHECK(eval("m:GetBehavior('InfoModifier').InfoValue") == "Before");
    CHECK(eval("m:GetBehavior('bouncemodifier').BounceHeight") == "2");
    CHECK(eval("m:GetBehavior('CopyModifier') == nil") == "True");
    CHECK(eval("m:GetBehavior('MountFilter'):IsFiltered()") == "True");
    world.mumble.mount = viewer::enumValue(viewer::Gw2Enum::Mount, "Skyscale");
    CHECK(eval("m:GetBehavior('MountFilter'):IsFiltered()") == "False");
    CHECK(eval("m:GetBehavior('AchievementFilter') == nil") == "True");
    CHECK(eval("m.ResetLength") == "30");
    CHECK(eval("m.Focused") == "False");

    // Focused, then the script changes what it says: the screen follows.
    world.standAt(0);
    world.tick();
    CHECK(eval("m.Focused") == "True");
    CHECK(world.screen.info == std::vector<std::string>{"Before"});
    eval("(function() m:GetBehavior('InfoModifier').InfoValue = 'After' end)()");
    world.tick();
    CHECK(world.screen.info == std::vector<std::string>{"After"});

    // The live marker's own: the pack's baked marker keeps what the file says.
    const viewer::BakedMarker& baked = world.installed.packs[0]->loaded->pathables.markers.at(0);
    CHECK(*baked.behaviors->info == "Before");

    // ResetLength is what the timer hides for.
    eval("(function() m.ResetLength = 120 end)()");
    world.press();
    world.now += 60'000;
    world.tick();
    CHECK(world.filtered(0));
}
