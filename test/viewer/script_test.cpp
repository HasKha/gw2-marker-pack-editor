// The script engine against a small pack of its own: the call styles, the census, the
// sandbox, errors staying inside Lua, and each part of the Pathing API scripts lean on.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>
#include <string>

#include "corpus.h"
#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

namespace {

using viewer::script::Level;

constexpr int kMap = 15;

// Two categories, a parent and its child, and three markers: two on the map, one of them a
// child's with a height offset, and one elsewhere.
constexpr const char* kPackXml = R"(<OverlayData>
  <MarkerCategory name="Parent" DisplayName="The parent" iconFile="parent.png">
    <MarkerCategory name="Child" iconFile="child.png" heightOffset="2"/>
  </MarkerCategory>
  <POIs>
    <POI MapID="15" xpos="1" ypos="2" zpos="3" type="parent" GUID="AAAAAAAAAAAAAAAAAAAAAA=="/>
    <POI MapID="15" xpos="10" ypos="20" zpos="30" type="parent.child" GUID="AQAAAAAAAAAAAAAAAAAAAA=="/>
    <POI MapID="16" xpos="0" ypos="0" zpos="0" type="parent"/>
  </POIs>
</OverlayData>)";

// One installed pack, the live map of kMap, and an engine over them.
struct Scripted {
    explicit Scripted(const std::string& name, std::string_view pack_lua = {})
        : temp(name) {
        corpus::writeFile(temp / "pack" / "markers.xml", kPackXml);
        corpus::writeFile(temp / "pack" / "scripts" / "Counted.lua", "required = (required or 0) + 1\n");
        if (!pack_lua.empty()) corpus::writeFile(temp / "pack" / "pack.lua", pack_lua);

        viewer::runtime::install(&installed, std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(temp / "pack")));
        live = viewer::runtime::liveMap(installed, kMap);
        mumble.available = true;
        mumble.map_id = kMap;

        viewer::script::Host host;
        host.installed = &installed;
        host.live = &live;
        host.category_states = &states;
        host.storage = &storage;
        host.mumble = &mumble;
        host.set_clipboard = [this](const std::string& text, const std::string&) {
            clipboard = text;
            return true;
        };
        host.call_budget = std::chrono::milliseconds(50);
        engine = std::make_unique<viewer::script::Engine>(host);
    }

    bool run(std::string_view code) { return engine->run(0, "test.lua", code); }

    // Runs `expression` and returns what it printed: the shortest way to read a value out.
    std::string eval(const std::string& expression) {
        engine->console().clear();
        REQUIRE(run("print(" + expression + ")"));
        return engine->console().messages().back().text;
    }

    size_t errors() const { return engine->console().errors(); }

    corpus::TempDir temp;
    viewer::runtime::Installed installed;
    viewer::runtime::LiveMap live;
    viewer::store::CategoryStates states;
    viewer::store::KeyValues storage;
    viewer::MumbleState mumble;
    std::string clipboard;
    std::unique_ptr<viewer::script::Engine> engine;
};

} // namespace

TEST_CASE("a method works called with a colon or a dot", "[viewer][script]") {
    Scripted s("script_call_styles");
    CHECK(s.eval(R"(World:CategoryByType("parent") == World.CategoryByType("parent"))") == "True");
    CHECK(s.eval("I:Vector3(3, 4, 0):Length()") == "5");
    CHECK(s.eval("I.Vector3(3, 4, 0).Length()") == "5");
    // The static form, with both vectors given, is not mistaken for the instance one.
    CHECK(s.eval("I:Vector3(1, 0, 0):Dot(I:Vector3(2, 0, 0))") == "2");
    CHECK(s.eval("I:Vector3(9, 9, 9).Dot(I:Vector3(1, 0, 0), I:Vector3(2, 0, 0))") == "2");
}

TEST_CASE("a member the API lacks reads as nil and is counted once, with its file and line", "[viewer][script]") {
    Scripted s("script_census");
    CHECK(s.run("for i = 1, 3 do\n  x = Mumble.PlayerCharacter.NoSuchMember\nend\nMumble.UI.AlsoMissing = 1"));
    CHECK(s.errors() == 0);
    CHECK(s.eval("x == nil") == "True");

    const auto& entries = s.engine->census().entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].member == "PlayerCharacter.NoSuchMember");
    CHECK(entries[0].source == "pack/test.lua");
    CHECK(entries[0].line == 2);
    CHECK(entries[1].member == "UI.AlsoMissing");
    CHECK(entries[1].line == 4);
}

TEST_CASE("a script error is reported with its file and line, and nothing else stops", "[viewer][script]") {
    Scripted s("script_errors");
    // Two errors for one failed load, as Pathing reports it: what went wrong, then which file.
    // a + nil raises in NeoLua too, where nil + a is a.
    CHECK_FALSE(s.run("local a = 1\nlocal b = a + nil"));
    REQUIRE(s.errors() == 2);
    CHECK_THAT(
        s.engine->console().messages()[s.engine->console().messages().size() - 2].text,
        Catch::Matchers::ContainsSubstring("test.lua:2:"));

    // An error a binding raises is located at the script's call too.
    CHECK_FALSE(s.run("\n\nI:Vector3('x', 0, 0)"));
    CHECK_THAT(
        s.engine->console().messages()[s.engine->console().messages().size() - 2].text,
        Catch::Matchers::ContainsSubstring("test.lua:3:") && Catch::Matchers::ContainsSubstring("bad argument"));

    CHECK(s.run("ok = true"));
    CHECK(s.eval("ok") == "True");
}

TEST_CASE("a script stuck in a loop is stopped", "[viewer][script]") {
    Scripted s("script_budget");
    CHECK_FALSE(s.run("while true do end"));
    CHECK_THAT(
        s.engine->console().messages()[s.engine->console().messages().size() - 2].text,
        Catch::Matchers::ContainsSubstring("script stopped"));
    CHECK(s.run("after = 1"));
}

TEST_CASE("the sandbox is Pathing's: no io, no loading code, no debug", "[viewer][script]") {
    Scripted s("script_sandbox");
    for (const char* name : {"io", "debug", "load", "loadfile", "dofile", "package", "collectgarbage", "pcall", "error", "utf8"}) {
        INFO(name);
        CHECK(s.eval(name) == "");
    }
    CHECK(s.eval("os.execute") == "");
    CHECK(s.eval("type(string.format)") == "function");
    CHECK(s.eval("PathingVersion") == viewer::script::kPathingVersion);
    CHECK(s.eval(R"(Pathing:IsVersionAtLeast("1.10.5"))") == "True");
    CHECK(s.eval(R"(Pathing:IsVersionAtLeast("99.0.0"))") == "False");
    CHECK(s.eval(R"(Pathing:IsVersionAtLeast("not a version"))") == "False");
}

TEST_CASE("math is NeoLua's, with what 5.4 dropped", "[viewer][script][regression]") {
    // Teh's Trails' heart follower turns with math.atan2 every tick. Without it the tick errored,
    // the pack's one OnTick handler was dropped, and every animation it ran stopped with it.
    Scripted s("script_math");
    CHECK(s.eval("math.atan2(1, 1) == math.pi / 4") == "True");
    CHECK(s.eval("math.pow(2, 10)") == "1024");
    CHECK(s.eval("math.ldexp(0.5, 3)") == "4");
    CHECK(s.eval("select(2, math.frexp(8))") == "4");
    CHECK(s.eval("math.cosh(0) + math.sinh(0) + math.tanh(0)") == "1");
    CHECK(s.eval("math.e == math.exp(1)") == "True");
    CHECK(s.errors() == 0);
}

TEST_CASE("a library function the sandbox lacks is counted by the census", "[viewer][script]") {
    Scripted s("script_library_census");
    CHECK(s.run("x = math.nosuch\ny = string.nosuch\nz = ('text').alsonot"));
    const auto& entries = s.engine->census().entries();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].member == "math.nosuch");
    CHECK(entries[0].line == 1);
    CHECK(entries[1].member == "string.nosuch");
    CHECK(entries[2].member == "string.alsonot");
    CHECK(s.eval("('text'):upper()") == "TEXT");
}

TEST_CASE("the deprecated Packs is World, with one warning", "[viewer][script]") {
    Scripted s("script_packs");
    CHECK(s.eval("Packs == World") == "True");
    CHECK(s.eval("Packs == World") == "True");
    size_t warnings = 0;
    for (const auto& message : s.engine->console().messages()) warnings += message.text.find("`Packs` is deprecated") != std::string::npos;
    CHECK(warnings <= 1);
}

TEST_CASE("Pack:Require runs a file of the pack once, whatever its case", "[viewer][script]") {
    Scripted s("script_require");
    CHECK(s.run(R"(Pack:Require("scripts/counted") Pack:Require("Scripts/COUNTED.lua") require("scripts/counted", Pack))"));
    CHECK(s.eval("required") == "1");
    CHECK(s.eval(R"(Pack:Require("scripts/missing"))") == "");
    CHECK(s.errors() == 0);
}

// Scripts load on the frame, on every map change, and a frame never reads the disk: what they
// run is what was read with the pack.
TEST_CASE("pack.lua and what it requires come from the pack as it was read", "[viewer][script][regression]") {
    Scripted s("script_read_with_pack", "loaded = true\nPack:Require(\"scripts/counted\")\n");
    std::filesystem::remove_all(s.temp / "pack");
    s.engine->loadPacks();
    CHECK(s.eval("loaded") == "True");
    CHECK(s.eval("required") == "1");
    CHECK(s.errors() == 0);
}

TEST_CASE("pack.lua runs with Pack, and its OnTick handlers every tick", "[viewer][script]") {
    Scripted s("script_pack_lua", R"(
ticks = 0
Event:OnTick(function(gameTime) ticks = ticks + 1 last = gameTime.TotalGameTime.TotalSeconds end)
Event:OnTick(function() error_here() end)
)");
    s.engine->loadPacks();
    s.engine->tick(1.5, 0.016);
    s.engine->tick(2.5, 0.016);
    CHECK(s.eval("ticks") == "2");
    CHECK(s.eval("last") == "2.5");
    // The failing handler ran once and was dropped; the other kept going.
    CHECK(s.errors() == 1);
}

TEST_CASE("a marker reads and writes in Blish's space, without its height offset", "[viewer][script]") {
    Scripted s("script_marker");
    REQUIRE(s.live.markers.size() == 2);
    CHECK(s.eval(R"(World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA==").Position.Z)") == "2");
    CHECK(s.eval(R"(World:MarkerByGuid("AQAAAAAAAAAAAAAAAAAAAA==").Position.Z)") == "20");
    CHECK(s.eval(R"(World:MarkerByGuid("AQAAAAAAAAAAAAAAAAAAAA==").HeightOffset)") == "2");

    CHECK(s.run(R"(World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA=="):SetPos(4, 5, 6))"));
    // In the file's space, Y up, with the default heightOffset of 1.5 still on top.
    CHECK(s.live.markers[0].marker.position == glm::vec3(4, 6 + 1.5f, 5));
    CHECK(s.run(R"(World:MarkerByGuid("AQAAAAAAAAAAAAAAAAAAAA=="):SetPosZ(0))"));
    CHECK(s.live.markers[1].marker.position.y == 2.0f);

    CHECK(s.run(
        R"(local m = World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA==") m.InGameVisibility = false m.Size = 3 m.Tint = I:Color(300, -5, 10))"));
    CHECK_FALSE(s.live.markers[0].marker.in_game_visibility);
    CHECK(s.live.markers[0].marker.icon_size == 3.0f);
    CHECK(s.live.markers[0].marker.tint == IM_COL32(255, 0, 10, 255));

    // The map's size, which Teh's Trails pulses its highlighted waypoint with.
    CHECK(s.run(R"(local m = World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA==") m.MapDisplaySize = 75 m.ScaleOnMapWithZoom = false)"));
    CHECK(s.live.markers[0].marker.map_display_size == 75.0f);
    CHECK_FALSE(s.live.markers[0].marker.scale_on_map_with_zoom);
    CHECK(s.engine->census().entries().empty());

    // nil is a value type's default, as NeoLua converts it; RotationXyz alone is nullable.
    CHECK(s.run(R"(World:MarkerByGuid("AQAAAAAAAAAAAAAAAAAAAA==").Tint = nil)"));
    CHECK(s.live.markers[1].marker.tint == IM_COL32(0, 0, 0, 0));
    CHECK(s.eval(R"(World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA==").RotationXyz)") == "");
    CHECK(s.run(R"(World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA=="):SetRotZ(1))"));
    CHECK(s.live.markers[0].marker.has_rotation);
    CHECK(s.live.markers[0].marker.rotation == glm::vec3(0, 0, 1));

    CHECK(s.run(R"(World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA=="):Remove())"));
    CHECK(s.live.markers[0].removed);
    CHECK(s.eval(R"(World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA=="))") == "");
    CHECK(s.errors() == 0);
}

TEST_CASE("a marker, and its guid, are one object however often a script asks", "[viewer][script]") {
    Scripted s("script_identity");
    CHECK(s.run(R"(
local guid = "AAAAAAAAAAAAAAAAAAAAAA=="
seen = {}
seen[World:MarkerByGuid(guid)] = 1
seen[World:MarkerByGuid(guid).Guid] = 2
same = rawequal(World:MarkerByGuid(guid), World:MarkerByGuid(guid))
)"));
    CHECK(s.eval(R"(seen[World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA==")])") == "1");
    CHECK(s.eval(R"(seen[I:Guid("AAAAAAAAAAAAAAAAAAAAAA==")])") == "2");
    CHECK(s.eval("same") == "True");
    CHECK(s.eval(R"(World:MarkerByGuid("AAAAAAAAAAAAAAAAAAAAAA==").Guid:ToBase64())") == "AAAAAAAAAAAAAAAAAAAAAA==");
    CHECK(s.eval(R"("g=" .. I:Guid("AQAAAAAAAAAAAAAAAAAAAA=="))") == "g=00000001-0000-0000-0000-000000000000");
}

TEST_CASE("categories: names as written, markers under them, visibility through the player's states", "[viewer][script]") {
    Scripted s("script_category");
    CHECK(s.eval(R"(World:CategoryByType("PARENT.child").Namespace)") == "Parent.Child");
    CHECK(s.eval(R"(World:CategoryByType("parent").DisplayName)") == "The parent");
    CHECK(s.eval(R"(World:CategoryByType("parent.child").Parent == World:CategoryByType("parent"))") == "True");
    CHECK(s.eval(R"(World:CategoryByType("parent").Parent.Root)") == "True");
    CHECK(s.eval(R"(World:CategoryByType("nothing"))") == "");

    CHECK(s.eval(R"(#World:CategoryByType("parent"):GetMarkers())") == "1");
    CHECK(s.eval(R"(#World:CategoryByType("parent"):GetMarkers(true))") == "2");
    CHECK(s.eval("#World.RootCategory:GetMarkers(true)") == "2");

    CHECK(s.run(R"(World:CategoryByType("parent.child"):Hide())"));
    CHECK_FALSE(s.states.enabled("parent.child", true));
    CHECK(s.eval(R"(World:CategoryByType("parent.child"):IsVisible())") == "False");
    CHECK(s.eval(R"(World:CategoryByType("parent"):IsVisible())") == "True");
}

TEST_CASE("Pack:CreateMarker makes a marker that inherits from its pack's category", "[viewer][script]") {
    Scripted s("script_create");
    CHECK(s.run(R"(made = Pack:CreateMarker({ xpos = 1, ypos = 2.5, zpos = 3, type = "parent.child", AutoTrigger = true }))"));
    REQUIRE(s.live.markers.size() == 3);
    const viewer::runtime::LiveMarker& made = s.live.markers.back();
    CHECK(made.marker.icon == "child.png");
    CHECK(made.marker.map_id == kMap);
    CHECK(made.marker.height_offset == 2.0f);
    CHECK(made.node == s.installed.tree.find("parent.child"));
    CHECK(s.eval("made.Position.Z") == "2.5");
    CHECK(s.eval(R"(#World:CategoryByType("parent.child"):GetMarkers())") == "2");
    CHECK(s.errors() == 0);
}

TEST_CASE("vectors add, subtract, scale and compare by value", "[viewer][script]") {
    Scripted s("script_vectors");
    CHECK(s.eval("tostring(I:Vector3(1, 2, 3) + I:Vector3(1, 1, 1))") == "{X:2 Y:3 Z:4}");
    CHECK(s.eval("(I:Vector3(4, 0, 0) - I:Vector3(1, 0, 0)):Length()") == "3");
    CHECK(s.eval("(2 * I:Vector3(1, 2, 3)).Z") == "6");
    CHECK(s.eval("(I:Vector3(2, 4, 6) / 2).Y") == "2");
    CHECK(s.eval("(-I:Vector3(1, 0, 0)).X") == "-1");
    CHECK(s.eval("I:Vector3(1, 2, 3) == I:Vector3(1, 2, 3)") == "True");
    CHECK(s.eval("Mumble.PlayerCharacter.Position.X") == "0");
}

TEST_CASE("Storage keeps strings under a namespace, 000 when none is given", "[viewer][script]") {
    Scripted s("script_storage");
    CHECK(s.run(R"(Storage:UpsertValue("ns", "a", "one") Storage:UpsertValue("b", 2))"));
    CHECK(s.storage.read("ns\\a") == "one");
    CHECK(s.storage.read("000\\b") == "2");
    CHECK(s.eval(R"(Storage:ReadValue("ns", "a"))") == "one");
    CHECK(s.eval(R"(Storage:ReadValue("missing"))") == "");
    CHECK(s.run(R"(Storage:DeleteValue("ns", "a"))"));
    CHECK_FALSE(s.storage.read("ns\\a"));
    CHECK_FALSE(s.run(R"(Storage:UpsertValue("ns", "", "x"))"));
}

TEST_CASE("os is Pathing's: local-epoch time, Sunday as 0, impossible dates refused", "[viewer][script]") {
    Scripted s("script_os");
    CHECK(s.eval(R"(os.date("%Y-%m-%d %H:%M", os.time{year = 2024, month = 3, day = 10, hour = 12}))") == "2024-03-10 12:00");
    CHECK(s.eval(R"(os.date("*t", os.time{year = 2024, month = 3, day = 10}).wday)") == "0");
    CHECK(s.eval(R"(os.date("%A %j %%Q", os.time{year = 2024, month = 3, day = 10}))") == "Sunday 070 %Q");
    CHECK(s.eval(R"(os.date("100%", 0))") == "100%");
    CHECK(s.eval(R"(select(2, os.time{year = 2024, month = 2, day = 30}) ~= nil)") == "True");
    CHECK(s.eval(R"((os.time{year = 2024, month = 1.5}) == nil)") == "True");
    CHECK(s.eval(R"(os.date("*t").isdst)") == "True");
    CHECK(s.eval(R"(os.date("!*t").isdst)") == "False");
    CHECK(s.eval("os.difftime(10, 4)") == "6");

    // A time no calendar here reaches is the script's error, never a name read out of range.
    // gmtime_s reaches twelve hours before 1970, for the time zones west of UTC; a day before is past it.
    CHECK_FALSE(s.run(R"(os.date("%B %A", -86400))"));
    CHECK_FALSE(s.run(R"(os.date("!%B", 1e18))"));
    CHECK_FALSE(s.run(R"(os.date("%c", 0/0))"));
    CHECK_FALSE(s.run(R"(os.date("%c", math.huge))"));
}

TEST_CASE("table.ToLson writes NeoLua's notation, and FromLson reads it back", "[viewer][script]") {
    Scripted s("script_lson");
    CHECK(s.eval(R"(table.ToLson({}))") == "{}");
    CHECK(s.eval(R"(table.ToLson({1, 2.5, "a\"b"}, false))") == "{1,2.5,\"a\\\"b\"}");
    CHECK(s.eval(R"(table.ToLson({3.0}, false))") == "{3.0}");
    CHECK(s.eval(R"(table.ToLson({x = true}))") == "{\n\tx = true\n}");
    CHECK(s.eval(R"(table.ToLson({["two words"] = {1}}, false))") == "{[\"two words\"]={1}}");
    CHECK_FALSE(s.run(R"(table.ToLson({print}))"));

    CHECK(s.run(R"(
local back = table.FromLson(table.ToLson({1, "two", { nested = { 3 } }, flag = false, [10] = "ten"}))
ok = back[1] == 1 and back[2] == "two" and back[3].nested[1] == 3 and back.flag == false and back[10] == "ten"
)"));
    CHECK(s.eval("ok") == "True");
    // Lua's own spellings, which NeoLua's lexer takes as well.
    CHECK(s.eval(R"(table.FromLson("{ a = 'q', [2] = -0x10; -1.5e1, }").a)") == "q");
    CHECK(s.eval(R"(table.FromLson("{ a = 'q', [2] = -0x10; -1.5e1, }")[2])") == "-16");
    CHECK(s.eval(R"(table.FromLson("{ a = 'q', [2] = -0x10; -1.5e1, }")[1])") == "-15");
    CHECK_FALSE(s.run(R"(table.FromLson("{ a = os.exit() }"))"));

    // NeoLua writes a key bare when .NET calls every character of it a letter: Teh's Trails
    // keys its saved trails by character name, and a player's kv.json holds `Zhé = "..."`.
    CHECK(s.eval(R"(table.FromLson('{["54"]={Zh\u{E9}="guid"}}')["54"]["Zh\u{E9}"])") == "guid");
    CHECK(s.eval(R"(table.FromLson(table.ToLson({["Zh\u{E9}"] = 1}))["Zh\u{E9}"])") == "1");
}

TEST_CASE("Menu entries are found again by name, and a click flips and calls them", "[viewer][script]") {
    Scripted s("script_menu");
    CHECK(s.run(R"(
clicked = nil
local entry = Menu:Add("Toggle me", function(menu) clicked = menu.Checked end, true, false)
same = rawequal(Menu:Add("TOGGLE ME", nil), entry)
)"));
    CHECK(s.eval("same") == "True");
    REQUIRE(s.engine->menus().size() == 2);
    s.engine->clickMenu(1);
    CHECK(s.eval("clicked") == "True");
    CHECK(s.engine->menus()[1].checked);
}

TEST_CASE("Debug, print and User reach the console and the host", "[viewer][script]") {
    Scripted s("script_debug");
    CHECK(s.run(R"(print("a", 1, nil, true) Debug:Warn("careful") Debug:Watch("w", 42) copied = User.SetClipboard("text"))"));
    const auto& messages = s.engine->console().messages();
    CHECK(messages[messages.size() - 2].text == "a 1  True");
    CHECK(messages[messages.size() - 1].level == Level::Warn);
    CHECK(s.engine->watches().at("w") == "42");
    CHECK(s.clipboard == "text");
    CHECK(s.eval("copied") == "True");
}

TEST_CASE("the collector keeps up with scripts that leave garbage every frame", "[viewer][script][regression]") {
    // Behaviors calling a script-tick function for each of a map's markers, every frame: Lua's
    // own pacing let the heap this leaves double every cycle, each cycle's pause doubling with
    // it, until a frame took 76 ms. Here it reached 188 MB.
    Scripted s("script_gc_pace", R"(
function Pulse(marker, gameTime)
  local p = marker.Position
  return p.X > 0
end
)");
    s.engine->loadPacks();
    const viewer::behaviors::ScriptCall pulse{"Pulse", {}};
    size_t most = 0;
    constexpr int kFrames = 20000;
    for (int frame = 0; frame < kFrames; ++frame) {
        s.engine->tick(frame / 60.0, 1.0 / 60.0);
        // As behaviors call a map's script-tick markers, one call each, eighty a frame.
        for (int call = 0; call < 40; ++call) {
            for (size_t marker = 0; marker < s.live.markers.size(); ++marker) {
                s.engine->callBehavior(pulse, viewer::behaviors::Entity{false, marker}, viewer::behaviors::Hook::Tick, false);
            }
        }
        most = std::max(most, s.engine->heapBytes());
    }
    CHECK(s.errors() == 0);
    // What is live is a few hundred KB; a collector that keeps pace holds the heap near it.
    // Pure Lua garbage, made inside a tick handler, does not set it off: the calls in do.
    CHECK(most < size_t{8} * 1024 * 1024);
}

TEST_CASE("every script-tick call of a tick is handed that tick's one GameTime", "[viewer][script]") {
    // One a call was garbage for the collector, a userdata per scripted marker per frame.
    Scripted s("script_tick_time", R"(
local last
function Same(marker, gameTime)
  local same = last == gameTime
  last = gameTime
  return same
end
)");
    s.engine->loadPacks();
    const viewer::behaviors::ScriptCall same{"Same", {}};
    const auto call = [&] {
        return s.engine->callBehavior(same, viewer::behaviors::Entity{false, 0}, viewer::behaviors::Hook::Tick, false);
    };
    REQUIRE_FALSE(s.live.markers.empty());
    s.engine->tick(1.0, 1.0 / 60.0);
    CHECK(call() == false);
    CHECK(call() == true);
    s.engine->tick(1.0 + 1.0 / 60.0, 1.0 / 60.0);
    CHECK(call() == false);
    CHECK(call() == true);
    CHECK(s.errors() == 0);
}
