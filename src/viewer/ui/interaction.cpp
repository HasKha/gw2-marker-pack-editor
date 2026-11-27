#include "viewer/ui/interaction.h"

#include <imgui.h>

#include <algorithm>

#include "viewer/ui/achievement_tooltip.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

namespace viewer::ui::interaction {

namespace {

constexpr ImGuiWindowFlags kOverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                           ImGuiWindowFlags_NoMove;

// SmallInteract's place on screen: AdvancedDefaults' InteractGearXOffset and YOffset.
constexpr float kPromptX = 0.62f;
constexpr float kPromptY = 0.58f;

// Pathing's InfoWindow, laid out over the world. Its text is DefaultFont18, 18 of Blish HUD's
// pixels, and everything else is in proportion to it, so the window is the same shape whatever
// size the text comes out.
void drawInfo(behaviors::Screen* screen, settings::InfoDisplay info, float ui_scale) {
    // One of Blish HUD's pixels at the Normal interface size, in UI pixels: its GetScaleRatio (see
    // map.h).
    constexpr float kBlishPixel = 0.897f;
    // Pathing's own font size, and the floor for text drawn over the world (docs/code_style.md),
    // which is larger than Pathing's at the Normal interface size.
    constexpr float kPathingTextPixels = 18.0f;
    constexpr float kLeastTextPixels = 20.0f;
    // In Pathing's pixels, from UiStates.InitInfo and InfoWindow: placed at AdvancedDefaults'
    // InfoWindow offsets, 512 wide, its label 350 wide at (70, 60) with 70 below it, and the close
    // button 32 square, 64 from the right and 45 down.
    constexpr float kLeft = 300.0f;
    constexpr float kTop = 200.0f;
    constexpr float kWidth = 512.0f;
    constexpr float kLabelLeft = 70.0f;
    constexpr float kLabelTop = 60.0f;
    constexpr float kLabelWidth = 350.0f;
    constexpr float kBelowLabel = 70.0f;
    constexpr float kCloseRight = 64.0f;
    constexpr float kCloseTop = 45.0f;
    constexpr float kCloseSize = 32.0f;

    const float blish = ui_scale * kBlishPixel;
    const float text = std::max(kPathingTextPixels * blish, kLeastTextPixels);
    const float unit = text / kPathingTextPixels; // screen pixels per pixel of the window's layout
    const ImVec2 at(kLeft * blish, kTop * blish);
    const bool bare = info == settings::InfoDisplay::WithoutBackground;

    // Over the world and under the mouse: what is behind it takes the clicks, as Pathing's lets
    // them through. Only the close button, a window of its own, takes any.
    ImGui::SetNextWindowPos(at, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kWidth * unit, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bare ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f) : style::INFO_BACKGROUND);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, bare ? 0.0f : ImGui::GetStyle().WindowBorderSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kLabelLeft * unit, kLabelTop * unit));
    constexpr ImGuiWindowFlags kInfoFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##marker_info", nullptr, kInfoFlags)) {
        fonts::pushOverlay(text);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kLabelWidth * unit);
        ImGui::TextUnformatted(screen->info.back().c_str());
        ImGui::PopTextWrapPos();
        fonts::pop();
        // The label's 70 below it, of which the window's padding gives 60.
        ImGui::Dummy(ImVec2(0.0f, (kBelowLabel - kLabelTop) * unit));
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // InfoWindow.PaintBeforeChildren draws the close button only with the window's art.
    if (bare) return;
    const float close = kCloseSize * unit;
    ImGui::SetNextWindowPos(ImVec2(at.x + (kWidth - kCloseRight) * unit, at.y + kCloseTop * unit), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##marker_info_close", nullptr, kOverlayFlags | ImGuiWindowFlags_NoBackground)) {
        if (ImGui::InvisibleButton("close", ImVec2(close, close))) screen->info_closed = true;
        // Drawn rather than an icon of the font, which may not be built.
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float inset = close * 0.28f;
        const ImU32 colour = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : style::INFO_CLOSE);
        const float thickness = std::max(2.0f, close * 0.08f);
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->AddLine(ImVec2(min.x + inset, min.y + inset), ImVec2(max.x - inset, max.y - inset), colour, thickness);
        list->AddLine(ImVec2(max.x - inset, min.y + inset), ImVec2(min.x + inset, max.y - inset), colour, thickness);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace

bool draw(
    behaviors::Screen* screen,
    settings::InfoDisplay info,
    float ui_scale,
    const std::string& key_name,
    bool show_prompt,
    achievements::Catalog* achievements) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    if (info != settings::InfoDisplay::Never && !screen->info_closed && !screen->info.empty() && !screen->info.back().empty()) {
        drawInfo(screen, info, ui_scale);
    }

    if (!show_prompt || !screen->prompt) return false;
    bool clicked = false;
    ImGui::SetNextWindowPos(ImVec2(display.x * kPromptX, display.y * kPromptY), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##marker_prompt", nullptr, kOverlayFlags)) {
        // Nothing until the details are in: Pathing's "Loading achievement details..." would
        // stand for good while the API is down.
        if (const achievements::Details* details =
                screen->prompt->achievement && achievements ? achievements->find(*screen->prompt->achievement) : nullptr) {
            achievement_tooltip::draw(*details, -1);
            ImGui::Separator();
        }
        const std::string text = behaviors::formatMessage(screen->prompt->text, "[" + key_name + "]");
        ImGui::PushStyleColor(ImGuiCol_Text, screen->prompt->unhides ? style::UNHIDE : style::PROMPT);
        clicked = ImGui::Selectable(text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
    return clicked;
}

} // namespace viewer::ui::interaction
