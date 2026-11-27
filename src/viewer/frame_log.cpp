#include "viewer/frame_log.h"

#include <format>

#include "viewer/live/bake.h"

namespace viewer::frame_log {

namespace {

std::string milliseconds(double value) {
    return std::format("{:.1f}", value);
}

} // namespace

double Steps::total() const {
    return packs + live_map + scripts + behaviors + interaction + visibility + saves + editor + scene + maps + render + moments;
}

std::string line(const Frame& frame) {
    // Ours, with each step that counts, the rest rounding to nothing; then the whole frame.
    constexpr double kShown = 0.1;
    std::string text = "Slow frame: ours " + milliseconds(frame.steps.total()) + " ms";
    std::string steps;
    const auto step = [&](const char* name, double value, const std::string& note = {}) {
        if (value < kShown) return;
        steps += (steps.empty() ? "" : ", ") + std::string(name) + " " + milliseconds(value) + note;
    };
    const Steps& s = frame.steps;
    step("packs", s.packs);
    step("live map", s.live_map);
    step("scripts", s.scripts, frame.slowest_scripts.empty() ? "" : " (mostly " + frame.slowest_scripts + ")");
    step("behaviors", s.behaviors);
    step("interaction", s.interaction);
    step("visibility", s.visibility);
    step("saves", s.saves);
    step("editor", s.editor);
    step("scene", s.scene);
    step("maps", s.maps);
    step("render", s.render);
    step("moments", s.moments);
    if (!steps.empty()) text += " (" + steps + ")";
    text += " of a " + milliseconds(frame.interval) + " ms frame";

    // What was going on: the map and its size, and work waiting that lands on a later frame.
    text += ". Map " + (frame.map_id == kMapUnknown ? std::string("none") : std::to_string(frame.map_id));
    text += ", " + std::to_string(frame.quads) + " quads";
    if (frame.lua_bytes > 0) {
        text += std::format(", Lua {:.1f} MB", static_cast<double>(frame.lua_bytes) / (1024.0 * 1024.0));
    }
    if (frame.jobs > 0) text += ", " + std::to_string(frame.jobs) + " jobs";
    if (frame.uploads > 0) {
        // RGBA, four bytes a pixel, as Nexus copies them to the GPU.
        text += ", " + std::to_string(frame.uploads) + (frame.uploads == 1 ? " image" : " images") + " to Nexus (" +
                std::to_string(frame.upload_pixels * 4 / (1024 * 1024)) + " MB)";
    }
    if (frame.loading_packs) text += ", loading packs";
    if (frame.skipped > 0) text += ". " + std::to_string(frame.skipped) + " more since the last line";
    return text;
}

bool Limiter::take(double ours, double threshold, std::chrono::steady_clock::time_point now, size_t* skipped) {
    if (ours <= threshold) return false;
    constexpr auto kSpacing = std::chrono::seconds(1);
    if (logged && now - last < kSpacing) {
        ++passed_over;
        return false;
    }
    *skipped = passed_over;
    passed_over = 0;
    last = now;
    logged = true;
    return true;
}

} // namespace viewer::frame_log
