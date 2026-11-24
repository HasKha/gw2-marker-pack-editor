#pragma once

struct AddonAPI;

namespace RTAPI {
struct RealTimeData;
}

// The RealTime API addon's data block, while that addon is loaded. Optional: without it the
// camera is MumbleLink's. Nexus can load and unload it at any time, so it is looked up again
// whenever Nexus says it loaded.
namespace viewer::rtapi {

// Looks the block up and follows RTAPI being loaded and unloaded from here on.
void load(AddonAPI* api);

// Stops following it.
void unload();

// The block now, or null while RTAPI is not loaded. Any thread.
const RTAPI::RealTimeData* data();

} // namespace viewer::rtapi
