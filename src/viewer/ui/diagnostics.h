#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "viewer/game/camera.h"
#include "viewer/render/good_render.h"
#include "viewer/render/textures.h"

// The diagnostics in the options page's Development section: what draws the world and with which
// camera, the live map, the packs and their images, and the worker. For whoever is chasing a pack
// that does not show or a frame that stutters. The viewer gathers the figures while the section is
// open; they are made into lines here, so the text on screen and on the clipboard is the same.
namespace viewer::ui::diagnostics {

// How long each Present's work took, over the last frames.
class FrameTimes
{
public:
    // About four seconds at 60 fps: long enough to hold a hitch until the player has read it.
    static constexpr size_t KEPT = 240;

    void add(double milliseconds);
    double last() const { return last_; }
    double peak() const; // the longest of those kept

private:
    std::array<double, KEPT> times_{};
    size_t next_ = 0;
    double last_ = 0.0;
};

struct PackFigures {
    std::string name; // its file name
    std::string error; // why it did not load; empty when it did
    TextureCache::Counts images;
};

struct Figures {
    // Drawing.
    good_render_sink::Service service = good_render_sink::Service::None;
    good_render_sink::Drawn drawn; // what GoodRender's moments drew last frame
    CameraSource camera = CameraSource::None; // what the scene was built with, at Present
    bool full_map_up = false;
    bool full_map_from_game = false; // its open and close as GoodRender read them, not estimated
    size_t scene_items = 0;
    double present_ms = 0.0;
    double present_peak_ms = 0.0;

    // The live map.
    int map_id = 0; // kMapUnknown before MumbleLink has named one
    bool map_described = false; // in the API's list of maps, which the minimap and map need
    bool in_gameplay = true;
    size_t markers = 0;
    size_t trails = 0;

    // Packs, and what is waiting on them.
    size_t packs_found = 0;
    bool loading = false;
    std::vector<PackFigures> packs; // every installed pack, those that failed to load included
    size_t uploads_images = 0; // handed to Nexus and not made yet
    int64_t uploads_pixels = 0;
    size_t jobs = 0; // unfinished on the worker
    size_t lua_bytes = 0; // the scripts' heap, garbage included
};

struct Line {
    // FrameTime is text too: the Present line, which draw() follows with its controls.
    enum class Kind { Heading, Text, FrameTime, Warning, Error };
    Kind kind = Kind::Text;
    std::string text;
    const char* icon = nullptr; // a heading's
};

// The figures as the section shows them: each group under its heading.
std::vector<Line> describe(const Figures& figures);

// `lines`, with a button that copies them to the clipboard as plain text. `frame_time_controls`
// draws what goes under the frame time: the slow-frame log's setting.
void draw(const std::vector<Line>& lines, const std::function<void()>& frame_time_controls);

} // namespace viewer::ui::diagnostics
