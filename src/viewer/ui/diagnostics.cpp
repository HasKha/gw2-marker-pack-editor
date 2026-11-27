#include "viewer/ui/diagnostics.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <algorithm>
#include <format>

#include "viewer/live/bake.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

namespace viewer::ui::diagnostics {

namespace {

std::string milliseconds(double value) {
    return std::format("{:.2f} ms", value);
}

// "1 marker", "2 markers".
std::string plural(size_t value, const char* noun) {
    return std::to_string(value) + " " + noun + (value == 1 ? "" : "s");
}

const char* cameraName(CameraSource source) {
    switch (source) {
    case CameraSource::MumbleLink: return "MumbleLink";
    case CameraSource::Rtapi: return "RTAPI";
    case CameraSource::GoodRender: return "GoodRender";
    case CameraSource::None: break;
    }
    return "none yet";
}

} // namespace

void FrameTimes::add(double milliseconds) {
    times[next] = milliseconds;
    next = (next + 1) % KEPT;
    latest = milliseconds;
}

double FrameTimes::peak() const {
    return *std::max_element(times.begin(), times.end());
}

std::vector<Line> describe(const Figures& figures) {
    std::vector<Line> lines;
    const auto heading = [&lines](const char* icon, const char* text) { lines.push_back(Line{Line::Kind::Heading, text, icon}); };
    const auto text = [&lines](std::string text, Line::Kind kind = Line::Kind::Text) { lines.push_back(Line{kind, std::move(text)}); };

    // What draws the world and the maps, as the last frame went. What the moments did not draw, the
    // overlay draws over everything (viewer.md, Drawing).
    heading(ICON_FA_DISPLAY, "Rendering");
    switch (figures.service) {
    case good_render_sink::Service::Serving: text("GoodRender: running"); break;
    case good_render_sink::Service::OtherVersion:
        text("GoodRender: running, but not a version this build of Markus can use", Line::Kind::Warning);
        break;
    case good_render_sink::Service::None: text("GoodRender: not running"); break;
    }
    if (figures.drawn.occluded && figures.drawn.over) {
        text("World: drawn by GoodRender");
    } else if (figures.drawn.over) {
        // A moment with no world_quad: only what nothing hides goes into its draw list.
        text("World: GoodRender without world_quad; what should hide behind the world is in the overlay, over everything");
    } else {
        text("World: the overlay, over everything");
    }
    text(figures.drawn.map ? "Minimap: drawn by GoodRender, under the game's windows" : "Minimap: the overlay, over everything");
    if (figures.full_map_up) {
        text(
            figures.full_map_from_game ? "Full map: opens and closes as the game draws it, from GoodRender"
                                       : "Full map: its opening and closing estimated");
    }
    // The scene is built at Present with the game's camera, which sorts, culls and places the
    // player fades go by; a moment draws it with GoodRender's own. The overlay draws with the
    // game's camera as well.
    if (figures.drawn.over) {
        text(std::string("Camera: GoodRender's to draw, ") + cameraName(figures.camera) + "'s to sort, cull and fade");
    } else {
        text(std::string("Camera: ") + cameraName(figures.camera));
    }
    text("Scene: " + plural(figures.scene_items, "quad"));
    text(
        "Present: " + milliseconds(figures.present_ms) + ", at most " + milliseconds(figures.present_peak_ms) + " in the last " +
            std::to_string(FrameTimes::KEPT) + " frames",
        Line::Kind::FrameTime);

    heading(ICON_FA_MAP_LOCATION_DOT, "Map");
    if (figures.map_id == kMapUnknown) {
        text("No map yet");
    } else if (figures.map_described) {
        text("Map " + std::to_string(figures.map_id));
    } else {
        text(
            "Map " + std::to_string(figures.map_id) + ": not in the API's list of maps, so nothing on the minimap or map",
            Line::Kind::Warning);
    }
    if (!figures.in_gameplay) text("Loading screen: nothing drawn");
    text("On this map: " + plural(figures.markers, "marker") + ", " + plural(figures.trails, "trail"));

    // A pack that failed, and every image a pack names that is not there, are what an author fixes.
    heading(ICON_FA_BOX_ARCHIVE, "Packs");
    text(
        "Loaded: " + std::to_string(figures.packs.size()) + " of " + std::to_string(figures.packs_found) +
        (figures.loading ? ", loading" : ""));
    for (const PackFigures& pack : figures.packs) {
        if (!pack.error.empty()) text(pack.name + ": " + pack.error, Line::Kind::Error);
    }
    text(std::format("Lua heap: {:.1f} MB", static_cast<double>(figures.lua_bytes) / (1024.0 * 1024.0)));

    heading(ICON_FA_IMAGE, "Images");
    size_t ready = 0;
    size_t loading = 0;
    size_t missing = 0;
    for (const PackFigures& pack : figures.packs) {
        ready += pack.images.ready;
        loading += pack.images.loading;
        missing += pack.images.missing.size();
    }
    text(
        "Looked up: " + std::to_string(ready) + " ready, " + std::to_string(loading) + " on their way, " + std::to_string(missing) +
        " missing");
    // RGBA, four bytes a pixel, as Nexus copies them to the GPU.
    const int64_t megabytes = figures.uploads_pixels * 4 / (1024 * 1024);
    text("Handed to Nexus, not made yet: " + plural(figures.uploads_images, "image") + ", " + std::to_string(megabytes) + " MB");
    text("Worker: " + plural(figures.jobs, "job") + " to finish");
    // A broken pack can name hundreds; the first few say which pack and what is wrong.
    constexpr size_t kMissingShown = 20;
    for (const PackFigures& pack : figures.packs) {
        const std::vector<std::string>& names = pack.images.missing;
        for (size_t i = 0; i < std::min(names.size(), kMissingShown); ++i)
            text("Missing: " + pack.name + ": " + names[i], Line::Kind::Warning);
        if (names.size() > kMissingShown) {
            text("Missing: " + pack.name + ": and " + std::to_string(names.size() - kMissingShown) + " more", Line::Kind::Warning);
        }
    }
    return lines;
}

void draw(const std::vector<Line>& lines, const std::function<void()>& frame_time_controls) {
    if (ImGui::Button(fonts::label(ICON_FA_COPY, "Copy Diagnostics").c_str())) {
        std::string text;
        for (const Line& line : lines) text += line.text + "\n";
        ImGui::SetClipboardText(text.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("For a bug report.");

    for (const Line& line : lines) {
        switch (line.kind) {
        case Line::Kind::Heading: style::subheading(line.icon, line.text.c_str()); break;
        case Line::Kind::Text: ImGui::Text("%s", line.text.c_str()); break;
        case Line::Kind::FrameTime:
            ImGui::Text("%s", line.text.c_str());
            if (frame_time_controls) frame_time_controls();
            break;
        // A pack's error or a missing file's path runs as long as the pack makes it.
        case Line::Kind::Warning:
        case Line::Kind::Error:
            ImGui::PushStyleColor(ImGuiCol_Text, line.kind == Line::Kind::Error ? style::ERROR_TEXT : style::WARNING);
            ImGui::TextWrapped("%s", line.text.c_str());
            ImGui::PopStyleColor();
            break;
        }
    }
}

} // namespace viewer::ui::diagnostics
