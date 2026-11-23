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

    // Pathing's pack settings for behaviors (ModuleSettings.cs), with its defaults.
    bool allow_auto_hide = true; // "Allow Markers to Automatically Hide"
    bool allow_animation = true; // "Allow Markers to Animate": bounce
    bool show_interact_prompt = true; // "Allow Markers to Show Interact Gear On-Screen"
    int clipboard_consent = 0; // behaviors::ClipboardConsent: Always

    // The key that interacts with a focused marker: Blish HUD's own interact binding, F
    // unless changed, since neither it nor Nexus can read the game's. A Windows virtual key.
    int interact_key = 'F';
    int info_display = 0; // InfoDisplay: Pathing's "Marker Info Display Mode"

    // Pathing's pack settings for the world, as WorldOptions takes them.
    // Where the menu is: in Nexus's own menu, and behind an icon of ours in Nexus's bar.
    bool menu_in_nexus = true;
    bool menu_icon = true;

    bool world_pathables = true;
    float max_opacity = 1.0f;
    float view_distance = 25000.0f;
    float max_trail_speed = 10.0f;
    float marker_scale = 1.0f;
    bool combat_fade = true;

    // Pathing's map settings (ModuleSettings.InitMapSettings), with its defaults. The
    // visibilities are map_scene::Visibility.
    bool map_pathables = true; // "Show Markers on Maps"
    int map_marker_visibility = 0; // on the full map
    int map_trail_visibility = 0;
    float map_opacity = 1.0f;
    int minimap_marker_visibility = 0;
    int minimap_trail_visibility = 0;
    float minimap_opacity = 1.0f;
    bool map_above_below = true; // "Show Above/Below Indicators"
    bool map_fade_far_trails = true; // "Fade Trail Segments Which Are High Above or Below"
    bool map_tooltips = true;
    float map_trail_width = 2.0f; // Blish HUD's pixels, 0.5 to 4.5
};

// Pathing's MarkerInfoDisplayMode.
enum class InfoDisplay { Default, WithoutBackground, Never };

// Reads the settings in `folder`. Anything missing keeps its default — except
// enable_editing, which with no viewer.json yet starts on when the editor's settings.json is
// there. An existing editor user updating in place keeps the editor; a new player who never
// asked for it does not get it.
Settings load(const std::filesystem::path& folder);

// False when the file could not be written.
bool save(const std::filesystem::path& folder, const Settings& settings);

} // namespace viewer::settings
