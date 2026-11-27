#include <Windows.h>

#include <Mumble.h>
#include <Nexus.h>
#include <imgui.h>

#include <mutex>

#include "editor/marker_editor.h"
#include "viewer/viewer.h"

void AddonLoad(AddonAPI* aApi);
void AddonUnload();

// Held for the whole of AddonUnload. Nexus (stable, Loader.cpp UnloadAddon) runs each unload on a
// detached thread of its own and keeps the addon Loaded until it frees the library, with nothing
// stopping a second one meanwhile: its IsWaitingForUnload is never set, and toggling "Disable until
// update" queues an unload every time. The second call must not return while the first is still
// tearing down, since Nexus frees the library as soon as either returns.
std::mutex unloading;

AddonDefinition AddonDef = {};
HMODULE hSelf = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: hSelf = hModule; break;
    case DLL_PROCESS_DETACH: break;
    case DLL_THREAD_ATTACH: break;
    case DLL_THREAD_DETACH: break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef() {
    AddonDef.Signature = -668341; // random unused negative integer
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name = viewer::ADDON_NAME;
    AddonDef.Version.Major = VERSION_MAJOR;
    AddonDef.Version.Minor = VERSION_MINOR;
    AddonDef.Version.Build = VERSION_BUILD;
    AddonDef.Version.Revision = 0;
    AddonDef.Author = "Haskha";
    AddonDef.Description = "Yet another marker pack addon.";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = EAddonFlags_None;
    AddonDef.Provider = EUpdateProvider_GitHub;
    AddonDef.UpdateLink = "https://github.com/HasKha/gw2-markus";
    return &AddonDef;
}

void AddonLoad(AddonAPI* aApi) {
    ImGui::SetCurrentContext((ImGuiContext*)aApi->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))aApi->ImguiMalloc, (void (*)(void*, void*))aApi->ImguiFree);

    // No DL_NEXUS_LINK: its Width/Height are the swap chain's and can go stale, and the
    // world is drawn into an ImGui draw list, whose size ImGui itself reports. See
    // viewer::gameCamera.
    Mumble::Data* mumble_link = (Mumble::Data*)aApi->DataLink.Get("DL_MUMBLE_LINK");

    // The editor registers nothing: the viewer drives it, from Open Editor on. Connected
    // first, so no callback of the viewer's ever sees the hooks half written.
    marker_editor::load(aApi, mumble_link);
    viewer::connectEditor(marker_editor::hooks());
    viewer::load(aApi, mumble_link);
}

void AddonUnload() {
    // A second call waits here, then finds nothing left to stop: both unloads below do nothing
    // the second time.
    const std::lock_guard<std::mutex> lock(unloading);
    // The viewer first: once its callbacks are gone, nothing calls into the editor.
    viewer::unload();
    marker_editor::unload();
}
