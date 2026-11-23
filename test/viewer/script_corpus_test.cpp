// Every script in the corpus, run the way the viewer runs it: on each map its pack has
// anything on, a fresh state, pack.lua, and a few ticks — against a fake MumbleLink that puts
// the player on the map. Pathing's Lua API has no documentation of its own, so this, and the
// census it fills, is how the API is known to be enough. See docs/viewer.md#the-census.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <set>
#include <string>

#include "corpus.h"
#include "pack/source.h"
#include "viewer/registry.h"
#include "viewer/runtime.h"
#include "viewer/script/engine.h"
#include "viewer/script/mumble.h"
#include "viewer/store.h"

namespace {

// A second apart, so that a handler throttled to run every so often does run.
constexpr int kTicks = 3;
constexpr double kTickSeconds = 1.0;

// The packs the census has to be empty for, which is milestone 2's bar (docs/plan.md).
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
        viewer::script::MumbleState mumble;
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

            engine.console().clear();
            engine.reset();
            engine.loadPacks();
            for (int tick = 1; tick <= kTicks; ++tick) engine.tick(tick * kTickSeconds, kTickSeconds);

            for (const viewer::script::Message& message : engine.console().messages()) {
                if (message.level == viewer::script::Level::Error) errors.push_back("map " + std::to_string(map) + ": " + message.text);
            }
        }

        INFO(file << ": " << errors.size() << " errors, the first: " << (errors.empty() ? std::string() : errors.front()));
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
