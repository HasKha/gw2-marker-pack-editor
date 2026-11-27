// What behaviors put on screen, drawn into ImGui frames with no device: the info window laid out as
// the settings say, under the mouse but for its close button unless it can be dragged.

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
        settings.show_interact_prompt = false;
    }
    ~InfoFrames() { ImGui::DestroyContext(context); }

    InfoFrames(const InfoFrames&) = delete;
    InfoFrames& operator=(const InfoFrames&) = delete;

    // One frame with the mouse at `mouse`, its left button held when `down`.
    viewer::ui::interaction::Drawn run(ImVec2 mouse, bool down = false) {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = mouse;
        io.MouseDown[0] = down;
        ImGui::NewFrame();
        const viewer::ui::interaction::Drawn drawn = viewer::ui::interaction::draw(&screen, &settings, 1.0f, nullptr, "F", nullptr);
        ImGui::Render();
        return drawn;
    }

    viewer::behaviors::Screen screen;
    viewer::settings::Settings settings;

private:
    ImGuiContext* context;
};

ImVec2 centre(const ImGuiWindow* window) {
    return ImVec2(window->Pos.x + window->Size.x * 0.5f, window->Pos.y + window->Size.y * 0.5f);
}

} // namespace

TEST_CASE("info is Pathing's window, larger, and lets the mouse through but for its X", "[interaction]") {
    InfoFrames frames;
    frames.settings.info_movable = false;
    frames.run(ImVec2(0.0f, 0.0f));
    frames.run(ImVec2(0.0f, 0.0f));

    // At the place saved; Pathing's 350 of label and 32 of X in proportion to text of 20 pixels
    // where Pathing's is 18, with a margin either side; each to the whole pixel ImGui rounds a
    // window to.
    const ImGuiWindow* info = ImGui::FindWindowByName("##marker_info");
    REQUIRE(info);
    CHECK(info->Pos.x == Approx(frames.settings.info_x).margin(1.0f));
    CHECK(info->Pos.y == Approx(frames.settings.info_y).margin(1.0f));
    CHECK(info->Size.x == Approx(2.0f * frames.settings.info_margin + (350.0f + 32.0f) * 20.0f / 18.0f).margin(1.0f));

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

TEST_CASE("a movable info window is dragged to a place the settings keep, its X with it", "[interaction]") {
    InfoFrames frames;
    REQUIRE(frames.settings.info_movable); // by default
    frames.run(ImVec2(0.0f, 0.0f));
    frames.run(ImVec2(0.0f, 0.0f));
    const ImGuiWindow* info = ImGui::FindWindowByName("##marker_info");
    const ImGuiWindow* close = ImGui::FindWindowByName("##marker_info_close");
    REQUIRE(info);
    REQUIRE(close);
    const ImVec2 start = info->Pos;
    const ImVec2 close_start = close->Pos;

    // Pressed in the box, moved 100 right and 50 down, let go: only the release says to save.
    const ImVec2 grab = centre(info);
    frames.run(grab);
    CHECK_FALSE(frames.run(grab, true).info_moved);
    CHECK(ImGui::GetIO().WantCaptureMouse);
    CHECK_FALSE(frames.run(ImVec2(grab.x + 100.0f, grab.y + 50.0f), true).info_moved);
    CHECK(frames.run(ImVec2(grab.x + 100.0f, grab.y + 50.0f)).info_moved);
    CHECK(frames.settings.info_x == Approx(start.x + 100.0f).margin(1.0f));
    CHECK(frames.settings.info_y == Approx(start.y + 50.0f).margin(1.0f));
    CHECK_FALSE(frames.screen.info_closed);

    frames.run(ImVec2(0.0f, 0.0f));
    CHECK(info->Pos.x == Approx(start.x + 100.0f).margin(1.0f));
    CHECK(close->Pos.x == Approx(close_start.x + 100.0f).margin(1.0f));
    CHECK(close->Pos.y == Approx(close_start.y + 50.0f).margin(1.0f));

    // The X over the box still closes it rather than dragging it.
    frames.run(centre(close));
    frames.run(centre(close), true);
    frames.run(centre(close));
    CHECK(frames.screen.info_closed);
    CHECK(frames.settings.info_x == Approx(start.x + 100.0f).margin(1.0f));
}

TEST_CASE("the prompt is a gear whose text shows only while it is hovered, and a click interacts", "[interaction]") {
    // Pathing's SmallInteract: before, the text was always out beside it.
    InfoFrames frames;
    frames.screen.info.clear();
    frames.screen.prompt = viewer::behaviors::Screen::Prompt{0, "Will trigger a script {0}"};
    frames.settings.show_interact_prompt = true;
    const auto tooltipShown = [] {
        const ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00");
        return tooltip && tooltip->Active;
    };

    // 64 of Blish HUD's pixels square, by default at SmallInteract's place on this 1920×1080 screen.
    frames.run(ImVec2(0.0f, 0.0f));
    frames.run(ImVec2(0.0f, 0.0f));
    const ImGuiWindow* gear = ImGui::FindWindowByName("##marker_prompt");
    REQUIRE(gear);
    CHECK(gear->Pos.x == Approx(1920.0f * 0.62f).margin(1.0f));
    CHECK(gear->Pos.y == Approx(1080.0f * 0.58f).margin(1.0f));
    CHECK(gear->Size.x == Approx(64.0f * 0.897f).margin(1.0f));
    CHECK_FALSE(tooltipShown());

    frames.run(centre(gear));
    CHECK(tooltipShown());
    CHECK_FALSE(frames.run(centre(gear), true).prompt_clicked);
    CHECK(frames.run(centre(gear)).prompt_clicked);

    // Wherever the settings put it, and kept on the screen.
    frames.settings.gear_x = 100.0f;
    frames.settings.gear_y = 5000.0f;
    frames.run(ImVec2(0.0f, 0.0f));
    CHECK(gear->Pos.x == Approx(100.0f).margin(1.0f));
    CHECK(gear->Pos.y + gear->Size.y == Approx(1080.0f).margin(1.0f));
}

TEST_CASE("an info window saved beyond the screen is drawn at its edge", "[interaction]") {
    // As when the resolution is made smaller than the one it was placed at.
    InfoFrames frames;
    frames.settings.info_x = 5000.0f;
    frames.settings.info_y = 5000.0f;
    frames.run(ImVec2(0.0f, 0.0f));
    frames.run(ImVec2(0.0f, 0.0f));
    const ImGuiWindow* info = ImGui::FindWindowByName("##marker_info");
    REQUIRE(info);
    CHECK(info->Pos.x + info->Size.x == Approx(1920.0f).margin(1.0f));
    CHECK(info->Pos.y + info->Size.y == Approx(1080.0f).margin(1.0f));
    CHECK(frames.settings.info_x == 5000.0f); // its saved place stands until it is dragged
}
