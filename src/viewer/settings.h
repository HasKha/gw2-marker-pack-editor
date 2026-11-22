#pragma once

#include <filesystem>

// The viewer's own settings, in `<addons>/MarkerPackEditor/viewer.json`.
namespace viewer::settings {

constexpr const char* FILE_NAME = "viewer.json";

// The editor's file, in the same folder. editor/settings.cpp writes it; the viewer only ever
// asks whether it exists, as the sign of an install that had the editor before the viewer.
constexpr const char* EDITOR_FILE_NAME = "settings.json";

struct Settings {
    // Whether the editor loads at all. Off, it is inert: no windows, keybinds, Quick Access
    // entry or WndProc handling.
    bool enable_editing = false;
};

// Reads the settings in `folder`. Anything missing keeps its default — except
// enable_editing, which with no viewer.json yet starts on when the editor's settings.json is
// there. An existing editor user updating in place keeps the editor; a new player who never
// asked for it does not get it.
Settings load(const std::filesystem::path& folder);

// False when the file could not be written.
bool save(const std::filesystem::path& folder, const Settings& settings);

} // namespace viewer::settings
