#include <Windows.h>

#include <Mumble.h>
#include <Nexus.h>
#include <imgui.h>

#include "marker_editor.h"

void AddonLoad(AddonAPI* aApi);
void AddonUnload();

AddonDefinition AddonDef = {};
HMODULE hSelf = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
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
    AddonDef.Name = marker_editor::ADDON_NAME;
    AddonDef.Version.Major = VERSION_MAJOR;
    AddonDef.Version.Minor = VERSION_MINOR;
    AddonDef.Version.Build = VERSION_BUILD;
    AddonDef.Version.Revision = 0;
    AddonDef.Author = "Haskha";
    AddonDef.Description = "Create and edit marker packs in-game.";
    AddonDef.Load = AddonLoad;
    AddonDef.Unload = AddonUnload;
    AddonDef.Flags = EAddonFlags_None;
    AddonDef.Provider = EUpdateProvider_GitHub;
    AddonDef.UpdateLink = "https://github.com/HasKha/gw2-marker-pack-editor";
    return &AddonDef;
}

void AddonLoad(AddonAPI* aApi) {
    ImGui::SetCurrentContext((ImGuiContext*)aApi->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))aApi->ImguiMalloc, (void (*)(void*, void*))aApi->ImguiFree);

    // No DL_NEXUS_LINK: its Width/Height are the swap chain's and can go stale, and the
    // world is drawn into an ImGui draw list, whose size ImGui itself reports. See
    // world::fromMumble.
    Mumble::Data* mumble_link = (Mumble::Data*)aApi->DataLink.Get("DL_MUMBLE_LINK");

    marker_editor::load(aApi, mumble_link);
}

void AddonUnload() {
    marker_editor::unload();
}
