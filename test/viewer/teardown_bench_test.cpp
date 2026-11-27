// How long unloading takes over what a session holds, offline: the installed packs, the live map
// of the busiest map and the script engine run on it, each freed as unload frees them. Nexus frees
// the library only once Unload returns, so all of it happens while the player waits on the Addons
// page. Hidden from ctest; run before and after a change to what a pack or a live map keeps, on a
// Release build and on a Debug one, where freeing is many times slower:
//
//     bin/Release/markus_viewer_test.exe "[.bench]"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <map>
#include <memory>

#include "corpus.h"
#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

TEST_CASE("unloading what a session holds over the corpus, timed", "[.bench]") {
    const std::vector<std::filesystem::path> paths = corpus::packs();
    if (paths.empty()) FAIL(corpus::missingMessage());

    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };

    auto installed = std::make_unique<viewer::runtime::Installed>();
    size_t markers = 0;
    size_t trails = 0;
    std::map<int, size_t> per_map;
    const Clock::time_point loading = Clock::now();
    for (const std::filesystem::path& path : paths) {
        auto loaded = std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(path));
        markers += loaded->pathables.markers.size();
        trails += loaded->pathables.trails.size();
        for (const viewer::BakedMarker& marker : loaded->pathables.markers) ++per_map[marker.map_id];
        viewer::runtime::install(installed.get(), std::move(loaded));
    }
    const double load_ms = ms(Clock::now() - loading);

    int busiest = 0;
    for (const auto& [map, count] : per_map) {
        if (count > per_map[busiest]) busiest = map;
    }

    // The busiest map live, with every pack's script loaded on it and ticked, as in game.
    auto live = std::make_unique<viewer::runtime::LiveMap>(viewer::runtime::liveMap(*installed, busiest));
    viewer::store::CategoryStates states;
    viewer::store::KeyValues storage;
    viewer::MumbleState mumble;
    mumble.available = true;
    mumble.map_id = busiest;
    viewer::script::Host host;
    host.installed = installed.get();
    host.live = live.get();
    host.category_states = &states;
    host.storage = &storage;
    host.mumble = &mumble;
    auto engine = std::make_unique<viewer::script::Engine>(host);
    engine->reset();
    engine->loadPacks();
    for (int tick = 1; tick <= 3; ++tick) engine->tick(tick * 0.016, 0.016);

    // In the order a session frees them: the engine, after the live map, after the packs.
    Clock::time_point step = Clock::now();
    engine.reset();
    const double engine_ms = ms(Clock::now() - step);
    step = Clock::now();
    live.reset();
    const double live_ms = ms(Clock::now() - step);
    step = Clock::now();
    installed.reset();
    const double installed_ms = ms(Clock::now() - step);

    std::printf(
        "%zu packs, %zu markers, %zu trails, loaded in %.0f ms. Map %d with %zu markers live.\n"
        "Freed: script engine %.0f ms, live map %.0f ms, installed packs %.0f ms.\n",
        paths.size(),
        markers,
        trails,
        load_ms,
        busiest,
        per_map[busiest],
        engine_ms,
        live_ms,
        installed_ms);
    SUCCEED();
}
