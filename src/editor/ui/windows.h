#pragma once

namespace ui::windows {

// The editor is several small windows rather than one big one, so the 3D viewport is not
// held hostage by a panel the user is not looking at. Each one is independently closable;
// the View menu and the Nexus options page are how they come back.
enum class Id { Tree, Details, Toolbar, TrailEditor, Resources, Count };

// Window title, including the ##id suffix that keeps ImGui's state per window.
const char* title(Id id);

// Stable name for the settings file. Unlike the enum's order, this may not change.
const char* key(Id id);

bool& visible(Id id);

// Hides everything, or brings back whatever was last hidden. What the keybind and the
// quick access item do: the editor as a whole goes away and comes back with its layout.
void toggleAll();

// The contents of the View menu. Call between BeginMenu and EndMenu.
void drawViewMenu();

// Checkboxes for the Nexus options page, so a closed window is always recoverable even
// with no keybind bound.
void drawOptions();

} // namespace ui::windows
