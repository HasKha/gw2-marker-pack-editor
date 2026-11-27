#include "viewer/ui/interaction.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "viewer/ui/achievement_tooltip.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

namespace viewer::ui::interaction {

namespace {

constexpr ImGuiWindowFlags kOverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                           ImGuiWindowFlags_NoMove;

// A window of `size` whose top-left corner is saved at (`x`, `y`), kept on the screen whatever
// that place: a smaller resolution than the one it was set at may leave it beyond the edge.
ImVec2 onScreen(float x, float y, ImVec2 size) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    return ImVec2(std::clamp(x, 0.0f, std::max(0.0f, display.x - size.x)), std::clamp(y, 0.0f, std::max(0.0f, display.y - size.y)));
}

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

    // The box around the text.
    fonts::pushOverlay(text);
    const ImVec2 label = ImGui::CalcTextSize(screen->info.back().c_str(), nullptr, false, wrap);
    const ImVec2 size(margin + wrap + close + margin, std::max(margin + label.y + margin, margin + close));
    const ImVec2 at = onScreen(settings->info_x, settings->info_y, size);

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

// Pathing's SmallInteract: the game's gear, tinted, what it does a tooltip over it, at the place
// `settings` say. True when it was clicked, which interacts as the key does.
bool drawGear(
    const behaviors::Screen& screen,
    const settings::Settings& settings,
    float ui_scale,
    ImTextureID gear,
    const std::string& key_name,
    achievements::Catalog* achievements) {
    // One of Blish HUD's pixels at the Normal interface size, in UI pixels: its GetScaleRatio
    // (see map.h). The gear is 64 of them square.
    constexpr float kBlishPixel = 0.897f;
    constexpr float kGearSize = 64.0f;
    // From SmallInteract.Paint: faint while the player moves, near whole a moment after they
    // stop, a little more under the mouse; and one turn as it shows, at 20 radians a second.
    constexpr double kSubtleDelay = 0.103;
    constexpr double kSubtleDamper = 0.05;
    constexpr float kTurnRate = 20.0f;
    constexpr float kFullTurn = 6.2831853f;

    const behaviors::Screen::Prompt& prompt = *screen.prompt;
    const float size = kGearSize * ui_scale * kBlishPixel;
    bool clicked = false;
    ImGui::SetNextWindowPos(onScreen(settings.gear_x, settings.gear_y, ImVec2(size, size)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("##marker_prompt", nullptr, kOverlayFlags | ImGuiWindowFlags_NoBackground)) {
        clicked = ImGui::InvisibleButton("gear", ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();

        // Null until Nexus has made the texture: the gear is there to hover and click meanwhile.
        if (gear) {
            const double now = ImGui::GetTime();
            const double still = std::max(now - screen.moved_at, kSubtleDamper);
            const float opacity = (hovered ? 0.4f : 0.3f) + static_cast<float>(std::min((still / kSubtleDelay - kSubtleDamper) * 0.6, 0.6));
            const float turn = std::min(static_cast<float>(now - prompt.shown_at) * kTurnRate, kFullTurn);
            ImVec4 tint = prompt.unhides ? style::UNHIDE : style::PROMPT;
            tint.w *= std::min(opacity, 1.0f);

            // Turned about its centre: each corner of the square, rotated.
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 centre(min.x + size * 0.5f, min.y + size * 0.5f);
            const float c = std::cos(turn) * size * 0.5f;
            const float s = std::sin(turn) * size * 0.5f;
            const auto corner = [&](float x, float y) { return ImVec2(centre.x + x * c - y * s, centre.y + x * s + y * c); };
            ImGui::GetWindowDrawList()->AddImageQuad(
                gear,
                corner(-1.0f, -1.0f),
                corner(1.0f, -1.0f),
                corner(1.0f, 1.0f),
                corner(-1.0f, 1.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImVec2(0.0f, 1.0f),
                ImGui::GetColorU32(tint));
        }

        if (hovered) {
            ImGui::BeginTooltip();
            // Nothing until the details are in: Pathing's "Loading achievement details..." would
            // stand for good while the API is down.
            if (const achievements::Details* details =
                    prompt.achievement && achievements ? achievements->find(*prompt.achievement) : nullptr) {
                achievement_tooltip::draw(*details, -1);
                ImGui::Separator();
            }
            ImGui::TextUnformatted(behaviors::formatMessage(prompt.text, "[" + key_name + "]").c_str());
            ImGui::EndTooltip();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return clicked;
}

} // namespace

Drawn draw(
    behaviors::Screen* screen,
    settings::Settings* settings,
    float ui_scale,
    ImTextureID gear,
    const std::string& key_name,
    achievements::Catalog* achievements) {
    Drawn drawn;
    if (static_cast<settings::InfoDisplay>(settings->info_display) != settings::InfoDisplay::Never && !screen->info_closed &&
        !screen->info.empty() && !screen->info.back().empty()) {
        drawn.info_moved = drawInfo(screen, settings);
    }

    if (!settings->show_interact_prompt || !screen->prompt) return drawn;
    drawn.prompt_clicked = drawGear(*screen, *settings, ui_scale, gear, key_name, achievements);
    return drawn;
}

} // namespace viewer::ui::interaction
