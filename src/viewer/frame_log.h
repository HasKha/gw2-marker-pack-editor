#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

// The slow-frame log, the Development section's: a frame in which our own work took long, as one
// line in Nexus's log saying how long, where it went, how long the whole frame was, and what was
// going on. Our share alone decides: the whole frame is the game's and every addon's, and is slow
// for reasons none of ours.
namespace viewer::frame_log {

// Where our share of one frame went, in milliseconds.
struct Steps {
    // Present's work (preRender), in its order.
    double packs = 0.0; // packs loaded, written or downloaded; the catalog and the account
    double live_map = 0.0; // the camera, and the live map copied out on a map or pack change
    double scripts = 0.0;
    double behaviors = 0.0; // filters, and scripts' once, tick and filter
    double interaction = 0.0; // the interact key: copies, category toggles, scripts' trigger
    double visibility = 0.0; // which categories show, worked out again when one changed; fade-ins
    double saves = 0.0; // the states made text for the worker to write, every few seconds
    double editor = 0.0; // the editor's pack made a live map
    double scene = 0.0;
    double maps = 0.0;
    // After it.
    double render = 0.0; // our Nexus render: the overlay, our windows, the editor's
    double moments = 0.0; // GoodRender's moments, inside the game's frame

    double total() const;
};

struct Frame {
    double interval = 0.0; // from one Present to the next: the whole frame, for scale
    Steps steps;
    // The pack whose scripts take longest, empty when none ran.
    std::string slowest_scripts;

    int map_id = 0;
    size_t quads = 0;
    // The scripts' heap, garbage included: one that grows from line to line is the collector
    // falling behind, and its pauses grow with it.
    size_t lua_bytes = 0;
    size_t jobs = 0; // unfinished on the worker
    size_t uploads = 0; // images handed to Nexus and not made yet, which its Present makes
    int64_t upload_pixels = 0;
    bool loading_packs = false;
    // Frames over the threshold passed over since the last line: see Limiter.
    size_t skipped = 0;
};

// One line: our share, every step of it that took 0.1 ms or more, the whole frame, then what was
// going on.
std::string line(const Frame& frame);

// Which frames over the threshold are logged: at most one a second, so that a run of them is a
// line a second rather than sixty, saying how many it stands for.
class Limiter
{
public:
    // Whether a frame whose work of ours took `ours` ms, at `now`, is logged: more than `threshold`
    // ms, as it must be. `*skipped` is then the frames over it passed over since the last logged.
    bool take(double ours, double threshold, std::chrono::steady_clock::time_point now, size_t* skipped);

private:
    std::chrono::steady_clock::time_point last_{};
    bool logged_ = false;
    size_t skipped_ = 0;
};

} // namespace viewer::frame_log
