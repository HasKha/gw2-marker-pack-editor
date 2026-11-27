#pragma once

#include <filesystem>
#include <string>

// The Windows desktop around the game: its clipboard, the player's browser and File Explorer.
namespace viewer::desktop {

// Puts `text` on the clipboard. False when Windows would not take it.
bool copy(const std::string& text);

// Opens `url` in the player's browser.
void openInBrowser(const std::string& url);

// Opens `folder` in File Explorer.
void openFolder(const std::filesystem::path& folder);

// True only when the OS cursor is actually on screen. The game hides it while the mouse turns
// the camera, and Nexus stops updating ImGui's MousePos then (UiInput.cpp): the last known
// position lingers, and would let a camera drag click whatever it was last pointing at.
bool cursorVisible();

} // namespace viewer::desktop
