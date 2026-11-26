#pragma once

#include <string>

// The Windows desktop around the game: its clipboard, and the player's browser.
namespace viewer::desktop {

// Puts `text` on the clipboard. False when Windows would not take it.
bool copy(const std::string& text);

// Opens `url` in the player's browser.
void openInBrowser(const std::string& url);

} // namespace viewer::desktop
