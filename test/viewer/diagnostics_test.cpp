// The Development section's diagnostics: the figures made into the lines shown and copied, and
// what the viewer reads them from that is not counted elsewhere.

#include <catch2/catch_test_macros.hpp>

#include <good_render.h>

#include <algorithm>
#include <string>
#include <vector>

#include "viewer/live/bake.h"
#include "viewer/render/good_render.h"
#include "viewer/ui/diagnostics.h"

namespace {

using viewer::ui::diagnostics::Line;

bool has(const std::vector<Line>& lines, const std::string& text, Line::Kind kind = Line::Kind::Text) {
    return std::any_of(lines.begin(), lines.end(), [&](const Line& line) { return line.text == text && line.kind == kind; });
}

} // namespace

TEST_CASE("frame times keep the last and the longest of the frames kept", "[diagnostics]") {
    viewer::ui::diagnostics::FrameTimes times;
    times.add(3.0);
    times.add(9.0);
    times.add(1.0);
    CHECK(times.last() == 1.0);
    CHECK(times.peak() == 9.0);

    // A hitch is forgotten once as many frames have come after it as are kept.
    for (size_t i = 0; i < viewer::ui::diagnostics::FrameTimes::KEPT; ++i) times.add(2.0);
    CHECK(times.peak() == 2.0);
}

TEST_CASE("the diagnostics say what drew the world, and with which camera", "[diagnostics]") {
    viewer::ui::diagnostics::Figures figures;
    figures.map_id = 15;
    figures.map_described = true;

    SECTION("GoodRender drew all of it") {
        figures.service = viewer::good_render_sink::Service::Serving;
        figures.drawn.occluded = true;
        figures.drawn.over = true;
        figures.drawn.map = true;
        figures.camera = viewer::CameraSource::Rtapi;
        const std::vector<Line> lines = viewer::ui::diagnostics::describe(figures);
        CHECK(has(lines, "GoodRender: running"));
        CHECK(has(lines, "World: drawn by GoodRender"));
        CHECK(has(lines, "Minimap: drawn by GoodRender, under the game's windows"));
        CHECK(has(lines, "Camera: GoodRender's to draw, RTAPI's to sort, cull and fade"));
    }

    SECTION("a moment without world_quad draws with GoodRender's camera too") {
        figures.service = viewer::good_render_sink::Service::Serving;
        figures.drawn.over = true;
        figures.camera = viewer::CameraSource::MumbleLink;
        CHECK(has(viewer::ui::diagnostics::describe(figures), "Camera: GoodRender's to draw, MumbleLink's to sort, cull and fade"));
    }

    SECTION("no service: the overlay draws everything") {
        figures.camera = viewer::CameraSource::MumbleLink;
        const std::vector<Line> lines = viewer::ui::diagnostics::describe(figures);
        CHECK(has(lines, "GoodRender: not running"));
        CHECK(has(lines, "World: the overlay, over everything"));
        CHECK(has(lines, "Minimap: the overlay, over everything"));
        CHECK(has(lines, "Camera: MumbleLink"));
    }

    SECTION("a service of another version is a warning") {
        figures.service = viewer::good_render_sink::Service::OtherVersion;
        CHECK(
            has(viewer::ui::diagnostics::describe(figures),
                "GoodRender: running, but not a version this build of Markus can use",
                Line::Kind::Warning));
    }
}

TEST_CASE("the diagnostics name a map the API does not describe", "[diagnostics]") {
    viewer::ui::diagnostics::Figures figures;
    figures.map_id = viewer::kMapUnknown;
    CHECK(has(viewer::ui::diagnostics::describe(figures), "No map yet"));

    figures.map_id = 1234;
    figures.markers = 1;
    figures.trails = 2;
    const std::vector<Line> lines = viewer::ui::diagnostics::describe(figures);
    CHECK(has(lines, "Map 1234: not in the API's list of maps, so nothing on the minimap or map", Line::Kind::Warning));
    CHECK(has(lines, "On this map: 1 marker, 2 trails"));
}

TEST_CASE("the diagnostics list a pack's error and the images it names that are missing", "[diagnostics]") {
    viewer::ui::diagnostics::Figures figures;
    figures.packs_found = 3;
    figures.loading = true;

    viewer::ui::diagnostics::PackFigures broken;
    broken.name = "broken.taco";
    broken.error = "not a zip archive";
    figures.packs.push_back(broken);

    viewer::ui::diagnostics::PackFigures sloppy;
    sloppy.name = "sloppy.zip";
    sloppy.images.ready = 4;
    sloppy.images.loading = 1;
    for (int i = 0; i < 25; ++i) sloppy.images.missing.push_back("icon" + std::to_string(100 + i) + ".png");
    figures.packs.push_back(sloppy);

    const std::vector<Line> lines = viewer::ui::diagnostics::describe(figures);
    CHECK(has(lines, "Loaded: 2 of 3, loading"));
    CHECK(has(lines, "broken.taco: not a zip archive", Line::Kind::Error));
    CHECK(has(lines, "Looked up: 4 ready, 1 on their way, 25 missing"));
    CHECK(has(lines, "Missing: sloppy.zip: icon100.png", Line::Kind::Warning));
    // The rest of a long list is counted, not listed.
    CHECK_FALSE(has(lines, "Missing: sloppy.zip: icon124.png", Line::Kind::Warning));
    CHECK(has(lines, "Missing: sloppy.zip: and 5 more", Line::Kind::Warning));
}

TEST_CASE("the sink reports whether GoodRender serves the Moment version it was built against", "[diagnostics][good_render]") {
    viewer::good_render_sink::load(nullptr);
    // The test stands in for the service: the block is this process's, shared with the sink.
    good_render::Link service = good_render::open();
    REQUIRE(service.block);

    CHECK(viewer::good_render_sink::service() == viewer::good_render_sink::Service::None);

    service.block->oldest_served = good_render::MOMENT_VERSION;
    service.block->newest_served = good_render::MOMENT_VERSION;
    CHECK(viewer::good_render_sink::service() == viewer::good_render_sink::Service::Serving);

    service.block->oldest_served = good_render::MOMENT_VERSION + 1;
    service.block->newest_served = good_render::MOMENT_VERSION + 1;
    CHECK(viewer::good_render_sink::service() == viewer::good_render_sink::Service::OtherVersion);

    service.block->oldest_served = 0;
    service.block->newest_served = 0;
    good_render::close(service);
    viewer::good_render_sink::unload();
}
