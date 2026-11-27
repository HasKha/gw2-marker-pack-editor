#include "viewer/ui/interaction.h"

#include <imgui.h>

#include <algorithm>
#include <array>

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

// Pathing's InfoWindow over the world, in a box as `settings` say. The text wraps at the width of
// Pathing's label and the X is in proportion to it, so the box keeps its shape whatever size the
// text is. True when the box was let go after a drag.
bool drawInfo(behaviors::Screen* screen, settings::Settings* settings) {
    // From InfoWindow, in pixels of Pathing's text, DefaultFont18: its label 350 wide, its close
    // button 32 square.
    constexpr float kPathingTextPixels = 18.0f;
    constexpr float kLabelWidth = 350.0f;
    constexpr float kCloseSize = 32.0f;

    const float text = settings->info_text_pixels;
    const float unit = text / kPathingTextPixels; // screen pixels per pixel of Pathing's layout
    const float margin = settings->info_margin;
    const bool bare = static_cast<settings::InfoDisplay>(settings->info_display) == settings::InfoDisplay::WithoutBackground;
    // InfoWindow.PaintBeforeChildren draws the close button only with the window's art. It sits
    // half a margin in from the top-right corner, half a margin from the text, which wraps short
    // of it.
    const float close = bare ? 0.0f : kCloseSize * unit;
    const float wrap = kLabelWidth * unit;

    // The box around the text, kept on the screen whatever its saved place: a smaller resolution
    // may leave that beyond it.
    fonts::pushOverlay(text);
    const ImVec2 label = ImGui::CalcTextSize(screen->info.back().c_str(), nullptr, false, wrap);
    const ImVec2 size(margin + wrap + close + margin, std::max(margin + label.y + margin, margin + close));
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 at(
        std::clamp(settings->info_x, 0.0f, std::max(0.0f, display.x - size.x)),
        std::clamp(settings->info_y, 0.0f, std::max(0.0f, display.y - size.y)));

    // Over the world: unless it can be moved, what is behind it takes the clicks, as Pathing's lets
    // them through, and only the close button, a window of its own, takes any. Never brought in
    // front of that button by a click. Laid out by hand with no padding, so the drag handle is
    // the box exactly and nothing makes it grow.
    bool moved = false;
    const std::array<float, 4>& background = settings->info_background;
    ImGui::SetNextWindowPos(at, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        bare ? ImVec4(0.0f, 0.0f, 0.0f, 0.0f) : ImVec4(background[0], background[1], background[2], background[3]));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, bare ? 0.0f : ImGui::GetStyle().WindowBorderSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoScrollWithMouse | (settings->info_movable ? 0 : ImGuiWindowFlags_NoInputs);
    if (ImGui::Begin("##marker_info", nullptr, flags)) {
        if (settings->info_movable) {
            ImGui::InvisibleButton("drag", size);
            if (ImGui::IsItemActive()) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                settings->info_x = at.x + delta.x;
                settings->info_y = at.y + delta.y;
            }
            moved = ImGui::IsItemDeactivated();
        }
        ImGui::SetCursorPos(ImVec2(margin, margin));
        ImGui::PushTextWrapPos(margin + wrap);
        ImGui::TextUnformatted(screen->info.back().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    fonts::pop();

    if (bare) return moved;
    // Where this frame's box is, not where a drag has just put the next: the two move together.
    ImGui::SetNextWindowPos(ImVec2(at.x + size.x - margin * 0.5f - close, at.y + margin * 0.5f), ImGuiCond_Always);
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
    return moved;
}

} // namespace

Drawn draw(behaviors::Screen* screen, settings::Settings* settings, const std::string& key_name, achievements::Catalog* achievements) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;

    Drawn drawn;
    if (static_cast<settings::InfoDisplay>(settings->info_display) != settings::InfoDisplay::Never && !screen->info_closed &&
        !screen->info.empty() && !screen->info.back().empty()) {
        drawn.info_moved = drawInfo(screen, settings);
    }

    if (!settings->show_interact_prompt || !screen->prompt) return drawn;
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
        drawn.prompt_clicked = ImGui::Selectable(text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
    return drawn;
}

} // namespace viewer::ui::interaction
