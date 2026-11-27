#pragma once

#include <imgui.h>

#include <initializer_list>

// What our windows share in how they look: the colours, and the few widgets imgui 1.80 lacks or
// that every form should draw the same way. The editor's windows use it too.
namespace viewer::ui::style {

// Titles: a pack's name in the library. Near the gold the game titles its own windows with.
inline const ImVec4 ACCENT(0.96f, 0.82f, 0.52f, 1.0f);
inline const ImVec4 LINK(0.35f, 0.65f, 1.0f, 1.0f);
inline const ImVec4 LINK_HOVERED(0.60f, 0.80f, 1.0f, 1.0f);
inline const ImVec4 WARNING(1.0f, 0.8f, 0.3f, 1.0f);
inline const ImVec4 ERROR_TEXT(1.0f, 0.4f, 0.4f, 1.0f);
// SmallInteract's tints: orange for what a marker will do, light blue for a hidden marker.
inline const ImVec4 PROMPT(1.0f, 142.0f / 255.0f, 50.0f / 255.0f, 1.0f);
inline const ImVec4 UNHIDE(173.0f / 255.0f, 216.0f / 255.0f, 230.0f / 255.0f, 1.0f);
// A marker's info window's X; its background is the player's, settings::Settings::info_background.
inline const ImVec4 INFO_CLOSE(0.75f, 0.75f, 0.75f, 1.0f);
// The description label's colour in Pathing's AchievementTooltipView: 204 of 255 grey.
inline const ImVec4 ACHIEVEMENT_DESCRIPTION(204.0f / 255.0f, 204.0f / 255.0f, 204.0f / 255.0f, 1.0f);

// The theme's text colour, fainter: what is said about something rather than the thing, such as
// a pack's author. Not TextDisabled, which is for what cannot be used.
ImVec4 muted();

// Greys out and locks what is drawn until endDisabled(), when `disabled`: imgui 1.80 has no
// BeginDisabled(), so this is its item flag, which locks a widget but draws it as ever, and a
// fainter alpha. A tooltip on a locked item needs ImGuiHoveredFlags_AllowWhenDisabled.
void beginDisabled(bool disabled);
void endDisabled();

// A button, dim and inert while not `enabled` but still hovered, so its tooltip can say why.
// `width` 0 fits the label. True when pressed while enabled.
bool button(const char* label, bool enabled = true, float width = 0.0f);

// The width of the widest of `labels` as a button, for a group of buttons drawn alike.
float buttonWidth(std::initializer_list<const char*> labels);

// A heading inside a form or a section: `icon` and `text`, a little space above it.
void subheading(const char* icon, const char* text);

// What a box for many things shows: none of them on, some, or all.
enum class TriState { Off, Mixed, On };

// A checkbox showing `state`: ticked when On, empty when Off, a dot when Mixed, which imgui 1.80's
// own mixed checkbox draws as a square. True when clicked; what that turns things to is the
// caller's.
bool triStateCheckbox(const char* label, TriState state);

// A section of a page: a tree node of `icon` and `label`, its whole row clickable, open at first
// when `default_open`. `id` names it, so that the label can change and the icon draw or not
// without the node closing; its content is in that ID's scope. True when open, and then popped
// with ImGui::TreePop().
bool section(const char* id, const char* icon, const char* label, bool default_open = false);

// A group inside a section: its subheading, and under it what follows indented by an icon's width,
// until endSubsection().
void beginSubsection(const char* icon, const char* text);
void endSubsection();

// A link, which imgui 1.80 has none of: underlined text in the link colour, lighter and with a
// hand while hovered, and `tooltip` then. True when clicked.
bool link(const char* text, const char* tooltip);

} // namespace viewer::ui::style
