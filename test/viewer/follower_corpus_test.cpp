// Teh's Trails' heart follower, walked frame by frame: the pack's own scripts and markers, our
// script engine and behaviors, and a player running a route at 60 frames a second, as the viewer
// runs them — scripts tick, then behaviors.

#include <catch2/catch_test_macros.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>

#include "corpus.h"
#include "viewer/behaviors/states.h"
#include "viewer/behaviors/update.h"
#include "viewer/game/mumble.h"
#include "viewer/guid.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

namespace {

constexpr double kFrame = 1.0 / 60.0;
constexpr float kRunSpeed = 9.0f; // metres a second, about a character's run

// One map of a pack, its scripts loaded, and a player on it.
class Walk
{
public:
    Walk(const std::filesystem::path& pack, int map) {
        viewer::runtime::install(&installed, std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(pack)));
        live = viewer::runtime::liveMap(installed, map);
        mumble.available = true;
        mumble.map_id = map;

        viewer::script::Host host;
        host.installed = &installed;
        host.live = &live;
        host.category_states = &categories;
        host.storage = &storage;
        host.mumble = &mumble;
        host.show_info = [this](const std::string& text) { screen.addInfo(text); };
        host.hide_info = [this](const std::string& text) { screen.removeInfo(text); };
        engine = std::make_unique<viewer::script::Engine>(host);
        engine->loadPacks();
        loaded_markers = live.markers.size();
    }

    // The pack's marker of `guid`, by index.
    size_t find(const char* guid) const {
        const viewer::Guid wanted = viewer::guidFromText(guid);
        for (size_t i = 0; i < loaded_markers; ++i) {
            if (live.markers[i].marker.guid == wanted) return i;
        }
        FAIL("no marker " << guid);
        return 0;
    }

    // Runs from `from` to `to`, in the file's (x, y, z), a frame at a time, then stands there for
    // `stand` seconds.
    void run(glm::vec3 from, glm::vec3 to, double stand = 0.0) {
        const int frames = std::max(1, static_cast<int>(glm::distance(from, to) / (kRunSpeed * kFrame)));
        for (int i = 1; i <= frames; ++i) frame(from + (to - from) * (static_cast<float>(i) / static_cast<float>(frames)));
        for (double t = 0.0; t < stand; t += kFrame) frame(to);
    }

    // Markers scripts made carrying `info`, and how many of them stand now.
    size_t made(const std::string& info) const { return countMade(info, false); }
    size_t standing(const std::string& info) const { return countMade(info, true); }

    bool shown(const std::string& info) const { return std::find(screen.info.begin(), screen.info.end(), info) != screen.info.end(); }

    viewer::runtime::Installed installed;
    viewer::runtime::LiveMap live;
    viewer::store::CategoryStates categories;
    viewer::store::KeyValues storage;
    viewer::behaviors::BehaviorStates timers;
    viewer::behaviors::Screen screen;
    viewer::MumbleState mumble;
    std::unique_ptr<viewer::script::Engine> engine;
    size_t loaded_markers = 0;
    double seconds = 100.0;

private:
    void frame(glm::vec3 file) {
        mumble.player_position = glm::vec3(file.x, file.z, file.y); // Blish's (x, z, y)
        seconds += kFrame;
        engine->tick(seconds, kFrame);
        viewer::behaviors::Host host;
        host.states = &timers;
        host.categories = &categories;
        host.tree = &installed.tree;
        host.screen = &screen;
        host.script =
            [this](const viewer::behaviors::ScriptCall& call, viewer::behaviors::Entity entity, viewer::behaviors::Hook hook, bool flag) {
                return engine->callBehavior(call, entity, hook, flag);
            };
        const viewer::behaviors::Frame frame{&mumble, 1795348800000, seconds, nullptr, nullptr};
        viewer::behaviors::update(&live, viewer::runtime::visibleNodes(installed.tree, categories), host, frame);
    }

    size_t countMade(const std::string& info, bool standing_only) const {
        size_t count = 0;
        for (size_t i = loaded_markers; i < live.markers.size(); ++i) {
            const viewer::runtime::LiveMarker& marker = live.markers[i];
            const bool carries = marker.marker.behaviors && marker.marker.behaviors->info == info;
            if (carries && !(standing_only && marker.removed)) ++count;
        }
        return count;
    }
};

std::filesystem::path tehsTrails() {
    for (const std::filesystem::path& path : corpus::packs()) {
        if (path.filename() == "TehsTrails.taco") return path;
    }
    return {};
}

// Runs from 60 m out into the start heart of `start`, checks a follower carries its info, then on
// through each of `stops` in turn, a few seconds at each, then 80 m past the last: no follower is
// left, nor its info. The follower goes when the last stop, a plain icon with no behaviors, is
// focused; none ever was, so it stayed with the player, its info on screen, until the next heart.
void runHeart(int map, const char* start, std::initializer_list<const char*> stops) {
    const std::filesystem::path pack = tehsTrails();
    if (pack.empty()) SKIP("TehsTrails.taco is not in the corpus");

    Walk walk(pack, map);
    const viewer::runtime::LiveMarker& heart = walk.live.markers[walk.find(start)];
    REQUIRE(heart.marker.behaviors);
    REQUIRE(heart.marker.behaviors->info);
    const std::string info = *heart.marker.behaviors->info;
    glm::vec3 at = heart.marker.position;

    const glm::vec3 first_stop = walk.live.markers[walk.find(*stops.begin())].marker.position;
    const glm::vec3 behind = at + glm::normalize(glm::vec3(at.x - first_stop.x, 0.0f, at.z - first_stop.z)) * 60.0f;
    walk.run(behind, behind, 0.5);
    walk.run(behind, at, 1.0);
    CHECK(walk.made(info) >= 1);
    CHECK(walk.shown(info));

    glm::vec3 previous = at;
    for (const char* stop : stops) {
        previous = at;
        at = walk.live.markers[walk.find(stop)].marker.position;
        walk.run(previous, at, 3.0);
    }
    walk.run(at, at + glm::normalize(glm::vec3(at.x - previous.x, 0.0f, at.z - previous.z)) * 80.0f, 2.0);
    CHECK(walk.standing(info) == 0);
    CHECK_FALSE(walk.shown(info));
    CHECK(walk.engine->console.errors() == 0);
}

} // namespace

TEST_CASE("Teh's heart follower and its info go once its heart is done (corpus)", "[viewer][script][corpus][regression]") {
    // Teh_HeartFollower: map 15's "Kill bandits, Interact with feed", to its 100% heart.
    SECTION("to the finish heart") {
        runHeart(15, "7ZWdaPYnV0mZOiy+6Rttlw==", {"FU4XaRgni0CRpYjFe8J5rA=="});
    }
    // Teh_HeartFollower2: map 34's heart, its finish 7.7 m off, the closest pair in the pack; the
    // follower goes at the waypoint it points on to.
    SECTION("past the finish to the next waypoint") {
        runHeart(34, "Ro/w0DcxQkm2H/32p0Rccw==", {"Vdk6I5bNQ0mzEO/Dif71og==", "h63FZG19dUmUARsENIhedg=="});
    }
}
