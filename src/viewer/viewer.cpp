#include "viewer/viewer.h"

#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "util/job_queue.h"
#include "util/kept_file.h"
#include "util/path_text.h"
#include "viewer/behaviors/festivals.h"
#include "viewer/behaviors/states.h"
#include "viewer/behaviors/update.h"
#include "viewer/desktop.h"
#include "viewer/frame_log.h"
#include "viewer/game/camera.h"
#include "viewer/game/game_binds.h"
#include "viewer/game/interact_key.h"
#include "viewer/game/mumble.h"
#include "viewer/game/rtapi.h"
#include "viewer/gw2_api/account.h"
#include "viewer/gw2_api/achievements.h"
#include "viewer/gw2_api/api_key.h"
#include "viewer/live/runtime.h"
#include "viewer/maps/map.h"
#include "viewer/maps/map_scene.h"
#include "viewer/maps/maps.h"
#include "viewer/packs/blish_import.h"
#include "viewer/packs/library.h"
#include "viewer/packs/pack_folder.h"
#include "viewer/packs/registry.h"
#include "viewer/render/good_render.h"
#include "viewer/render/overlay.h"
#include "viewer/render/recycled.h"
#include "viewer/render/scene.h"
#include "viewer/render/textures.h"
#include "viewer/script/engine.h"
#include "viewer/session.h"
#include "viewer/settings.h"
#include "viewer/store.h"
#include "viewer/ui/diagnostics.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/icon_data.h"
#include "viewer/ui/import_window.h"
#include "viewer/ui/interaction.h"
#include "viewer/ui/main_menu.h"
#include "viewer/ui/map_icon_data.h"
#include "viewer/ui/map_menu.h"
#include "viewer/ui/pack_catalog.h"
#include "viewer/ui/script_console.h"
#include "viewer/utc.h"

namespace viewer {

namespace {

// ── What outlives a session ─────────────────────────────────────────────────────
// Written by Nexus's threads, or reached by the editor before the viewer loads. A callback Nexus
// had begun may still finish after unload deregistered it, so what one writes is never freed.

EditorHooks editor;

// The images of every pack, the editor's too, handed to Nexus and not made yet: see
// TextureUploads. The editor takes its address at its own load, before ours.
TextureUploads texture_uploads;

// Our icon in Nexus's bar was clicked: the menu opens under it on the next render. Set from
// Nexus's input handling, on a thread of its own, as is the bind turning All Markers on or off.
std::atomic<bool> menu_requested{false};
std::atomic<bool> all_markers_requested{false};

// The interact key and Escape, heard in the window procedure, which runs on the game's window
// thread while the frame runs on its render thread.
InteractKey interact_key;
std::atomic<bool> interact_pressed{false};
std::atomic<bool> escape_released{false};
// A right click, for the map's marker menu: ImGui never sees one over the map (ClickTracker).
ui::map_menu::ClickTracker right_click; // window thread only
std::atomic<bool> right_clicked{false};

constexpr const char* kMenuPopup = "##marker_pack_menu";
const std::vector<char> kNoNodes;

std::unique_ptr<Session> session;

// The session while the callbacks below may use it: set once it is made, cleared by unload()
// before anything of it goes. Apart from `session` because Nexus can call us after we deregister:
// its Addons page keeps its own copy of drawOptions in its list of addons and calls it until it
// builds that list again, while unload() runs on a thread of Nexus's own (stable, Addons.cpp and
// Loader.cpp UnloadAddon). No lock is needed: every callback runs inside Nexus's frame, under the
// lock CUiContext::Render holds and Renderer.Deregister takes, so once detach() has deregistered,
// a call that saw the session has returned, and every later one sees null.
std::atomic<Session*> live{nullptr};

// ── What Nexus calls ────────────────────────────────────────────────────────────

void preRender() {
    if (Session* running = live.load()) running->preRender();
}

void render() {
    if (Session* running = live.load()) running->render();
}

void drawOptions() {
    if (Session* running = live.load()) running->drawOptions();
}

// The menu on our icon.
void drawMenuItems() {
    if (Session* running = live.load()) running->drawItems(true);
}

// Our items in Nexus's own menu, without the separator above Manage Packs: Nexus draws one
// between each addon's items already.
void drawNexusMenuItems() {
    if (Session* running = live.load()) running->drawItems(false);
}

// Where a mouse message puts the cursor, in the game window's client pixels.
glm::ivec2 cursorAt(LPARAM flags) {
    return glm::ivec2(GET_X_LPARAM(flags), GET_Y_LPARAM(flags));
}

// Hears the interact key, Escape and the right click, and passes them all on: the game interacts
// with what it has in front of it as well, as it does with Blish HUD running, and FlatMap does
// not block a right click either. Reaches nothing of the session.
UINT onWndProc(HWND window, UINT message, WPARAM key, LPARAM flags) {
    // The editor first: a click it picks a marker with is not the game's.
    if (editor.wnd_proc && editor.wnd_proc(window, message, key, flags) == 0) return 0;
    const bool first_press = (flags & (LPARAM{1} << 30)) == 0;
    if (message == WM_KEYDOWN && first_press && interact_key.matches(key)) interact_pressed = true;
    if (message == WM_KEYUP && key == VK_ESCAPE) escape_released = true;
    if (message == WM_RBUTTONDOWN) right_click.down(cursorAt(flags));
    if (message == WM_MOUSEMOVE) right_click.moved(cursorAt(flags), desktop::cursorVisible());
    if (message == WM_RBUTTONUP && right_click.up(desktop::cursorVisible())) right_clicked = true;
    return message;
}

void onMenuKeybind(const char*, bool is_release) {
    if (!is_release) menu_requested = true;
}

void onAllMarkersKeybind(const char*, bool is_release) {
    if (!is_release) all_markers_requested = true;
}

} // namespace

// ── Load and unload ─────────────────────────────────────────────────────────────

Session::Session(AddonAPI* nexus, Mumble::Data* mumble)
    : api(nexus)
    , mumble_link(mumble) {
    nexus_link = static_cast<const NexusLinkData*>(api->DataLink.Get("DL_NEXUS_LINK"));

    // Nexus makes the directory; the files are ours.
    const char* directory = api->Paths.GetAddonDirectory(ADDON_FOLDER);
    folder = directory ? std::filesystem::path(directory) : std::filesystem::path();
    // Before anything reads from it. The editor asked for the folder already, so Nexus has made
    // it, but reads nothing from it before Open Editor.
    if (!folder.empty()) {
        const std::filesystem::path addons = (folder.has_filename() ? folder : folder.parent_path()).parent_path();
        const std::string error = store::adoptFolder(addons / OLD_ADDON_FOLDER, folder);
        if (!error.empty()) api->Log(ELogLevel_WARNING, ADDON_NAME, error.c_str());
    }
    persisted = settings::load(folder);
    // Read here, before there is a frame to stall; from here on every file goes through `jobs`.
    account_progress.open(inFolder(account::CACHE_FILE_NAME), persisted.api_key, utcNow(), &jobs);
    achievement_details.open(inFolder(achievements::CACHE_FILE_NAME), &jobs);
    states.load(folder);
    if (const char* game = api->Paths.GetGameDirectory()) {
        interact_key.open(std::filesystem::path(game) / "addons" / "Nexus" / game_binds::FILE_NAME);
    }
    festivals.start();
    maps.open(inFolder(map::CACHE_FILE_NAME));

    script::Host host;
    host.installed = &installed;
    host.live = &live_map;
    host.category_states = &states.categories;
    host.storage = &states.storage;
    host.mumble = &mumble_state;
    host.account = &account_progress.progress();
    // User.SetClipboard: only "Never" stops a script, as in Pathing's Lib/User.cs.
    host.set_clipboard = [this](const std::string& text, const std::string& message) {
        return persisted.clipboard_consent != static_cast<int>(behaviors::ClipboardConsent::Never) && setClipboard(text, message);
    };
    host.show_message = [this](const std::string& text) { api->UI.SendAlert(text.c_str()); };
    host.show_info = [this](const std::string& text) { screen.addInfo(text); };
    host.hide_info = [this](const std::string& text) { screen.removeInfo(text); };
    installed.api = api;
    installed.jobs = &jobs;
    installed.uploads = &texture_uploads;
    scripts = std::make_unique<script::Engine>(host);

    library.open(inFolder(catalog::STATE_FILE_NAME), &jobs);
    pack_folder.open(inFolder(PACKS_FOLDER), &jobs, api);
    // Offered once, by itself, when Blish HUD has anything to bring.
    if (!persisted.import_offered) {
        importer.find(registry::blishMarkersFolder());
        windows.import = !importer.found().empty();
        if (!windows.import) persisted.import_offered = true;
    }
}

Session::~Session() {
    saveStates();
    publish(nullptr);
    maps.stop();
}

void Session::attach() {
    good_render.emplace(mumble_link);
    rtapi::load(api);

    api->Renderer.Register(ERenderType_PreRender, viewer::preRender);
    api->Renderer.Register(ERenderType_Render, viewer::render);
    api->Renderer.Register(ERenderType_OptionsRender, viewer::drawOptions);
    api->WndProc.Register(onWndProc);
    // "(null)": no default bind; the player sets one in Nexus. Our icon presses it.
    // Nexus presses a shortcut's bind for a left click on it and has no other way to say so, so
    // the menu has a bind: unset, and named for what it is there for.
    api->InputBinds.RegisterWithString(KEYBIND_MENU_ID, onMenuKeybind, "(null)");
    api->Localization.Set(KEYBIND_MENU_ID, "en", "Open Markus Menu");
    api->InputBinds.RegisterWithString(KEYBIND_ALL_MARKERS_ID, onAllMarkersKeybind, "(null)");
    api->Localization.Set(KEYBIND_ALL_MARKERS_ID, "en", "Toggle Markers"); // Pathing's name for it
    api->Textures.GetOrCreateFromMemory(ICON_TEXTURE_ID, const_cast<unsigned char*>(ui::icon_data::kIcon), sizeof(ui::icon_data::kIcon));
    api->Textures.GetOrCreateFromMemory(
        ICON_HOVER_TEXTURE_ID,
        const_cast<unsigned char*>(ui::icon_data::kIconHover),
        sizeof(ui::icon_data::kIconHover));
    showInNexusMenu(persisted.menu_in_nexus);
    showIcon(persisted.menu_icon);
}

void Session::detach() {
    // First: each waits for Nexus's frame to end, so after them no callback is still using the
    // session (see `live`), and what follows tears down nothing a callback is reading.
    api->Renderer.Deregister(viewer::preRender);
    api->Renderer.Deregister(viewer::render);
    api->Renderer.Deregister(viewer::drawOptions);

    good_render.reset();
    rtapi::unload();

    api->WndProc.Deregister(onWndProc);
    showInNexusMenu(false);
    showIcon(false);
    api->InputBinds.Deregister(KEYBIND_MENU_ID);
    api->InputBinds.Deregister(KEYBIND_ALL_MARKERS_ID);
}

// ── The menu ────────────────────────────────────────────────────────────────────
// One list of items, shown in up to three places: Nexus's own menu, the right-click menu of our
// icon in Nexus's bar, and a popup of ours under the icon on a left click (render). The options
// page chooses the first two; the popup comes with the icon. Changing them from the options page,
// Nexus's or our window's, is safe: Nexus holds its quick access lock only while drawing the bar.

// Adds our items to Nexus's own menu, the one on its icon, or takes them out.
void Session::showInNexusMenu(bool show) {
    if (show && !in_nexus_menu) {
        api->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawNexusMenuItems);
    } else if (!show && in_nexus_menu) {
        api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
    }
    in_nexus_menu = show;
}

// Adds our icon to Nexus's bar, with our items as its right-click menu, or takes both out. A left
// click on it presses the menu's bind (attach), which opens the popup.
void Session::showIcon(bool show) {
    if (show && !icon_shown) {
        api->QuickAccess.Add(SHORTCUT_ID, ICON_TEXTURE_ID, ICON_HOVER_TEXTURE_ID, KEYBIND_MENU_ID, ADDON_NAME);
        api->QuickAccess.AddContextMenu(SHORTCUT_MENU_ID, SHORTCUT_ID, drawMenuItems);
    } else if (!show && icon_shown) {
        api->QuickAccess.RemoveContextMenu(SHORTCUT_MENU_ID);
        api->QuickAccess.Remove(SHORTCUT_ID);
    }
    icon_shown = show;
}

void Session::drawItems(bool separator) {
    // Walked while the menu is open only, and a map holds thousands at most.
    const std::vector<char> on_map = persisted.smart_categories ? runtime::nodesOnMap(installed.tree, live_map) : std::vector<char>{};

    ui::main_menu::Context context;
    context.tree = &installed.tree;
    context.states = &states.categories;
    context.scripts = persisted.run_scripts ? scripts.get() : nullptr;
    context.manage_packs = [this] { windows.catalog = true; };
    context.open_options = [this] { windows.options = true; };
    if (editor.start) context.open_editor = editor.start;
    context.on_map = persisted.smart_categories ? &on_map : nullptr;
    context.show_all = &show_all_menus;
    context.account = &account_progress.progress();
    context.auto_hide = persisted.allow_auto_hide;
    context.achievements = &achievement_details;
    context.achievement_tooltips = persisted.achievement_tooltips;
    context.select_all = persisted.menu_select_all;
    context.separator = separator;
    // Our font for its icons, pushed here since Nexus draws its menu and our icon's outside our render.
    ui::fonts::push();
    // A player's own toggle is written at once, not with the scripts' changes.
    if (ui::main_menu::drawItems(context)) saveStates();
    ui::fonts::pop();
}

// ── The session's work ──────────────────────────────────────────────────────────

// `name` in our folder, or nothing while there is no folder.
std::filesystem::path Session::inFolder(const std::filesystem::path& name) const {
    return folder.empty() ? folder : folder / name;
}

// Writes `persisted` as it is now, on the worker: whether it landed, which only the API key's
// message waits for.
std::future<bool> Session::saveSettings() {
    return jobs.ask([into = folder, saved = persisted] { return settings::save(into, saved); });
}

// Every few seconds: what changed is handed over to be written, and Nexus's binds are looked at
// again.
void Session::saveStates() {
    states.save(&jobs);
    jobs.post([] { interact_key.read(); });
}

// The clipboard, for a `copy` marker and User.SetClipboard alike, telling the player with
// `message`. Whether a pack may copy at all is the caller's to ask.
bool Session::setClipboard(const std::string& text, const std::string& message) {
    const bool copied = desktop::copy(text);
    if (copied) api->UI.SendAlert(message.c_str());
    return copied;
}

void Session::replaceLiveMap(int map_id) {
    // ReappearOnMapChange waits for the map, not for the packs to change under it.
    if (map_id != live_map.map_id) states.timers.mapChanged();
    live_map = runtime::liveMap(installed, map_id);
    scripts_stale = true;
    // Every entity is new, and what the old ones put on screen goes with them, as Pathing
    // unloads its entities.
    screen = behaviors::Screen{};
}

// The live map, and the scripts' handles into it, index the installed packs: whatever installs
// or removes one leaves them pointing at packs that moved, until they are built again. Whether
// or not there is a camera — the menu removes packs at character select too.
void Session::refreshLiveMap(int map_id) {
    if (live_map.map_id != map_id || live_map.revision != installed.revision) replaceLiveMap(map_id);
}

behaviors::Host Session::behaviorHost() {
    behaviors::Host host;
    host.states = &states.timers;
    host.categories = &states.categories;
    host.tree = &installed.tree;
    host.screen = &screen;
    host.copy = [this](const std::string& text, const std::string& message) { return setClipboard(text, message); };
    // Script.Update waits until every pack is in, and so do the engine's handles.
    if (persisted.run_scripts && !scripts_stale) {
        host.script = [this](const behaviors::ScriptCall& call, behaviors::Entity entity, behaviors::Hook hook, bool flag) {
            return scripts->callBehavior(call, entity, hook, flag);
        };
    }
    host.options.auto_hide = persisted.allow_auto_hide;
    host.options.animate = persisted.allow_animation;
    host.options.show_hidden = show_hidden;
    host.options.clipboard = static_cast<behaviors::ClipboardConsent>(persisted.clipboard_consent);
    return host;
}

behaviors::Frame Session::behaviorFrame() {
    return behaviors::Frame{&mumble_state, utcNow(), ImGui::GetTime(), &festivals.active(), &account_progress.progress()};
}

// Hands the fonts the game's device as soon as Nexus has a swap chain to take it from.
void Session::takeFontDevice() {
    if (font_device || !api->SwapChain) return;
    auto* swap_chain = static_cast<IDXGISwapChain*>(api->SwapChain);
    if (SUCCEEDED(swap_chain->GetDevice(IID_PPV_ARGS(&font_device)))) fonts.setDevice(font_device.Get());
}

// Screen pixels per UI pixel, as the game draws its minimap and map: the interface size, and
// the DPI scale the options say to take.
float Session::uiScale() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float dpi_scale = 1.0f;
    switch (static_cast<settings::DpiScaling>(persisted.map_dpi_scaling)) {
    case settings::DpiScaling::None: break;
    case settings::DpiScaling::Nexus:
        if (nexus_link && nexus_link->Scaling > 0.0f) return nexus_link->Scaling;
        break;
    case settings::DpiScaling::System: {
        // Windows' display scale. The system's, not the game window's monitor's as Nexus and Blish
        // HUD ask: the two differ only across monitors of different scales, which Custom is there for.
        const UINT dpi = GetDpiForSystem();
        dpi_scale = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
        break;
    }
    case settings::DpiScaling::Custom: dpi_scale = persisted.map_dpi_custom; break;
    }
    return map::uiScale(mumble_state.ui_size, dpi_scale, glm::vec2(display.x, display.y));
}

// Nexus's IsGameplay, which is off on a loading screen: MumbleLink is not written there, and
// keeps the last frame before it. Taken as on without the link, rather than never drawing.
bool Session::inGameplay() const {
    return !nexus_link || nexus_link->IsGameplay;
}

// One of the game's icons compiled in (map_icon_data.h), as Nexus's texture `id`. Nexus returns
// the texture it already made for an identifier, so asking every frame decodes each once; null
// until it is ready.
ImTextureID Session::gameIcon(const char* id, std::span<const unsigned char> png) {
    const Texture* texture = api->Textures.GetOrCreateFromMemory(id, const_cast<unsigned char*>(png.data()), png.size());
    return texture ? static_cast<ImTextureID>(texture->Resource) : nullptr;
}

// The above or below image the maps draw on an icon's corner, which the options page shows too.
ImTextureID Session::aboveBelowImage(bool above) {
    return above ? gameIcon("TEX_MarkerPackEditor_Above", ui::map_icon_data::kAbove)
                 : gameIcon("TEX_MarkerPackEditor_Below", ui::map_icon_data::kBelow);
}

// ── Present ─────────────────────────────────────────────────────────────────────

// What the world scene is built with: the options, and what the game is doing.
WorldOptions Session::worldOptions() {
    WorldOptions options;
    options.enabled = persisted.world_pathables;
    options.max_opacity = persisted.max_opacity;
    options.view_distance = persisted.view_distance;
    options.view_fade = persisted.view_fade;
    options.max_trail_speed = persisted.max_trail_speed;
    options.marker_scale = persisted.marker_scale;
    options.combat_fade = persisted.combat_fade;
    options.in_combat = mumble_state.is_in_combat;
    options.in_gameplay = inGameplay();
    options.show_hidden = show_hidden;
    options.occlusion = static_cast<Occlusion>(persisted.occlusion);
    return options;
}

// What the maps are handed at Present: the live maps, as the world scene is, and the options.
Maps::Frame Session::mapFrame(const TextureLookup& texture) {
    Maps::Frame frame;
    frame.mumble = &mumble_state;
    frame.in_gameplay = inGameplay();
    frame.world_drawn = stats.last_drawn.world;
    if (good_render && good_render->service() == good_render_sink::Service::Serving) frame.game_map = good_render_sink::frameMap();
    frame.seconds = ImGui::GetTime();
    frame.live = &live_map;
    frame.visible = &visible.nodes;
    frame.texture = texture;
    frame.edited = edited.live;
    frame.edited_texture = edited.texture;

    map_scene::Options& options = frame.options;
    options.enabled = persisted.map_pathables;
    options.map_markers = static_cast<map_scene::Visibility>(persisted.map_marker_visibility);
    options.map_trails = static_cast<map_scene::Visibility>(persisted.map_trail_visibility);
    options.minimap_markers = static_cast<map_scene::Visibility>(persisted.minimap_marker_visibility);
    options.minimap_trails = static_cast<map_scene::Visibility>(persisted.minimap_trail_visibility);
    options.map_opacity = persisted.map_opacity;
    options.minimap_opacity = persisted.minimap_opacity;
    options.above_below = persisted.map_above_below;
    options.fade_far_trails = persisted.map_fade_far_trails;
    options.trail_width = persisted.map_trail_width;
    options.show_hidden = show_hidden;

    frame.ui_scale = uiScale();
    frame.above = aboveBelowImage(true);
    frame.below = aboveBelowImage(false);
    return frame;
}

// The frame that ended with the Present beginning at `started`, logged when our work in it took
// long. Loading screens and the game in the background included: what we do there is ours all
// the same.
void Session::logSlowFrame(std::chrono::steady_clock::time_point started) {
    size_t skipped = 0;
    if (!persisted.log_slow_frames || !stats.slow_frames.take(stats.steps.total(), persisted.slow_frame_ms, started, &skipped)) return;
    frame_log::Frame frame;
    frame.interval = std::chrono::duration<double, std::milli>(started - stats.last_present).count();
    frame.steps = stats.steps;
    double slowest = 0.0;
    for (size_t pack = 0; pack < installed.packs.size(); ++pack) {
        if (const double seconds = scripts->frameSeconds(pack); seconds > slowest) {
            slowest = seconds;
            frame.slowest_scripts = util::pathText(installed.packs[pack]->loaded->path.filename());
        }
    }
    frame.map_id = live_map.map_id;
    if (const std::shared_ptr<const Scene> scene = published()) frame.quads = scene->items.size();
    frame.lua_bytes = scripts->heapBytes();
    frame.jobs = jobs.unfinished();
    frame.uploads = texture_uploads.images();
    frame.upload_pixels = texture_uploads.pixels();
    frame.loading_packs = pack_folder.loading();
    frame.skipped = skipped;
    api->Log(ELogLevel_INFO, ADDON_NAME, frame_log::line(frame).c_str());
}

// Everything is decided here, at Present, before the next frame is drawn: GoodRender's
// moments only draw what this publishes, because a moment runs inside the game's frame and
// anything slow there stalls the game.
void Session::preRender() {
    const auto started = std::chrono::steady_clock::now();
    stats.steps.moments = good_render_sink::takeMomentMilliseconds();
    if (stats.last_present != std::chrono::steady_clock::time_point{}) logSlowFrame(started);
    stats.last_present = started;
    stats.steps = {};

    frame_log::Laps laps;
    texture_uploads.nextFrame();
    pack_folder.update(&installed, &library.downloads);
    library.updateStale(pack_folder.local(), pack_folder.folder());
    account_progress.update(std::chrono::steady_clock::now(), utcNow());
    achievement_details.update(std::chrono::steady_clock::now());
    laps.lap(&stats.steps.packs);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link, rtapi::data(), glm::vec2(display.x, display.y));

    refreshLiveMap(camera.valid ? static_cast<int>(camera.map_id) : live_map.map_id);
    laps.lap(&stats.steps.live_map);
    mumble_state = mumbleState(mumble_link);

    // The scripts: reset onto the live map once every pack is in, as Pathing runs them once a
    // map's packs have loaded; then ticked every frame.
    if (persisted.run_scripts) {
        if (scripts_stale && !pack_folder.loading()) {
            scripts->reset();
            scripts->loadPacks();
            scripts_stale = false;
        }
        if (!scripts_stale) scripts->tick(ImGui::GetTime(), ImGui::GetIO().DeltaTime);
    }
    laps.lap(&stats.steps.scripts);

    // Behaviors see what scripts did this frame, and the scene what both did.
    visible.update(installed, states.categories);
    laps.lap(&stats.steps.visibility);

    // One frame of behaviors, after the scripts have ticked.
    const behaviors::Host host = behaviorHost();
    const behaviors::Frame frame = behaviorFrame();
    behaviors::update(&live_map, visible.nodes, host, frame);
    laps.lap(&stats.steps.behaviors);

    // Not while typing — in chat, or in one of our own text fields.
    const bool typing = mumble_state.is_text_input_focused || ImGui::GetIO().WantTextInput;
    if (interact_pressed.exchange(false) && !typing) behaviors::interact(&live_map, host, frame);
    // UiStates clears the info on Escape.
    if (escape_released.exchange(false)) screen.info.clear();
    laps.lap(&stats.steps.interaction);

    visible.update(installed, states.categories);
    runtime::startFadeIns(&live_map, visible.nodes, ImGui::GetTime());
    laps.lap(&stats.steps.visibility);
    if (states.due()) saveStates();
    laps.lap(&stats.steps.saves);
    edited.live = editor.edited_live ? editor.edited_live(live_map.map_id, &edited.texture) : nullptr;
    if (edited.live) runtime::startFadeIns(edited.live, kNoNodes, ImGui::GetTime());
    laps.lap(&stats.steps.editor);

    const TextureLookup texture = [this](size_t pack, const std::string& path) { return runtime::packImage(installed, pack, path); };

    std::vector<SceneLayer> layers{SceneLayer{&live_map, &visible.nodes, texture, false}};
    if (edited.live) layers.push_back(SceneLayer{edited.live, &kNoNodes, edited.texture, true});

    const std::shared_ptr<Scene> next = scenes.take();
    buildScene(layers, camera, static_cast<float>(ImGui::GetTime()), worldOptions(), next.get());
    scenes.keep(next);
    publish(next);
    stats.scene_camera = camera.source;
    laps.lap(&stats.steps.scene);

    maps.build(mapFrame(texture));
    laps.lap(&stats.steps.maps);
    stats.present_times.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
}

// ── Render ──────────────────────────────────────────────────────────────────────

// The map's tooltip and menu, over what was published this frame. None on the compass while the
// full map is up or MumbleLink's fields are still its, nor on the full map while it zooms.
void Session::drawMapMenu() {
    // Taken every frame, so a click no menu could open on is not kept for a later one.
    const bool clicked = right_clicked.exchange(false);
    const std::shared_ptr<const map_scene::MapScene> scene = map_scene::published(mumble_state.is_map_open);
    if (!scene || scene->full_map != mumble_state.is_map_open || scene->zoom != 1.0f || scene->keep_compass) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const std::optional<map::View> view = map::view(mumble_state, glm::vec2(display.x, display.y), scene->ui_scale);
    if (!view) return;

    ui::map_menu::Context context;
    context.scene = scene.get();
    context.view = &*view;
    context.live = {&live_map};
    if (edited.live) context.live.push_back(edited.live); // the map scene's layers, in order
    context.tree = &installed.tree;
    context.states = &states.categories;
    context.player = mumble_state.player_position;
    context.tooltips = persisted.map_tooltips;
    context.right_clicked = clicked;
    context.copy = [this](const std::string& text) { setClipboard(text, "Copied: " + text); };
    if (ui::map_menu::draw(context, &map_menu_view)) saveStates();
}

void Session::drawCatalog() {
    if (!windows.catalog) return;
    const ui::pack_catalog::Listing listing = ui::pack_catalog::list(library, pack_folder.local());
    bool retry = false;
    for (const ui::pack_catalog::Action& action :
         ui::pack_catalog::draw(&catalog_view, listing.rows, listing.status, listing.failed, &retry, &windows.catalog)) {
        const ui::pack_catalog::Row* row = action.row;
        switch (action.kind) {
        case ui::pack_catalog::Action::Kind::Download: library.downloads.start(*row->package, pack_folder.folder()); break;
        case ui::pack_catalog::Action::Kind::Delete:
            if (row->local) pack_folder.remove(&installed, row->local->path);
            break;
        case ui::pack_catalog::Action::Kind::KeepUpdated: library.setKeepUpdated(row->package->name, action.value); break;
        case ui::pack_catalog::Action::Kind::OpenPage: desktop::openInBrowser(row->package->info); break;
        case ui::pack_catalog::Action::Kind::OpenFolder: desktop::openFolder(pack_folder.folder()); break;
        }
    }
    if (retry) library.requestList();
}

void Session::drawImport() {
    if (importer.takeFound()) {
        import_choices = {};
        windows.import = true;
    }

    // Once the copy is done: Pathing's states into ours, written at once, and the new packs loaded.
    if (const std::optional<blish_import::Importer::Done> done = importer.finish()) {
        for (const auto& [id, on] : done->states.categories) states.categories.remember(id, on);
        for (const auto& [key, value] : done->states.storage) states.storage.upsert(key, value);
        states.timers.merge(done->timers);
        saveStates();
        for (const std::string& error : done->copied.errors)
            api->Log(ELogLevel_WARNING, ADDON_NAME, ("Blish HUD import: " + error).c_str());
        pack_folder.rescan();
    }

    const bool was_open = windows.import;
    const ui::import_window::Answer answer =
        ui::import_window::draw(importer.found(), &import_choices, importer.status(), importer.running(), &windows.import);
    if (answer == ui::import_window::Answer::Import) {
        const std::vector<std::filesystem::path>& found = importer.found().packs;
        std::vector<std::filesystem::path> chosen;
        for (size_t i = 0; i < found.size(); ++i) {
            if (i < import_choices.packs.size() && import_choices.packs[i]) chosen.push_back(found[i]);
        }
        importer.start(std::move(chosen), pack_folder.folder());
    }
    // Answered, or closed: it does not open by itself again.
    if (!persisted.import_offered && (answer != ui::import_window::Answer::None || (was_open && !windows.import))) {
        persisted.import_offered = true;
        saveSettings();
    }
}

void Session::render() {
    frame_log::Laps laps;
    takeFontDevice();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link, rtapi::data(), glm::vec2(display.x, display.y));

    // The world and the maps, before our font is pushed (see fonts::push). The overlay draws what
    // GoodRender did not reach: everything with no service, or a loading screen or the full map;
    // and the full map always. Behind our own windows and the editor's, but over the game.
    const std::shared_ptr<const Scene> scene = published();
    const good_render_sink::Drawn drawn = good_render_sink::takeDrawn();
    stats.last_drawn = drawn;
    // Not with the map up: this is over the game's UI, so over the map. The scene is still built
    // then, for GoodRender, which draws it under the map.
    if (scene && camera.valid && !camera.map_open && !(drawn.occluded && drawn.over)) {
        overlay::drawScene(ImGui::GetBackgroundDrawList(), camera, *scene, !drawn.occluded, !drawn.over);
    }
    // Not the minimap with the full map up, which covers it.
    if (!drawn.map && !camera.map_open) {
        map_scene::drawPublished(ImGui::GetBackgroundDrawList(), mumble_link, glm::vec2(display.x, display.y), false);
    }
    map_scene::drawPublished(ImGui::GetBackgroundDrawList(), mumble_link, glm::vec2(display.x, display.y), true);

    ui::fonts::push();
    drawMapMenu();

    // The menu's popup, for a left click on our icon.
    if (menu_requested.exchange(false)) {
        ui::main_menu::placeUnderIcon();
        ImGui::OpenPopup(kMenuPopup);
    }
    if (ImGui::BeginPopup(kMenuPopup)) {
        drawItems(true);
        ImGui::EndPopup();
    }

    // The options page in a window of ours, from the menu's Options. Not titled "Options" alone:
    // that is Nexus's window, in the same ImGui context. Before the windows it can open, so they
    // show in the frame it was clicked in.
    if (windows.options) {
        ImGui::SetNextWindowSize(ImVec2(480.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Markus Options", &windows.options)) drawOptionsPage();
        ImGui::End();
    }
    ui::script_console::draw(*scripts, installed, inFolder(ui::script_console::SAVE_FILE_NAME), jobs, &windows.console);
    drawCatalog();
    drawImport();
    // Registers a key the check found fit. From the frame rather than the options page, so a key
    // is kept even if the page closed while the check ran.
    if (std::optional<std::string> key = registration.takeAccepted()) {
        persisted.api_key = std::move(*key);
        account_progress.setKey(persisted.api_key, utcNow());
        registration.saving(saveSettings());
    }
    // Either may have removed a pack, and the prompt below calls into the live map.
    refreshLiveMap(live_map.map_id);

    if (camera.valid && !camera.map_open) {
        const ui::interaction::Drawn on_screen = ui::interaction::draw(
            &screen,
            &persisted,
            uiScale(),
            gameIcon("TEX_MarkerPackEditor_Gear", ui::map_icon_data::kGear),
            interact_key.name(),
            &achievement_details);
        if (on_screen.prompt_clicked) behaviors::interactWith(&live_map, screen.prompt->marker, behaviorHost(), behaviorFrame());
        if (on_screen.info_moved) saveSettings();
    }

    if (all_markers_requested.exchange(false)) {
        states.categories.set(runtime::ALL_MARKERS, !states.categories.enabled(runtime::ALL_MARKERS, true), true);
        saveStates();
    }
    ui::fonts::pop();

    // Over the world as well as in its windows, so it pushes the font around its windows itself.
    if (editor.render) editor.render();
    laps.lap(&stats.steps.render);
}

// ── The options page ────────────────────────────────────────────────────────────

void Session::drawOptions() {
    frame_log::Laps laps;
    takeFontDevice();
    ui::fonts::push();
    drawOptionsPage();
    ui::fonts::pop();
    laps.lap(&stats.steps.render);
}

void load(AddonAPI* api, Mumble::Data* mumble_link) {
    session = std::make_unique<Session>(api, mumble_link);
    live.store(session.get());
    // Only once `live` is set: Nexus may call back as soon as a callback is registered.
    session->attach();
}

// Does nothing when there is no session, so a second call is harmless.
void unload() {
    // Before detach(), whose deregistering waits out any callback that saw the session: see `live`.
    if (!live.exchange(nullptr)) return;
    session->detach();
    // Everything else stops as the session is destroyed: see Session's members.
    session.reset();
    // After the session's job queue has run everything it was handed.
    texture_uploads = TextureUploads{};
    editor = {};
}

void connectEditor(const EditorHooks& hooks) {
    editor = hooks;
}

TextureUploads& textureUploads() {
    return texture_uploads;
}

} // namespace viewer
