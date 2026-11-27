#pragma once

#include <array>
#include <cstddef>

namespace ui::windows {

// The editor is several small windows rather than one big one, so the 3D viewport is not
// held hostage by a panel the user is not looking at. The tree is the main one: the session
// lasts as long as it is open, and the others are drawn only with it. Each of the others is
// closable on its own, and the main window's View menu is how it comes back.
enum class Id { Tree, Details, Toolbar, TrailEditor, Resources, Count };

constexpr size_t kCount = static_cast<size_t>(Id::Count);

// Window title, including the ##id suffix that keeps ImGui's state per window.
const char* title(Id id);

// Stable name for the settings file. Unlike the enum's order, this may not change.
const char* key(Id id);

// Which windows are on screen, each as it starts out until the settings say otherwise.
class Shown
{
public:
    Shown();

    bool& operator[](Id id) { return shown[static_cast<size_t>(id)]; }
    bool operator[](Id id) const { return shown[static_cast<size_t>(id)]; }

    bool operator==(const Shown& other) const = default;

private:
    std::array<bool, kCount> shown{};
};

// The contents of the View menu: every window but the main one. Call between BeginMenu and
// EndMenu.
void drawViewMenu(Shown& shown);

} // namespace ui::windows
