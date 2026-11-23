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

    // Whether Blish HUD's markers folder is read as well as our own. On, so a player coming
    // over from Blish HUD keeps their packs without copying anything; off for one still
    // running Blish HUD, who would otherwise see every marker twice.
    bool read_blish_folder = true;

    // Whether packs' Lua scripts run: Pathing's "Enable Lua Scripts", on by default as there.
    bool run_scripts = true;
};

// Reads the settings in `folder`. Anything missing keeps its default — except
// enable_editing, which with no viewer.json yet starts on when the editor's settings.json is
// there. An existing editor user updating in place keeps the editor; a new player who never
// asked for it does not get it.
Settings load(const std::filesystem::path& folder);

// False when the file could not be written.
bool save(const std::filesystem::path& folder, const Settings& settings);

} // namespace viewer::settings
