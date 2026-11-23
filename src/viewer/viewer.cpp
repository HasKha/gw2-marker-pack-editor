#include "viewer/viewer.h"

#include <imgui.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "pack/source.h"
#include "viewer/behaviors/festivals.h"
#include "viewer/behaviors/states.h"
#include "viewer/behaviors/update.h"
#include "viewer/camera.h"
#include "viewer/map.h"
#include "viewer/map_scene.h"
#include "viewer/mumble.h"
#include "viewer/registry.h"
#include "viewer/render/good_render.h"
#include "viewer/render/overlay.h"
#include "viewer/runtime.h"
#include "viewer/scene.h"
#include "viewer/script/engine.h"
#include "viewer/settings.h"
#include "viewer/store.h"
#include "viewer/textures.h"
#include "viewer/ui/category_menu.h"
#include "viewer/ui/interaction.h"
#include "viewer/ui/map_menu.h"
#include "viewer/ui/options.h"
#include "viewer/ui/script_console.h"

namespace viewer {

namespace {

AddonAPI* api = nullptr;
Mumble::Data* mumble_link = nullptr;
// Read for its Scaling only: its Width and Height go stale (see fromMumble).
const NexusLinkData* nexus_link = nullptr;
std::filesystem::path folder;

// What the options page edits and saves, against what this session loaded with.
settings::Settings persisted;
bool editing_this_session = false;

EditorHooks editor;

registry::Loader loader;
// Every path discovery found, so a pack the editor lets go of is read again only if it was
// one of ours to begin with.
std::vector<std::filesystem::path> discovered;

runtime::Installed installed;
runtime::LiveMap live;
store::CategoryStates category_states;
std::vector<char> visible;
uint64_t visible_revision = ~0ull; // the category_states revision `visible` was derived from
uint64_t visible_tree = ~0ull; // and the Installed revision

// A few textures decoded per frame across every pack — see LoadBudget.
LoadBudget texture_budget{4};

// The last scene published, and the one before it, the older reused once no sink holds it any
// more. A scene is thousands of items, and allocating one a frame is a waste.
template <class T>
class Recycled
{
public:
    // A scene to build into: the spare, if nothing holds it, or a new one.
    std::shared_ptr<T> take() { return spare_ && spare_.use_count() == 1 ? std::move(spare_) : std::make_shared<T>(); }
    // Keeps `built`, which is about to be published.
    void keep(const std::shared_ptr<T>& built) {
        spare_ = std::move(current_);
        current_ = built;
    }
    void clear() {
        current_.reset();
        spare_.reset();
    }

private:
    std::shared_ptr<T> current_;
    std::shared_ptr<T> spare_;
};

Recycled<Scene> scenes;

// The minimap and the full map: every map's rectangles, the fade after the map changes, and
// what was published.
map::Catalog maps;
map_scene::FadeIn map_fade;
map_scene::TrailCache map_trails;
Recycled<map_scene::MapScene> map_scenes;

// What the editor has open, as last reported, and what the viewer has acted on.
std::filesystem::path edit_requested;
std::filesystem::path edit_applied;

bool markers_window = false;
bool console_window = false;

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

// The interact key, heard in the window procedure, which runs on the game's window thread
// while the frame runs on its render thread.
std::atomic<int> interact_key{'F'};
std::atomic<bool> interact_pressed{false};
std::atomic<bool> escape_released{false};

// What outlives the session is written at most this often, as Pathing flushes its states,
// since a script may change them every tick.
constexpr std::chrono::seconds kSaveInterval{5};
std::chrono::steady_clock::time_point last_save;
uint64_t saved_categories = 0; // the CategoryStates revision last written

std::filesystem::path categoriesFile() {
    return folder.empty() ? folder : folder / store::CATEGORIES_FILE_NAME;
}

std::filesystem::path storageFile() {
    return folder.empty() ? folder : folder / store::STORAGE_FILE_NAME;
}

std::filesystem::path timersFile() {
    return folder.empty() ? folder : folder / behaviors::TIMERS_FILE_NAME;
}

std::filesystem::path mapsFile() {
    return folder.empty() ? folder : folder / map::CACHE_FILE_NAME;
}

behaviors::UtcMillis utcNow() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Every few seconds: what changed is written, and timers that ran out are dropped.
void saveStates() {
    if (category_states.revision() != saved_categories && category_states.save(categoriesFile()))
        saved_categories = category_states.revision();
    if (script_storage.dirty()) script_storage.save(storageFile());
    behavior_states.expire(utcNow());
    if (behavior_states.dirty()) behavior_states.save(timersFile());
    last_save = std::chrono::steady_clock::now();
}

// The clipboard, for a `copy` marker and User.SetClipboard alike, telling the player with
// `message`. Whether a pack may copy at all is the caller's to ask.
bool setClipboard(const std::string& text, const std::string& message) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0 || !OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    bool copied = false;
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(length) * sizeof(wchar_t))) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, static_cast<wchar_t*>(GlobalLock(memory)), length);
        GlobalUnlock(memory);
        copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        if (!copied) GlobalFree(memory);
    }
    CloseClipboard();
    if (copied && api) api->UI.SendAlert(message.c_str());
    return copied;
}

// Where the scripts are: freshly reset onto the live map once every pack is in, as Pathing
// runs them once a map's packs have loaded; then ticked every frame.
void runScripts() {
    if (!persisted.run_scripts) return;
    if (scripts_stale) {
        if (loader.busy()) return;
        scripts->reset();
        scripts->loadPacks();
        scripts_stale = false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    scripts->tick(ImGui::GetTime(), io.DeltaTime);
}

// Every pack in our own folder and, unless turned off, in Blish HUD's.
void discoverAndLoad() {
    std::vector<std::filesystem::path> folders;
    if (!folder.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(folder / PACKS_FOLDER, ec);
        folders.push_back(folder / PACKS_FOLDER);
    }
    if (persisted.read_blish_folder) {
        const std::filesystem::path blish = registry::blishMarkersFolder();
        std::error_code ec;
        if (!blish.empty() && std::filesystem::is_directory(blish, ec)) folders.push_back(blish);
    }

    discovered.clear();
    for (const std::filesystem::path& source : folders) {
        for (std::filesystem::path& path : registry::discoverPacks(source)) discovered.push_back(std::move(path));
    }
    loader.add(discovered);
}

void reloadPacks() {
    loader.stop();
    runtime::clear(&installed);
    discoverAndLoad();
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
    return behaviors::Frame{&mumble_state, utcNow(), ImGui::GetTime(), &festivals.active()};
}

// The interact key's name as the keyboard writes it, for the prompt.
std::string keyName(int virtual_key) {
    const LONG scan = static_cast<LONG>(MapVirtualKeyW(static_cast<UINT>(virtual_key), MAPVK_VK_TO_VSC)) << 16;
    char name[64] = {};
    if (GetKeyNameTextA(scan, name, static_cast<int>(sizeof(name))) > 0) return name;
    return "?";
}

// Hears the interact key and Escape, and passes both on: the game interacts with what it has
// in front of it as well, as it does with Blish HUD running.
UINT onWndProc(HWND, UINT message, WPARAM key, LPARAM flags) {
    const bool first_press = (flags & (LPARAM{1} << 30)) == 0;
    if (message == WM_KEYDOWN && first_press && static_cast<int>(key) == interact_key.load()) interact_pressed = true;
    if (message == WM_KEYUP && key == VK_ESCAPE) escape_released = true;
    return message;
}

// One frame of behaviors, after the scripts have ticked.
void runBehaviors() {
    const behaviors::Host host = behaviorHost();
    const behaviors::Frame frame = behaviorFrame();
    behaviors::update(&live, visible, host, frame);

    // Not while typing — in chat, or in one of our own text fields.
    const bool typing = mumble_state.is_text_input_focused || ImGui::GetIO().WantTextInput;
    if (interact_pressed.exchange(false) && !typing) behaviors::interact(&live, host, frame);
    // UiStates clears the info on Escape.
    if (escape_released.exchange(false)) screen.info.clear();
}

// Takes what the loader has finished, and what the editor has taken or given back.
void updatePacks() {
    while (std::unique_ptr<registry::LoadedPack> loaded = loader.take()) {
        // Opened in the editor while it was loading: the editor has it now. Or loaded twice,
        // having been handed back by the editor before its first load came in.
        if (registry::samePack(loaded->path, edit_applied) || runtime::isInstalled(installed, loaded->path)) continue;
        if (!loaded->error.empty()) api->Log(ELogLevel_WARNING, ADDON_NAME, (loaded->path.string() + ": " + loaded->error).c_str());
        runtime::install(&installed, std::move(loaded));
    }

    if (edit_requested == edit_applied) return;
    const auto isDiscovered = [](const std::filesystem::path& path) {
        return std::any_of(discovered.begin(), discovered.end(), [&](const std::filesystem::path& known) {
            return registry::samePack(known, path);
        });
    };
    if (!edit_applied.empty() && isDiscovered(edit_applied)) loader.add({edit_applied});
    if (!edit_requested.empty()) runtime::uninstall(&installed, edit_requested);
    edit_applied = edit_requested;
}

std::string loadingStatus() {
    if (!loader.busy()) return std::to_string(installed.packs.size()) + " packs";
    return "Loading packs: " + std::to_string(installed.packs.size()) + " of " + std::to_string(discovered.size());
}

// Screen pixels per UI pixel: Nexus's own scaling, which follows the interface size and the
// game's DPI scaling, or the interface size alone before Nexus says.
float uiScale() {
    if (nexus_link && nexus_link->Scaling > 0.0f) return nexus_link->Scaling;
    return map::uiScale(mumble_state.ui_size);
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
    options.show_hidden = show_hidden;
    return options;
}

map_scene::Options mapOptions() {
    map_scene::Options options;
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
    return options;
}

// What the minimap or the full map shows this frame. Nothing on a map the API has not
// described, as in Pathing.
void buildMapScene(const TextureLookup& texture) {
    const std::shared_ptr<map_scene::MapScene> next = map_scenes.take();
    const float fade = map_fade.update(mumble_state, ImGui::GetTime());
    if (const map::MapDetails* details = mumble_state.available ? maps.find(mumble_state.map_id) : nullptr) {
        map_scene::build(live, visible, *details, mumble_state, mapOptions(), fade, uiScale(), texture, &map_trails, next.get());
    } else {
        *next = map_scene::MapScene{};
    }
    map_scenes.keep(next);
    map_scene::publish(next);
}

// The map's tooltip and menu, over what was published this frame.
void drawMapMenu() {
    const std::shared_ptr<const map_scene::MapScene> scene = map_scene::published();
    if (!scene || scene->full_map != mumble_state.is_map_open) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const std::optional<map::View> view = map::view(mumble_state, glm::vec2(display.x, display.y), scene->ui_scale);
    if (!view) return;

    ui::map_menu::Context context;
    context.scene = scene.get();
    context.view = &*view;
    context.live = &live;
    context.tree = &installed.tree;
    context.states = &category_states;
    context.player = mumble_state.player_position;
    context.tooltips = persisted.map_tooltips;
    context.copy = [](const std::string& text) { setClipboard(text, "Copied: " + text); };
    if (ui::map_menu::draw(context)) saveStates();
}

// Everything is decided here, at Present, before the next frame is drawn: GoodRender's
// moments only draw what this publishes, because a moment runs inside the game's frame and
// anything slow there stalls the game.
void preRender() {
    updatePacks();

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));

    if (camera.valid && (live.map_id != static_cast<int>(camera.map_id) || live.revision != installed.revision)) {
        replaceLiveMap(static_cast<int>(camera.map_id));
    }
    mumble_state = mumbleState(mumble_link);
    runScripts();

    // Behaviors see what scripts did this frame, and the scene what both did.
    const auto updateVisible = [] {
        if (visible_revision == category_states.revision() && visible_tree == installed.revision) return;
        visible = runtime::visibleNodes(installed.tree, category_states);
        visible_revision = category_states.revision();
        visible_tree = installed.revision;
    };
    updateVisible();
    runBehaviors();
    updateVisible();
    if (std::chrono::steady_clock::now() - last_save > kSaveInterval) saveStates();

    texture_budget.reset();
    const TextureLookup texture = [](size_t pack, const std::string& path) -> ImTextureID {
        runtime::InstalledPack& installed_pack = *installed.packs[pack];
        const pack::DataSource* source = installed_pack.loaded->source.get();
        const TextureCache::Reader read = [source](const std::string& key, std::vector<uint8_t>* bytes) {
            std::string error;
            return source && source->read(key, bytes, &error);
        };
        return installed_pack.textures.get(pack::normalizePath(path), texture_budget, read).texture;
    };

    const std::shared_ptr<Scene> next = scenes.take();
    buildScene(live, visible, camera, static_cast<float>(ImGui::GetTime()), worldOptions(), texture, next.get());
    scenes.keep(next);
    publish(next);

    buildMapScene(texture);
}

void render() {
    // The overlay only on a frame GoodRender did not reach: no service, a loading screen.
    if (!good_render_sink::takeDrawn()) {
        if (const std::shared_ptr<const Scene> scene = published()) {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));
            // Behind our own windows and the editor's, but over the game.
            if (camera.valid) overlay::drawScene(ImGui::GetBackgroundDrawList(), camera, *scene);
        }
    }
    if (!good_render_sink::takeMapDrawn()) {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        map_scene::drawPublished(ImGui::GetBackgroundDrawList(), mumble_link, glm::vec2(display.x, display.y));
    }
    drawMapMenu();

    const auto script_menu = [] {
        if (persisted.run_scripts) ui::script_console::drawMenu(scripts.get());
    };
    // A player's own toggle is written at once, not with the scripts' changes.
    if (ui::category_menu::draw(installed.tree, &category_states, &markers_window, loadingStatus(), script_menu)) saveStates();
    ui::script_console::draw(*scripts, installed, &console_window);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));
    if (camera.valid && !camera.map_open &&
        ui::interaction::draw(
            screen,
            static_cast<settings::InfoDisplay>(persisted.info_display),
            keyName(persisted.interact_key),
            persisted.show_interact_prompt)) {
        behaviors::interactWith(&live, screen.prompt->marker, behaviorHost(), behaviorFrame());
    }
}

void drawOptions() {
    // Applied at the next load rather than now, both ways. Loading the editor means
    // registering render callbacks, and Nexus holds its render mutex across every callback it
    // runs (Context::Render in Nexus's UI/UiContext.cpp), so registering from inside one would
    // deadlock. Unloading it now would throw away a pack's unsaved edits.
    if (ImGui::Checkbox("Enable pack editing", &persisted.enable_editing)) settings::save(folder, persisted);
    if (persisted.enable_editing != editing_this_session) ImGui::Text("Takes effect the next time the addon loads.");

    if (ImGui::Checkbox("Read Blish HUD's packs", &persisted.read_blish_folder)) {
        settings::save(folder, persisted);
        reloadPacks();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn off while Blish HUD is running, or every marker shows twice.");

    if (ImGui::Checkbox("Run pack scripts", &persisted.run_scripts)) {
        settings::save(folder, persisted);
        // Off throws every handler away, and what scripts did to the map with them; on starts
        // them again from pack.lua.
        scripts->reset();
        replaceLiveMap(live.map_id);
    }

    if (ImGui::CollapsingHeader("Markers")) {
        if (ui::options::drawPack(&persisted, &show_hidden)) {
            interact_key = persisted.interact_key;
            settings::save(folder, persisted);
        }
    }
    if (ImGui::CollapsingHeader("Maps") && ui::options::drawMaps(&persisted)) settings::save(folder, persisted);

    if (ImGui::Button("Reload packs")) reloadPacks();
    ImGui::SameLine();
    ImGui::Text("%s", loadingStatus().c_str());

    ImGui::Checkbox("Markers window", &markers_window);
    ImGui::SameLine();
    ImGui::Checkbox("Script console", &console_window);

    if (editor.draw_options) {
        ImGui::Separator();
        editor.draw_options();
    }
}

void drawQuickAccess() {
    if (ImGui::MenuItem("Markers")) markers_window = !markers_window;
    if (ImGui::MenuItem("Script console")) console_window = !console_window;
}

void onMarkersKeybind(const char*, bool is_release) {
    if (!is_release) markers_window = !markers_window;
}

} // namespace

void load(AddonAPI* aApi, Mumble::Data* aMumbleLink) {
    api = aApi;
    mumble_link = aMumbleLink;
    nexus_link = static_cast<const NexusLinkData*>(api->DataLink.Get("DL_NEXUS_LINK"));

    // Nexus makes the directory; the files are ours.
    const char* directory = api->Paths.GetAddonDirectory(ADDON_FOLDER);
    folder = directory ? std::filesystem::path(directory) : std::filesystem::path();
    persisted = settings::load(folder);
    editing_this_session = persisted.enable_editing;
    category_states.load(categoriesFile());
    saved_categories = category_states.revision();
    script_storage.load(storageFile());
    behavior_states.load(timersFile());
    interact_key = persisted.interact_key;
    festivals.start();
    maps.start(mapsFile());

    script::Host host;
    host.installed = &installed;
    host.live = &live;
    host.category_states = &category_states;
    host.storage = &script_storage;
    host.mumble = &mumble_state;
    // User.SetClipboard: only "Never" stops a script, as in Pathing's Lib/User.cs.
    host.set_clipboard = [](const std::string& text, const std::string& message) {
        return persisted.clipboard_consent != static_cast<int>(behaviors::ClipboardConsent::Never) && setClipboard(text, message);
    };
    host.show_message = [](const std::string& text) {
        if (api) api->UI.SendAlert(text.c_str());
    };
    scripts = std::make_unique<script::Engine>(host);
    scripts_stale = true;

    discoverAndLoad();
    good_render_sink::load(mumble_link);

    api->Renderer.Register(ERenderType_PreRender, preRender);
    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
    api->WndProc.Register(onWndProc);
    api->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawQuickAccess);
    // "(null)": no default bind; the player sets one in Nexus.
    api->InputBinds.RegisterWithString(KEYBIND_MARKERS_ID, onMarkersKeybind, "(null)");
    api->Localization.Set(KEYBIND_MARKERS_ID, "en", "Toggle the markers window");
}

void unload() {
    // The service first: once unsubscribe returns no moment can be reading the scene.
    good_render_sink::unload();

    api->Renderer.Deregister(preRender);
    api->Renderer.Deregister(render);
    api->Renderer.Deregister(drawOptions);
    api->WndProc.Deregister(onWndProc);
    api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
    api->InputBinds.Deregister(KEYBIND_MARKERS_ID);

    loader.stop();
    festivals.stop();
    maps.stop();
    saveStates();
    scripts.reset();
    screen = behaviors::Screen{};
    publish(nullptr);
    scenes.clear();
    map_scene::publish(nullptr);
    map_scenes.clear();
    map_trails = map_scene::TrailCache{};
    live = runtime::LiveMap{};
    installed = runtime::Installed{};
    editor = {};
    api = nullptr;
}

bool editingEnabled() {
    return editing_this_session;
}

void connectEditor(const EditorHooks& hooks) {
    editor = hooks;
}

void setEditedPack(const std::filesystem::path& path) {
    edit_requested = path;
}

} // namespace viewer
