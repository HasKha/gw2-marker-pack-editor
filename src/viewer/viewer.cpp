#include "viewer/viewer.h"

#include <imgui.h>

#include <filesystem>

#include "viewer/settings.h"

namespace viewer {

namespace {

AddonAPI* api = nullptr;
std::filesystem::path folder;

// What the options page edits and saves, against what this session loaded with.
settings::Settings persisted;
bool editing_this_session = false;

EditorHooks editor;

void drawOptions() {
    // Applied at the next load rather than now, both ways. Loading the editor means
    // registering render callbacks, and Nexus holds its render mutex across every callback it
    // runs (Context::Render in Nexus's UI/UiContext.cpp), so registering from inside one would
    // deadlock. Unloading it now would throw away a pack's unsaved edits.
    if (ImGui::Checkbox("Enable pack editing", &persisted.enable_editing)) settings::save(folder, persisted);
    if (persisted.enable_editing != editing_this_session) ImGui::Text("Takes effect the next time the addon loads.");

    if (editor.draw_options) {
        ImGui::Separator();
        editor.draw_options();
    }
}

} // namespace

void load(AddonAPI* aApi) {
    api = aApi;

    // Nexus makes the directory; the files are ours.
    const char* directory = api->Paths.GetAddonDirectory(ADDON_FOLDER);
    folder = directory ? std::filesystem::path(directory) : std::filesystem::path();
    persisted = settings::load(folder);
    editing_this_session = persisted.enable_editing;

    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
}

void unload() {
    api->Renderer.Deregister(drawOptions);
    editor = {};
    api = nullptr;
}

bool editingEnabled() {
    return editing_this_session;
}

void connectEditor(const EditorHooks& hooks) {
    editor = hooks;
}

} // namespace viewer
