#pragma once

#include <glm/vec3.hpp>

#include <filesystem>
#include <string>

#include "editor/editor_state.h"
#include "viewer/game/camera.h"

namespace ui::trail_editor {

// What the window asked marker_editor to do this frame. The native file dialog is owned
// there — one runs at a time and its result is polled from the render callback — so a
// request comes back out rather than being started here.
enum class Request { None, Export };

// What the window keeps between frames.
struct View {
    // Kept between invocations on purpose: the whole point of "add circle" is to place several
    // of the same circle, so only the centre is refreshed each time the dialog opens.
    struct Circle {
        glm::vec3 centre{0.0f};
        float radius = 4.0f;
        int segments = 72; // 5 degrees a step, what make_circle uses
        float angle_start = 0.0f;
        float angle_end = 360.0f;
    };
    Circle circle;
    bool open_circle_popup = false; // the menu asked; the body opens it
};

// Draws the trail open in the editor into the current window: its map id, its points, and
// drag fields for the selected one. Nothing in the body needs a dialog, so unlike drawMenu
// it has nothing to ask for.
void draw(editor::State& state, View& view, const viewer::Camera& camera);

// The Trail menu. Drawn only in this window's own menu bar — it acts on the trail editor's
// state, so it has no business in the tree or the details window. Greyed out when no trail
// is open.
Request drawMenu(editor::State& state, View& view, const viewer::Camera& camera);

// Writes the working copy to a .trl the user picked.
void exportTo(editor::State& state, const std::filesystem::path& file, std::string* status);

} // namespace ui::trail_editor
