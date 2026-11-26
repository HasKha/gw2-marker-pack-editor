#include "viewer/viewer.h"

#include <IconsFontAwesome7.h>
#include <d3d11.h>
#include <imgui.h>

#include <windows.h>

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
#include "viewer/ui/options.h"
#include "viewer/ui/pack_catalog.h"
#include "viewer/ui/script_console.h"
#include "viewer/utc.h"

namespace viewer {

namespace {

AddonAPI* api = nullptr;
Mumble::Data* mumble_link = nullptr;
// Read for its Scaling and IsGameplay only: its Width and Height go stale (see gameCamera).
const NexusLinkData* nexus_link = nullptr;

std::filesystem::path folder;

// `name` in our folder, or nothing while there is no folder.
std::filesystem::path inFolder(const std::filesystem::path& name) {
    return folder.empty() ? folder : folder / name;
}

// Every read and write of a file after load: the frame hands them over and takes the answer
// on a later frame, never waiting for the disk itself. Stopped in unload, which lets what is
// queued finish.
util::JobQueue jobs;

// The images of every pack, the editor's too, handed to Nexus and not made yet: see TextureUploads.
TextureUploads texture_uploads;

// What the options page edits and saves, against what this session loaded with.
settings::Settings persisted;

// Writes `persisted` as it is now, on the worker: whether it landed, which only the API key's
// message waits for.
std::future<bool> saveSettings() {
    return jobs.ask([folder = folder, saved = persisted] { return settings::save(folder, saved); });
}

// The game's device, which our font's atlas is uploaded to. Taken from Nexus's swap chain once
// there is one; ours to release.
ID3D11Device* font_device = nullptr;

EditorHooks editor;

PackFolder pack_folder;
runtime::Installed installed;
runtime::LiveMap live;
store::CategoryStates category_states;
std::vector<char> visible;
uint64_t visible_revision = ~0ull; // the category_states revision `visible` was derived from
uint64_t visible_tree = ~0ull; // and the Installed revision

Recycled<Scene> scenes;
Maps maps;

// For the Development section: what built the last scene and how long Present's work took, and
// what GoodRender's moments drew in the last frame.
CameraSource scene_camera = CameraSource::None;
ui::diagnostics::FrameTimes present_times;
good_render_sink::Drawn last_drawn;

// The slow-frame log: when the last Present began, where the frame since went, and which slow
// frames make a line.
std::chrono::steady_clock::time_point last_present{};
frame_log::Steps frame_steps;
frame_log::Limiter slow_frames;

// The pack open in the editor this frame, as EditorHooks::edited_live hands it over: drawn in
// the world and on the maps with the installed packs. Its markers belong to no node of our
// tree, which the editor's own decides for, so nothing of ours turns them off.
runtime::LiveMap* edited_live = nullptr;
TextureLookup edited_texture;
const std::vector<char> kNoNodes;

bool console_window = false;
bool catalog_window = false;
bool options_window = false;

// The Blish HUD import, and what the player chose to bring.
bool import_window = false;
blish_import::Importer importer;
ui::import_window::Choices import_choices;

catalog::Library library;

// Scripts: one engine for every pack, built again with every live map, as Pathing's is with
// every map. `scripts_stale` holds from the moment the live map is replaced until the engine
// has been reset onto it — a script's handles are indices into the live map, so it must not
// tick in between.
std::unique_ptr<script::Engine> scripts;
MumbleState mumble_state;
store::KeyValues script_storage;
bool scripts_stale = true;

// Behaviors: what they hid and until when, what they show on screen, and the festivals on.
behaviors::BehaviorStates behavior_states;
behaviors::Screen screen;
behaviors::FestivalFetcher festivals;

// Pathing's "Temporarily Show Ghost Markers", which it turns off at every start: so a session
// setting, never saved.
bool show_hidden = false;

// The API key in the options, what the registered key says of the account (achievements and
// this week's raids), and what achievements are, for their tooltips.
api_key::Registration registration;
account::Fetcher account_progress;
achievements::Catalog achievement_details;

// Our icon in Nexus's bar was clicked: the menu opens under it on the next render. Set from
// Nexus's input handling, on a thread of its own, as is the bind turning All Markers on or off.
std::atomic<bool> menu_requested{false};
std::atomic<bool> all_markers_requested{false};
constexpr const char* kMenuPopup = "##marker_pack_menu";
// Where the menu is placed in Nexus now: its own menu, and behind our icon in its bar.
bool menu_in_nexus = false;
bool menu_icon = false;

// Pathing's smart categories: the menus whose hidden categories were asked for, until the map
// changes.
std::set<size_t> menu_show_all;
int menu_show_all_map = 0;

// The interact key and Escape, heard in the window procedure, for the frame.
InteractKey interact_key;
std::atomic<bool> interact_pressed{false};
std::atomic<bool> escape_released{false};

// What outlives the session is written at most this often, as Pathing flushes its states,
// since a script may change them every tick.
constexpr std::chrono::seconds kSaveInterval{5};
std::chrono::steady_clock::time_point last_save;
util::KeptFile categories_file;
util::KeptFile storage_file;
util::KeptFile timers_file;

// Every few seconds: what changed is handed over to be written, timers that ran out are
// dropped, and Nexus's binds are looked at again.
void saveStates() {
    categories_file.save(&jobs, inFolder(store::CATEGORIES_FILE_NAME), category_states.revision(), [] { return category_states.text(); });
    storage_file.save(&jobs, inFolder(store::STORAGE_FILE_NAME), script_storage.revision(), [] { return script_storage.text(); });
    behavior_states.expire(utcNow());
    timers_file.save(&jobs, inFolder(behaviors::TIMERS_FILE_NAME), behavior_states.revision(), [] { return behavior_states.text(); });
    jobs.post([] { interact_key.read(); });
    last_save = std::chrono::steady_clock::now();
}

// The clipboard, for a `copy` marker and User.SetClipboard alike, telling the player with
// `message`. Whether a pack may copy at all is the caller's to ask.
bool setClipboard(const std::string& text, const std::string& message) {
    const bool copied = desktop::copy(text);
    if (copied && api) api->UI.SendAlert(message.c_str());
    return copied;
}

// Where the scripts are: freshly reset onto the live map once every pack is in, as Pathing
// runs them once a map's packs have loaded; then ticked every frame.
void runScripts() {
    if (!persisted.run_scripts) return;
    if (scripts_stale) {
        if (pack_folder.loading()) return;
        scripts->reset();
        scripts->loadPacks();
        scripts_stale = false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    scripts->tick(ImGui::GetTime(), io.DeltaTime);
}

void replaceLiveMap(int map_id) {
    // ReappearOnMapChange waits for the map, not for the packs to change under it.
    if (map_id != live.map_id) behavior_states.mapChanged();
    live = runtime::liveMap(installed, map_id);
    scripts_stale = true;
    // Every entity is new, and what the old ones put on screen goes with them, as Pathing
    // unloads its entities.
    screen = behaviors::Screen{};
}

// The live map, and the scripts' handles into it, index the installed packs: whatever installs
// or removes one leaves them pointing at packs that moved, until they are built again. Whether
// or not there is a camera — the menu removes packs at character select too.
void refreshLiveMap(int map_id) {
    if (live.map_id != map_id || live.revision != installed.revision) replaceLiveMap(map_id);
}

behaviors::Host behaviorHost() {
    behaviors::Host host;
    host.states = &behavior_states;
    host.categories = &category_states;
    host.tree = &installed.tree;
    host.screen = &screen;
    host.copy = setClipboard;
    // Script.Update waits until every pack is in, and so do the engine's handles.
    if (persisted.run_scripts && !scripts_stale) {
        host.script = [](const behaviors::ScriptCall& call, behaviors::Entity entity, behaviors::Hook hook, bool flag) {
            return scripts->callBehavior(call, entity, hook, flag);
        };
    }
    host.options.auto_hide = persisted.allow_auto_hide;
    host.options.animate = persisted.allow_animation;
    host.options.show_hidden = show_hidden;
    host.options.clipboard = static_cast<behaviors::ClipboardConsent>(persisted.clipboard_consent);
    return host;
}

behaviors::Frame behaviorFrame() {
    return behaviors::Frame{&mumble_state, utcNow(), ImGui::GetTime(), &festivals.active(), &account_progress.progress()};
}

// Hears the interact key and Escape, and passes both on: the game interacts with what it has
// in front of it as well, as it does with Blish HUD running.
UINT onWndProc(HWND window, UINT message, WPARAM key, LPARAM flags) {
    // The editor first: a click it picks a marker with is not the game's.
    if (editor.wnd_proc && editor.wnd_proc(window, message, key, flags) == 0) return 0;
    const bool first_press = (flags & (LPARAM{1} << 30)) == 0;
    if (message == WM_KEYDOWN && first_press && interact_key.matches(key)) interact_pressed = true;
    if (message == WM_KEYUP && key == VK_ESCAPE) escape_released = true;
    return message;
}

void drawCatalog() {
    if (!catalog_window) return;
    const ui::pack_catalog::Listing listing = ui::pack_catalog::list(library, pack_folder.local());
    bool retry = false;
    for (const ui::pack_catalog::Action& action :
         ui::pack_catalog::draw(listing.rows, listing.status, listing.failed, &retry, &catalog_window)) {
        const ui::pack_catalog::Row& row = *action.row;
        switch (action.kind) {
        case ui::pack_catalog::Action::Kind::Download: library.downloads().start(*row.package, pack_folder.folder()); break;
        case ui::pack_catalog::Action::Kind::Delete:
            if (row.local) pack_folder.remove(&installed, row.local->path);
            break;
        case ui::pack_catalog::Action::Kind::KeepUpdated: library.setKeepUpdated(row.package->name, action.value); break;
        case ui::pack_catalog::Action::Kind::OpenPage: desktop::openInBrowser(row.package->info); break;
        }
    }
    if (retry) library.requestList();
}

// Opens the import, reading again what Blish HUD has now: the window opens once the worker has
// read it (drawImport).
void openImport() {
    if (importer.running()) {
        import_window = true;
    } else {
        importer.findOnWorker(registry::blishMarkersFolder(), &jobs);
    }
}

void drawImport() {
    if (importer.takeFound()) {
        import_choices = {};
        import_window = true;
    }

    // Once the copy is done: Pathing's states into ours, written at once, and the new packs loaded.
    if (const std::optional<blish_import::Importer::Done> done = importer.finish()) {
        for (const auto& [id, on] : done->states.categories) category_states.remember(id, on);
        for (const auto& [key, value] : done->states.storage) script_storage.upsert(key, value);
        behavior_states.merge(done->timers);
        saveStates();
        for (const std::string& error : done->copied.errors)
            api->Log(ELogLevel_WARNING, ADDON_NAME, ("Blish HUD import: " + error).c_str());
        pack_folder.rescan();
    }

    const bool was_open = import_window;
    const ui::import_window::Answer answer =
        ui::import_window::draw(importer.found(), &import_choices, importer.status(), importer.running(), &import_window);
    if (answer == ui::import_window::Answer::Import) {
        const std::vector<std::filesystem::path>& found = importer.found().packs;
        std::vector<std::filesystem::path> chosen;
        for (size_t i = 0; i < found.size(); ++i) {
            if (i < import_choices.packs.size() && import_choices.packs[i]) chosen.push_back(found[i]);
        }
        importer.start(std::move(chosen), pack_folder.folder());
    }
    // Answered, or closed: it does not open by itself again.
    if (!persisted.import_offered && (answer != ui::import_window::Answer::None || (was_open && !import_window))) {
        persisted.import_offered = true;
        saveSettings();
    }
}

// The menu's items, wherever it is shown: Nexus's menu, our icon's, or our own popup.
void drawItems(bool separator) {
    if (live.map_id != menu_show_all_map) {
        menu_show_all.clear();
        menu_show_all_map = live.map_id;
    }
    // Walked while the menu is open only, and a map holds thousands at most.
    const std::vector<char> on_map = persisted.smart_categories ? runtime::nodesOnMap(installed.tree, live) : std::vector<char>{};

    ui::main_menu::Context context;
    context.tree = &installed.tree;
    context.states = &category_states;
    context.scripts = persisted.run_scripts ? scripts.get() : nullptr;
    context.manage_packs = [] { catalog_window = true; };
    context.open_options = [] { options_window = true; };
    if (editor.start) context.open_editor = editor.start;
    context.on_map = persisted.smart_categories ? &on_map : nullptr;
    context.show_all = &menu_show_all;
    context.account = &account_progress.progress();
    context.auto_hide = persisted.allow_auto_hide;
    context.achievements = &achievement_details;
    context.achievement_tooltips = persisted.achievement_tooltips;
    context.separator = separator;
    // Our font for its icons, pushed here since Nexus draws its menu and our icon's outside our render.
    ui::fonts::push();
    // A player's own toggle is written at once, not with the scripts' changes.
    if (ui::main_menu::drawItems(context)) saveStates();
    ui::fonts::pop();
}

void drawMenuItems() {
    drawItems(true);
}

void drawNexusMenuItems() {
    drawItems(false);
}

// Hands the fonts the game's device as soon as Nexus has a swap chain to take it from.
void takeFontDevice() {
    if (font_device || !api->SwapChain) return;
    auto* swap_chain = static_cast<IDXGISwapChain*>(api->SwapChain);
    if (SUCCEEDED(swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&font_device)))) ui::fonts::load(font_device);
}

// The menu for a left click on our icon.
void drawMenuPopup() {
    if (menu_requested.exchange(false)) {
        ui::main_menu::placeUnderIcon();
        ImGui::OpenPopup(kMenuPopup);
    }
    if (ImGui::BeginPopup(kMenuPopup)) {
        drawMenuItems();
        ImGui::EndPopup();
    }
}

// Windows' display scale. The system's, not the game window's monitor's as Nexus and Blish HUD
// ask: the two differ only across monitors of different scales, which Custom is there for.
float systemDpiScale() {
    const UINT dpi = GetDpiForSystem();
    return dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
}

// Screen pixels per UI pixel, as the game draws its minimap and map: the interface size, and
// the DPI scale the options say to take.
float uiScale() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float dpi_scale = 1.0f;
    switch (static_cast<settings::DpiScaling>(persisted.map_dpi_scaling)) {
    case settings::DpiScaling::None: break;
    case settings::DpiScaling::Nexus:
        if (nexus_link && nexus_link->Scaling > 0.0f) return nexus_link->Scaling;
        break;
    case settings::DpiScaling::System: dpi_scale = systemDpiScale(); break;
    case settings::DpiScaling::Custom: dpi_scale = persisted.map_dpi_custom; break;
    }
    return map::uiScale(mumble_state.ui_size, dpi_scale, glm::vec2(display.x, display.y));
}

// Nexus's IsGameplay, which is off on a loading screen: MumbleLink is not written there, and
// keeps the last frame before it. Taken as on without the link, rather than never drawing.
bool inGameplay() {
    return !nexus_link || nexus_link->IsGameplay;
}

WorldOptions worldOptions() {
    WorldOptions options;
    options.enabled = persisted.world_pathables;
    options.max_opacity = persisted.max_opacity;
    options.view_distance = persisted.view_distance;
    options.max_trail_speed = persisted.max_trail_speed;
    options.marker_scale = persisted.marker_scale;
    options.combat_fade = persisted.combat_fade;
    options.in_combat = mumble_state.is_in_combat;
    options.in_gameplay = inGameplay();
    options.show_hidden = show_hidden;
    options.occlusion = static_cast<Occlusion>(persisted.occlusion);
    return options;
}

// The above or below image the maps draw on an icon's corner, which the options page shows too.
// Nexus returns the texture it already made for an identifier, so asking every frame decodes
// each once; null until it is ready.
ImTextureID aboveBelowImage(bool above) {
    const char* id = above ? "TEX_MarkerPackEditor_Above" : "TEX_MarkerPackEditor_Below";
    const unsigned char* bytes = above ? ui::map_icon_data::kAbove : ui::map_icon_data::kBelow;
    const size_t size = above ? sizeof(ui::map_icon_data::kAbove) : sizeof(ui::map_icon_data::kBelow);
    const Texture* texture = api->Textures.GetOrCreateFromMemory(id, const_cast<unsigned char*>(bytes), size);
    return texture ? static_cast<ImTextureID>(texture->Resource) : nullptr;
}

// What the maps are handed at Present: the live maps, as the world scene is, and the options.
Maps::Frame mapFrame(const TextureLookup& texture) {
    Maps::Frame frame;
    frame.mumble = &mumble_state;
    frame.in_gameplay = inGameplay();
    frame.world_drawn = last_drawn.world;
    frame.seconds = ImGui::GetTime();
    frame.live = &live;
    frame.visible = &visible;
    frame.texture = texture;
    frame.edited = edited_live;
    frame.edited_texture = edited_texture;

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

// The map's tooltip and menu, over what was published this frame. None on the compass while the
// full map is up or MumbleLink's fields are still its, nor on the full map while it zooms.
void drawMapMenu() {
    const std::shared_ptr<const map_scene::MapScene> scene = map_scene::published(mumble_state.is_map_open);
    if (!scene || scene->full_map != mumble_state.is_map_open || scene->zoom != 1.0f || scene->keep_compass) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const std::optional<map::View> view = map::view(mumble_state, glm::vec2(display.x, display.y), scene->ui_scale);
    if (!view) return;

    ui::map_menu::Context context;
    context.scene = scene.get();
    context.view = &*view;
    context.live = {&live};
    if (edited_live) context.live.push_back(edited_live); // the map scene's layers, in order
    context.tree = &installed.tree;
    context.states = &category_states;
    context.player = mumble_state.player_position;
    context.tooltips = persisted.map_tooltips;
    context.copy = [](const std::string& text) { setClipboard(text, "Copied: " + text); };
    if (ui::map_menu::draw(context)) saveStates();
}

// The frame that ended with the Present beginning at `started`, logged when our work in it took
// long. Loading screens and the game in the background included: what we do there is ours all
// the same.
void logSlowFrame(std::chrono::steady_clock::time_point started) {
    size_t skipped = 0;
    if (!persisted.log_slow_frames || !slow_frames.take(frame_steps.total(), persisted.slow_frame_ms, started, &skipped)) return;
    frame_log::Frame frame;
    frame.interval = std::chrono::duration<double, std::milli>(started - last_present).count();
    frame.steps = frame_steps;
    double slowest = 0.0;
    for (size_t pack = 0; pack < installed.packs.size(); ++pack) {
        if (const double seconds = scripts->frameSeconds(pack); seconds > slowest) {
            slowest = seconds;
            frame.slowest_scripts = util::pathText(installed.packs[pack]->loaded->path.filename());
        }
    }
    frame.map_id = live.map_id;
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
void preRender() {
    const auto started = std::chrono::steady_clock::now();
    frame_steps.moments = good_render_sink::takeMomentMilliseconds();
    if (last_present != std::chrono::steady_clock::time_point{}) logSlowFrame(started);
    last_present = started;
    frame_steps = {};

    frame_log::Laps laps;
    texture_uploads.nextFrame();
    pack_folder.update(&installed, &library.downloads());
    library.updateStale(pack_folder.local(), pack_folder.folder());
    account_progress.update(std::chrono::steady_clock::now(), utcNow());
    achievement_details.update(std::chrono::steady_clock::now());
    laps.lap(&frame_steps.packs);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link, rtapi::data(), glm::vec2(display.x, display.y));

    refreshLiveMap(camera.valid ? static_cast<int>(camera.map_id) : live.map_id);
    laps.lap(&frame_steps.live_map);
    mumble_state = mumbleState(mumble_link);
    runScripts();
    laps.lap(&frame_steps.scripts);

    // Behaviors see what scripts did this frame, and the scene what both did.
    const auto updateVisible = [] {
        if (visible_revision == category_states.revision() && visible_tree == installed.revision) return;
        visible = runtime::visibleNodes(installed.tree, category_states);
        visible_revision = category_states.revision();
        visible_tree = installed.revision;
    };
    updateVisible();
    laps.lap(&frame_steps.visibility);

    // One frame of behaviors, after the scripts have ticked.
    const behaviors::Host host = behaviorHost();
    const behaviors::Frame frame = behaviorFrame();
    behaviors::update(&live, visible, host, frame);
    laps.lap(&frame_steps.behaviors);

    // Not while typing — in chat, or in one of our own text fields.
    const bool typing = mumble_state.is_text_input_focused || ImGui::GetIO().WantTextInput;
    if (interact_pressed.exchange(false) && !typing) behaviors::interact(&live, host, frame);
    // UiStates clears the info on Escape.
    if (escape_released.exchange(false)) screen.info.clear();
    laps.lap(&frame_steps.interaction);

    updateVisible();
    runtime::startFadeIns(&live, visible, ImGui::GetTime());
    laps.lap(&frame_steps.visibility);
    if (std::chrono::steady_clock::now() - last_save > kSaveInterval) saveStates();
    laps.lap(&frame_steps.saves);
    edited_live = editor.edited_live ? editor.edited_live(live.map_id, &edited_texture) : nullptr;
    if (edited_live) runtime::startFadeIns(edited_live, kNoNodes, ImGui::GetTime());
    laps.lap(&frame_steps.editor);

    const TextureLookup texture = [](size_t pack, const std::string& path) { return runtime::packImage(installed, pack, path); };

    std::vector<SceneLayer> layers{SceneLayer{&live, &visible, texture, false}};
    if (edited_live) layers.push_back(SceneLayer{edited_live, &kNoNodes, edited_texture, true});

    const std::shared_ptr<Scene> next = scenes.take();
    buildScene(layers, camera, static_cast<float>(ImGui::GetTime()), worldOptions(), next.get());
    scenes.keep(next);
    publish(next);
    scene_camera = camera.source;
    laps.lap(&frame_steps.scene);

    maps.build(mapFrame(texture));
    laps.lap(&frame_steps.maps);
    present_times.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
}

void drawOptionsWindow();

// The overlay draws what GoodRender did not reach: everything with no service, or a loading
// screen or the full map; and the full map always. Behind our own windows and the editor's, but
// over the game.
void drawWorld() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link, rtapi::data(), glm::vec2(display.x, display.y));
    const std::shared_ptr<const Scene> scene = published();

    const good_render_sink::Drawn drawn = good_render_sink::takeDrawn();
    last_drawn = drawn;
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
}

// Our windows and ImGui's: what is drawn with our font pushed.
void drawFrame() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link, rtapi::data(), glm::vec2(display.x, display.y));

    drawMapMenu();

    drawMenuPopup();
    // Before the windows it can open, so they show in the frame it was clicked in.
    drawOptionsWindow();
    ui::script_console::draw(*scripts, installed, inFolder(ui::script_console::SAVE_FILE_NAME), jobs, &console_window);
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
    refreshLiveMap(live.map_id);

    if (camera.valid && !camera.map_open &&
        ui::interaction::draw(
            screen,
            static_cast<settings::InfoDisplay>(persisted.info_display),
            interact_key.name(),
            persisted.show_interact_prompt,
            &achievement_details)) {
        behaviors::interactWith(&live, screen.prompt->marker, behaviorHost(), behaviorFrame());
    }

    if (all_markers_requested.exchange(false)) {
        category_states.set(runtime::ALL_MARKERS, !category_states.enabled(runtime::ALL_MARKERS, true), true);
        saveStates();
    }
}

void render() {
    frame_log::Laps laps;
    takeFontDevice();
    // The world and the map before our font is pushed: see fonts::push.
    drawWorld();
    ui::fonts::push();
    drawFrame();
    ui::fonts::pop();
    // Over the world as well as in its windows, so it pushes the font around its windows itself.
    if (editor.render) editor.render();
    laps.lap(&frame_steps.render);
}

void onMenuKeybind(const char*, bool is_release) {
    if (!is_release) menu_requested = true;
}

void onAllMarkersKeybind(const char*, bool is_release) {
    if (!is_release) all_markers_requested = true;
}

// Puts the menu in Nexus's menu when `in_nexus`, and behind our icon in Nexus's bar when `icon`,
// taking it out of either otherwise. Safe from the options page, Nexus's or our window's: Nexus
// holds its quick access lock only while drawing the bar.
void placeMenu(bool in_nexus, bool icon) {
    if (in_nexus != menu_in_nexus) {
        if (in_nexus) {
            api->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawNexusMenuItems);
        } else {
            api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
        }
        menu_in_nexus = in_nexus;
    }
    if (icon != menu_icon) {
        if (icon) {
            api->QuickAccess.Add(SHORTCUT_ID, ICON_TEXTURE_ID, ICON_HOVER_TEXTURE_ID, KEYBIND_MENU_ID, ADDON_NAME);
            api->QuickAccess.AddContextMenu(SHORTCUT_MENU_ID, SHORTCUT_ID, drawMenuItems);
        } else {
            api->QuickAccess.RemoveContextMenu(SHORTCUT_MENU_ID);
            api->QuickAccess.Remove(SHORTCUT_ID);
        }
        menu_icon = icon;
    }
}

// What the Development section shows. The images' counts walk every pack's cache, so this is
// gathered only while the section is open.
ui::diagnostics::Figures diagnosticFigures() {
    ui::diagnostics::Figures figures;
    figures.service = good_render_sink::service();
    figures.drawn = last_drawn;
    figures.camera = scene_camera;
    if (const std::shared_ptr<const Scene> scene = published()) figures.scene_items = scene->items.size();
    figures.present_ms = present_times.last();
    figures.present_peak_ms = present_times.peak();
    figures.map_id = live.map_id;
    figures.map_described = maps.find(live.map_id) != nullptr;
    figures.in_gameplay = inGameplay();
    figures.markers = live.markers.size();
    figures.trails = live.trails.size();
    figures.packs_found = pack_folder.found();
    figures.loading = pack_folder.loading();
    for (const std::unique_ptr<runtime::InstalledPack>& pack : installed.packs) {
        figures.packs.push_back(
            ui::diagnostics::PackFigures{util::pathText(pack->loaded->path.filename()), pack->loaded->error, pack->textures.counts()});
    }
    figures.uploads_images = texture_uploads.images();
    figures.uploads_pixels = texture_uploads.pixels();
    figures.jobs = jobs.unfinished();
    figures.lua_bytes = scripts->heapBytes();
    return figures;
}

// The page's sections, the first three open until the player closes them: what a new player
// sets up, the API key among it, before what fine-tunes the drawing.
void drawOptionsPage() {
    if (ImGui::CollapsingHeader("Interface", ImGuiTreeNodeFlags_DefaultOpen) && ui::options::drawInterface(&persisted)) {
        placeMenu(persisted.menu_in_nexus, persisted.menu_icon);
        saveSettings();
    }

    if (ImGui::CollapsingHeader("Marker Packs", ImGuiTreeNodeFlags_DefaultOpen)) {
        switch (ui::options::drawPacks(&persisted, pack_folder.status(installed))) {
        case ui::options::PacksAction::None: break;
        case ui::options::PacksAction::ScriptsToggled:
            saveSettings();
            // Off throws every handler away, and what scripts did to the map with them; on starts
            // them again from pack.lua.
            scripts->reset();
            replaceLiveMap(live.map_id);
            break;
        case ui::options::PacksAction::Manage: catalog_window = true; break;
        case ui::options::PacksAction::Reload: pack_folder.reload(&installed); break;
        case ui::options::PacksAction::Import: openImport(); break;
        }
    }

    if (ImGui::CollapsingHeader("GW2 API Key", ImGuiTreeNodeFlags_DefaultOpen)) {
        switch (ui::options::drawApiKey(
            persisted.api_key,
            registration.field(),
            registration.fieldSize(),
            registration.checking(),
            registration.status())) {
        case ui::options::KeyAction::None: break;
        case ui::options::KeyAction::OpenApplications: desktop::openInBrowser(api_key::APPLICATIONS_URL); break;
        case ui::options::KeyAction::Register: registration.submit(); break;
        case ui::options::KeyAction::Remove:
            persisted.api_key.clear();
            saveSettings();
            // The progress kept was the key's: it goes with it.
            account_progress.setKey(persisted.api_key, utcNow());
            account_progress.clearCache();
            registration.setStatus({});
            break;
        case ui::options::KeyAction::ClearCache:
            account_progress.clearCache();
            achievement_details.clear();
            registration.setStatus("Saved API data cleared.");
            break;
        }
    }

    if (ImGui::CollapsingHeader("Markers")) {
        if (ui::options::drawPack(&persisted, &show_hidden)) saveSettings();
    }
    if (ImGui::CollapsingHeader("Map and Minimap") && ui::options::drawMaps(&persisted, aboveBelowImage(true), aboveBelowImage(false))) {
        saveSettings();
    }

    // For pack authors and bug reports.
    if (ImGui::CollapsingHeader("Development")) {
        if (ImGui::Button(ui::fonts::label(ICON_FA_TERMINAL, "Open Lua Script Console").c_str())) console_window = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Messages and errors from pack scripts.");
        ImGui::SameLine();
        ui::diagnostics::draw(ui::diagnostics::describe(diagnosticFigures()), [] {
            if (ui::options::drawSlowFrameLog(&persisted)) saveSettings();
        });
    }
}

void drawOptions() {
    frame_log::Laps laps;
    takeFontDevice();
    ui::fonts::push();
    drawOptionsPage();
    ui::fonts::pop();
    laps.lap(&frame_steps.render);
}

// The same page in a window of ours, from the menu's Options; our render has the fonts pushed
// already. Not titled "Options" alone: that is Nexus's window, in the same ImGui context.
void drawOptionsWindow() {
    if (!options_window) return;
    ImGui::SetNextWindowSize(ImVec2(480.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Markus Options", &options_window)) drawOptionsPage();
    ImGui::End();
}

} // namespace

void load(AddonAPI* aApi, Mumble::Data* aMumbleLink) {
    api = aApi;
    mumble_link = aMumbleLink;
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
    category_states.load(inFolder(store::CATEGORIES_FILE_NAME));
    categories_file.loaded(category_states.revision());
    script_storage.load(inFolder(store::STORAGE_FILE_NAME));
    storage_file.loaded(script_storage.revision());
    behavior_states.load(inFolder(behaviors::TIMERS_FILE_NAME));
    timers_file.loaded(behavior_states.revision());
    if (const char* game = api->Paths.GetGameDirectory()) {
        interact_key.open(std::filesystem::path(game) / "addons" / "Nexus" / game_binds::FILE_NAME);
    }
    festivals.start();
    maps.open(inFolder(map::CACHE_FILE_NAME));

    script::Host host;
    host.installed = &installed;
    host.live = &live;
    host.category_states = &category_states;
    host.storage = &script_storage;
    host.mumble = &mumble_state;
    host.account = &account_progress.progress();
    // User.SetClipboard: only "Never" stops a script, as in Pathing's Lib/User.cs.
    host.set_clipboard = [](const std::string& text, const std::string& message) {
        return persisted.clipboard_consent != static_cast<int>(behaviors::ClipboardConsent::Never) && setClipboard(text, message);
    };
    host.show_message = [](const std::string& text) {
        if (api) api->UI.SendAlert(text.c_str());
    };
    installed.api = api;
    installed.jobs = &jobs;
    installed.uploads = &texture_uploads;
    scripts = std::make_unique<script::Engine>(host);
    scripts_stale = true;

    library.open(inFolder(catalog::STATE_FILE_NAME), &jobs);
    pack_folder.open(inFolder(PACKS_FOLDER), &jobs, api);
    // Offered once, by itself, when Blish HUD has anything to bring.
    if (!persisted.import_offered) {
        importer.find(registry::blishMarkersFolder());
        import_window = !importer.found().empty();
        if (!import_window) persisted.import_offered = true;
    }
    good_render_sink::load(mumble_link);
    rtapi::load(api);

    api->Renderer.Register(ERenderType_PreRender, preRender);
    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
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
    placeMenu(persisted.menu_in_nexus, persisted.menu_icon);
}

void unload() {
    // The service first: once unsubscribe returns no moment can be reading the scene.
    good_render_sink::unload();
    rtapi::unload();

    api->Renderer.Deregister(preRender);
    api->Renderer.Deregister(render);
    api->Renderer.Deregister(drawOptions);
    api->WndProc.Deregister(onWndProc);
    placeMenu(false, false);
    ui::fonts::unload();
    if (font_device) {
        font_device->Release();
        font_device = nullptr;
    }
    api->InputBinds.Deregister(KEYBIND_MENU_ID);
    api->InputBinds.Deregister(KEYBIND_ALL_MARKERS_ID);

    pack_folder.stop();
    library.stop();
    importer.stop();
    registration.stop();
    account_progress.stop();
    achievement_details.stop();
    festivals.stop();
    maps.stop();
    saveStates();
    scripts.reset();
    screen = behaviors::Screen{};
    publish(nullptr);
    scenes.clear();
    live = runtime::LiveMap{};
    installed = runtime::Installed{};
    // After the packs are gone, so what was queued to read their images reads nothing, and
    // after saveStates, whose writes it waits for.
    jobs.stop();
    texture_uploads = TextureUploads{};
    editor = {};
    api = nullptr;
}

void connectEditor(const EditorHooks& hooks) {
    editor = hooks;
}

TextureUploads& textureUploads() {
    return texture_uploads;
}

} // namespace viewer
