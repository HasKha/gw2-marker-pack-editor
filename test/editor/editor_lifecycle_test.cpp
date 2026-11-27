// The editor runs on demand: nothing of it is bound with Nexus until Open Editor, and once
// it has nothing open and nothing on screen it stops, giving Ctrl+Z back to the game.

#include <imgui.h>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "editor/marker_editor.h"

namespace {

// The binds Nexus holds a handler for, as a fake API sees them registered and deregistered.
std::set<std::string> bound;

void registerBind(const char* id, KEYBINDS_PROCESS, const char*) {
    bound.insert(id);
}
void deregisterBind(const char* id) {
    bound.erase(id);
}
void setText(const char*, const char*, const char*) {}
void log(ELogLevel, const char*, const char*) {}
const char* noDirectory(const char*) {
    return nullptr;
}

// A frame of ImGui with no device behind it, enough for the editor's windows to be drawn.
class Frames
{
public:
    Frames()
        : context(ImGui::CreateContext()) {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Frames() { ImGui::DestroyContext(context); }

    Frames(const Frames&) = delete;
    Frames& operator=(const Frames&) = delete;

    void run(const viewer::EditorHooks& hooks) {
        ImGui::NewFrame();
        hooks.render();
        ImGui::Render();
    }

private:
    ImGuiContext* context;
};

} // namespace

TEST_CASE("the editor binds nothing until it starts, and lets go once closed", "[editor]") {
    AddonAPI api{};
    api.Log = log;
    api.Paths.GetAddonDirectory = noDirectory;
    api.InputBinds.RegisterWithString = registerBind;
    api.InputBinds.Deregister = deregisterBind;
    api.Localization.Set = setText;
    bound.clear();
    Frames frames;

    marker_editor::load(&api, nullptr);
    const viewer::EditorHooks hooks = marker_editor::hooks();
    CHECK_FALSE(marker_editor::running());
    CHECK(bound.empty());
    // Not running, a click on the world is the game's.
    CHECK(hooks.wnd_proc(nullptr, WM_LBUTTONDOWN, 0, 0) == 1);

    hooks.start();
    CHECK(marker_editor::running());
    CHECK(bound == std::set<std::string>{marker_editor::KEYBIND_UNDO_ID, marker_editor::KEYBIND_REDO_ID});
    CHECK(marker_editor::windows()[ui::windows::Id::Tree]);
    frames.run(hooks);
    CHECK(marker_editor::running()); // its tree is on screen

    // Its main window gone, the editor is closed, whatever else was open.
    marker_editor::windows()[ui::windows::Id::Tree] = false;
    REQUIRE(marker_editor::windows()[ui::windows::Id::Details]);
    frames.run(hooks);
    CHECK_FALSE(marker_editor::running());
    CHECK(bound.empty());

    // And starts again, with the tree back.
    hooks.start();
    CHECK(marker_editor::running());
    CHECK(marker_editor::windows()[ui::windows::Id::Tree]);
    CHECK(bound.size() == 2);

    marker_editor::unload();
    CHECK_FALSE(marker_editor::running());
    CHECK(bound.empty());
}
