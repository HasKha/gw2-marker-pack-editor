// The slow-frame log: the line a frame of slow work of ours makes, and which frames make one.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "viewer/bake.h"
#include "viewer/frame_log.h"

TEST_CASE("a slow frame's line names our share, the steps that count, the frame, and what was going on", "[frame_log]") {
    viewer::frame_log::Frame frame;
    frame.interval = 48.24;
    frame.steps.live_map = 4.8;
    frame.steps.scripts = 0.9;
    frame.steps.scene = 0.04; // rounds to nothing, so left out
    frame.steps.render = 0.3;
    frame.slowest_scripts = "tehs_trails.taco";
    frame.map_id = 15;
    frame.quads = 3456;
    frame.lua_bytes = size_t{3} * 1024 * 1024 / 2;
    frame.jobs = 2;
    frame.uploads = 1;
    frame.upload_pixels = int64_t{2048} * 2048;
    frame.loading_packs = true;

    CHECK(
        viewer::frame_log::line(frame) ==
        "Slow frame: ours 6.0 ms (live map 4.8, scripts 0.9 (mostly tehs_trails.taco), render 0.3) of a 48.2 ms frame. "
        "Map 15, 3456 quads, Lua 1.5 MB, 2 jobs, 1 image to Nexus (16 MB), loading packs");
}

TEST_CASE("a line with nothing waiting says only the map, and how many it stands for", "[frame_log]") {
    viewer::frame_log::Frame frame;
    frame.interval = 19.2;
    frame.steps.behaviors = 13.4;
    frame.steps.saves = 0.1;
    frame.map_id = viewer::kMapUnknown;
    frame.quads = 95;
    frame.skipped = 3;
    CHECK(
        viewer::frame_log::line(frame) ==
        "Slow frame: ours 13.5 ms (behaviors 13.4, saves 0.1) of a 19.2 ms frame. Map none, 95 quads. 3 more since the last line");
}

TEST_CASE("frames of slow work of ours are logged at most once a second, counting those passed over", "[frame_log]") {
    using namespace std::chrono_literals;
    viewer::frame_log::Limiter limiter;
    const auto start = std::chrono::steady_clock::time_point{} + 100s;
    size_t skipped = 99;

    // Work within the threshold is never logged, however long the whole frame was.
    CHECK_FALSE(limiter.take(0.3, 5.0, start, &skipped));
    CHECK(limiter.take(6.0, 5.0, start, &skipped));
    CHECK(skipped == 0);

    // A run of them: the rest of that second is counted, not logged.
    CHECK_FALSE(limiter.take(6.0, 5.0, start + 100ms, &skipped));
    CHECK_FALSE(limiter.take(6.0, 5.0, start + 500ms, &skipped));
    CHECK_FALSE(limiter.take(1.0, 5.0, start + 1500ms, &skipped)); // within it, and not counted
    CHECK(limiter.take(6.0, 5.0, start + 1500ms, &skipped));
    CHECK(skipped == 2);
}
