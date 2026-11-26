#include "viewer/game/rtapi.h"

#include <Nexus.h>
#include <RTAPI.hpp>

#include <atomic>

namespace viewer::rtapi {

namespace {

// For the event callbacks, which Nexus hands nothing else: set before they are subscribed.
AddonAPI* addon_api = nullptr;

// Written from Nexus's event callbacks, which need not run on the render thread.
std::atomic<const RTAPI::RealTimeData*> block{nullptr};

const RTAPI::RealTimeData* findBlock() {
    return static_cast<const RTAPI::RealTimeData*>(addon_api->DataLink.Get(DL_RTAPI));
}

// EV_ADDON_LOADED and EV_ADDON_UNLOADED carry the signature of the addon, as an int.
bool isRtapi(void* event_args) {
    return event_args && *static_cast<const int*>(event_args) == RTAPI_SIG;
}

void onAddonLoaded(void* event_args) {
    if (isRtapi(event_args)) block = findBlock();
}

void onAddonUnloaded(void* event_args) {
    if (isRtapi(event_args)) block = nullptr;
}

} // namespace

void load(AddonAPI* api) {
    addon_api = api;
    block = findBlock();
    api->Events.Subscribe("EV_ADDON_LOADED", onAddonLoaded);
    api->Events.Subscribe("EV_ADDON_UNLOADED", onAddonUnloaded);
}

void unload() {
    if (!addon_api) return;
    addon_api->Events.Unsubscribe("EV_ADDON_LOADED", onAddonLoaded);
    addon_api->Events.Unsubscribe("EV_ADDON_UNLOADED", onAddonUnloaded);
    block = nullptr;
    addon_api = nullptr;
}

const RTAPI::RealTimeData* data() {
    // RTAPI zeroes GameBuild as it unloads, which may come before Nexus's event does. Its README
    // has addons check exactly this, so the block outlives the addon.
    const RTAPI::RealTimeData* current = block.load();
    return current && current->GameBuild != 0 ? current : nullptr;
}

} // namespace viewer::rtapi
