// What behaviors put on screen, drawn into ImGui frames with no device: the info window laid out as
// Pathing's, under the mouse but for its close button.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include "viewer/ui/interaction.h"

using Catch::Approx;

namespace {

class InfoFrames
{
public:
    InfoFrames()
        : context(ImGui::CreateContext()) {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1920.0f, 1080.0f);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        screen.info.push_back("Kill marsh drakes each time you WP here");
    }
    ~InfoFrames() { ImGui::DestroyContext(context); }

    InfoFrames(const InfoFrames&) = delete;
    InfoFrames& operator=(const InfoFrames&) = delete;

    // One frame with the mouse at `mouse`, its left button held when `down`.
    void run(ImVec2 mouse, bool down = false) {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = mouse;
        io.MouseDown[0] = down;
        ImGui::NewFrame();
        viewer::ui::interaction::draw(&screen, viewer::settings::InfoDisplay::Default, 1.0f, "F", false, nullptr);
        ImGui::Render();
    }

    viewer::behaviors::Screen screen;

private:
    ImGuiContext* context;
};

ImVec2 centre(const ImGuiWindow* window) {
    return ImVec2(window->Pos.x + window->Size.x * 0.5f, window->Pos.y + window->Size.y * 0.5f);
}

} // namespace

TEST_CASE("info is Pathing's window, larger, and lets the mouse through but for its X", "[interaction]") {
    InfoFrames frames;
    frames.run(ImVec2(0.0f, 0.0f));
    frames.run(ImVec2(0.0f, 0.0f));

    // At Pathing's (300, 200) of Blish HUD's pixels; its 512 wide in proportion to text of 20
    // pixels where Pathing's is 18; each to the whole pixel ImGui rounds a window to.
    const ImGuiWindow* info = ImGui::FindWindowByName("##marker_info");
    REQUIRE(info);
    CHECK(info->Pos.x == Approx(300.0f * 0.897f).margin(1.0f));
    CHECK(info->Pos.y == Approx(200.0f * 0.897f).margin(1.0f));
    CHECK(info->Size.x == Approx(512.0f * 20.0f / 18.0f).margin(1.0f));

    frames.run(centre(info));
    frames.run(centre(info));
    CHECK_FALSE(ImGui::GetIO().WantCaptureMouse);

    const ImGuiWindow* close = ImGui::FindWindowByName("##marker_info_close");
    REQUIRE(close);
    CHECK(close->Pos.x > info->Pos.x + info->Size.x * 0.75f);
    frames.run(centre(close));
    frames.run(centre(close), true);
    CHECK(ImGui::GetIO().WantCaptureMouse);
    CHECK_FALSE(frames.screen.info_closed);
    frames.run(centre(close));
    CHECK(frames.screen.info_closed);
}
