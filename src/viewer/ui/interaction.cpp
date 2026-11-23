#include "viewer/ui/interaction.h"

#include <imgui.h>

#include "viewer/ui/achievement_tooltip.h"

namespace viewer::ui::interaction {

namespace {

constexpr ImGuiWindowFlags kOverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                           ImGuiWindowFlags_NoMove;

// SmallInteract's place on screen: AdvancedDefaults' InteractGearXOffset and YOffset.
constexpr float kPromptX = 0.62f;
constexpr float kPromptY = 0.58f;

// SmallInteract's tints: orange for what a marker will do, light blue for a hidden marker.
const ImVec4 kPromptColour(1.0f, 142.0f / 255.0f, 50.0f / 255.0f, 1.0f);
const ImVec4 kUnhideColour(173.0f / 255.0f, 216.0f / 255.0f, 230.0f / 255.0f, 1.0f);

} // namespace

bool draw(
    const behaviors::Screen& screen,
    settings::InfoDisplay info,
    const std::string& key_name,
    bool show_prompt,
    achievements::Catalog* achievements) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    if (info != settings::InfoDisplay::Never && !screen.info.empty() && !screen.info.back().empty()) {
        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.12f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        const bool bare = info == settings::InfoDisplay::WithoutBackground;
        ImGui::SetNextWindowBgAlpha(bare ? 0.0f : 0.6f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, bare ? 0.0f : ImGui::GetStyle().WindowBorderSize);
        if (ImGui::Begin("##marker_info", nullptr, kOverlayFlags | ImGuiWindowFlags_NoInputs)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(screen.info.back().c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (!show_prompt || !screen.prompt) return false;
    bool clicked = false;
    ImGui::SetNextWindowPos(ImVec2(display.x * kPromptX, display.y * kPromptY), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##marker_prompt", nullptr, kOverlayFlags)) {
        // Nothing until the details are in: Pathing's "Loading achievement details..." would
        // stand for good while the API is down.
        if (const achievements::Details* details =
                screen.prompt->achievement && achievements ? achievements->find(*screen.prompt->achievement) : nullptr) {
            achievement_tooltip::draw(*details, -1);
            ImGui::Separator();
        }
        const std::string text = behaviors::formatMessage(screen.prompt->text, "[" + key_name + "]");
        ImGui::PushStyleColor(ImGuiCol_Text, screen.prompt->unhides ? kUnhideColour : kPromptColour);
        clicked = ImGui::Selectable(text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
    return clicked;
}

} // namespace viewer::ui::interaction
