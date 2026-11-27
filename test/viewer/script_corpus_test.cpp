// Every script in the corpus, run the way the viewer runs it: on each map its pack has
// anything on, a fresh state, pack.lua, and a few ticks — against a fake MumbleLink that puts
// the player on the map. Pathing's Lua API has no documentation of its own, so this, and the
// census it fills, is how the API is known to be enough. See docs/viewer.md#lua.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <set>
#include <string>

#include "corpus.h"
#include "pack/source.h"
#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

namespace {

// A second apart, so that a handler throttled to run every so often does run.
constexpr int kTicks = 3;
constexpr double kTickSeconds = 1.0;

// The packs the census has to be empty for.
bool censusMustBeEmpty(const std::string& file) {
    return file == "hero.blish.pack.zip" || file == "TehsTrails.taco";
}

std::set<int> mapsOf(const viewer::BakedPathables& pathables) {
    std::set<int> maps;
    for (const viewer::BakedMarker& marker : pathables.markers) maps.insert(marker.map_id);
    for (const viewer::BakedTrail& trail : pathables.trails) maps.insert(trail.map_id);
    maps.erase(viewer::kMapUnknown);
    return maps;
}

// Where the player stands on `map`: at its first marker, so that scripts measuring distances
// see a player who is somewhere sensible.
glm::vec3 standingPoint(const viewer::runtime::LiveMap& live) {
    if (live.markers.empty()) return glm::vec3(0.0f);
    const glm::vec3 file = live.markers.front().marker.position;
    return glm::vec3(file.x, file.z, file.y);
}

enum class Pass { Frame, Interaction };

// Errors the packs make in Pathing too, by pack and the text that says so. Each is a bug of
// the pack's, read off its own files, and the viewer failing the same way is the parity this
// test is for.
struct PackBug {
    const char* pack;
    const char* error;
};
constexpr PackBug kPackBugs[] = {
    // Teh_Bounce makes the marker its trigger names bounce, but a few of those markers sit in
    // categories (tt.hot.cm.rm.1 and .2 of map 1045) that declare no bounce, so
    // GetBehavior("BounceModifier") is nil there in Pathing as well.
    {"TehsTrails.taco", "bounce.lua:56: attempt to index a nil value (local 'bounce')"},
};

bool knownPackBug(const std::string& pack, const std::string& error) {
    return std::any_of(std::begin(kPackBugs), std::end(kPackBugs), [&](const PackBug& bug) {
        return pack == bug.pack && error.find(bug.error) != std::string::npos;
    });
}

void callEvery(viewer::script::Engine* engine, const viewer::behaviors::Behaviors& behaviors, viewer::behaviors::Entity entity, Pass pass) {
    using viewer::behaviors::Hook;
    const auto call = [&](const std::optional<viewer::behaviors::ScriptCall>& function, Hook hook, bool flag) {
        if (function) engine->callBehavior(*function, entity, hook, flag);
    };
    if (pass == Pass::Frame) {
        call(behaviors.once, Hook::Once, false);
        call(behaviors.tick, Hook::Tick, false);
        call(behaviors.filter, Hook::Filter, false);
        return;
    }
    call(behaviors.focus, Hook::Focus, true);
    call(behaviors.trigger, Hook::Trigger, false);
    call(behaviors.focus, Hook::Focus, false);
}

} // namespace

TEST_CASE("every corpus script loads and ticks on every map of its pack without an error", "[viewer][script][corpus]") {
    const std::vector<std::filesystem::path> packs = corpus::packs();
    if (packs.empty()) FAIL(corpus::missingMessage());

    size_t scripted = 0;
    for (const std::filesystem::path& path : packs) {
        // Only an archive's directory is read to know; most packs have no scripts, and loading
        // them whole would be most of this test's time.
        std::string error;
        const std::unique_ptr<pack::DataSource> source = pack::open(path, &error);
        if (!source || !source->contains("pack.lua")) continue;
        ++scripted;

        viewer::runtime::Installed installed;
        viewer::runtime::install(&installed, std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(path)));
        const viewer::registry::LoadedPack& loaded = *installed.packs.front()->loaded;

        viewer::runtime::LiveMap live;
        viewer::store::CategoryStates states;
        viewer::store::KeyValues storage;
        viewer::MumbleState mumble;
        mumble.available = true;

        viewer::script::Host host;
        host.installed = &installed;
        host.live = &live;
        host.category_states = &states;
        host.storage = &storage;
        host.mumble = &mumble;
        viewer::script::Engine engine(host);

        const std::string file = path.filename().string();
        std::vector<std::string> errors;
        for (const int map : mapsOf(loaded.pathables)) {
            live = viewer::runtime::liveMap(installed, map);
            mumble.map_id = map;
            mumble.player_position = standingPoint(live);

            engine.console.clear();
            engine.reset();
            engine.loadPacks();
            for (int tick = 1; tick <= kTicks; ++tick) engine.tick(tick * kTickSeconds, kTickSeconds);

            // Every script-* function the map's pathables name, as behaviors call them: what a
            // player walking the map would set off. Every once, tick and filter first, as the
            // first frame runs them all before anyone can walk anywhere; then each marker
            // focused, its key pressed, and let go.
            for (const Pass pass : {Pass::Frame, Pass::Interaction}) {
                // By index, and the size read again: a call may add markers.
                for (size_t i = 0; i < live.markers.size(); ++i) {
                    if (const auto behaviors = live.markers[i].marker.behaviors; behaviors && behaviors->scripted) {
                        callEvery(&engine, *behaviors, viewer::behaviors::Entity{false, i}, pass);
                    }
                }
                for (size_t i = 0; i < live.trails.size(); ++i) {
                    if (const auto behaviors = live.trails[i].trail.behaviors; behaviors && behaviors->scripted) {
                        callEvery(&engine, *behaviors, viewer::behaviors::Entity{true, i}, pass);
                    }
                }
            }
            // Then ticks again: much of what a focus or trigger starts runs from the pack's tick
            // handlers — Teh's Trails' highlight pulse and heart follower — and nowhere else.
            for (int tick = kTicks + 1; tick <= 2 * kTicks; ++tick) engine.tick(tick * kTickSeconds, kTickSeconds);

            for (const viewer::script::Message& message : engine.console.messages()) {
                if (message.level != viewer::script::Level::Error || knownPackBug(file, message.text)) continue;
                errors.push_back("map " + std::to_string(map) + ": " + message.text);
            }
        }

        // Each distinct error once, by its first line: one broken function fails on every map.
        std::set<std::string> distinct;
        for (const std::string& failure : errors)
            distinct.insert(failure.substr(failure.find(": ") + 2, failure.find('\n') - failure.find(": ") - 2));
        std::string listed;
        for (const std::string& failure : distinct) listed += "\n  " + failure;
        INFO(file << ": " << errors.size() << " errors:" << listed);
        CHECK(errors.empty());

        if (censusMustBeEmpty(file)) {
            std::string missing;
            for (const viewer::script::CensusEntry& entry : engine.census().entries()) {
                missing += "\n  " + entry.member + " at " + entry.source + ":" + std::to_string(entry.line);
            }
            INFO(file << " asks for what the API lacks:" << missing);
            CHECK(engine.census().entries().empty());
        }
    }
    CHECK(scripted > 0);
}
