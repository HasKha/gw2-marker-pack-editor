#include "viewer/viewer.h"

#include <imgui.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "pack/source.h"
#include "viewer/camera.h"
#include "viewer/registry.h"
#include "viewer/render/good_render.h"
#include "viewer/render/overlay.h"
#include "viewer/runtime.h"
#include "viewer/scene.h"
#include "viewer/script/engine.h"
#include "viewer/script/mumble.h"
#include "viewer/settings.h"
#include "viewer/store.h"
#include "viewer/textures.h"
#include "viewer/ui/category_menu.h"
#include "viewer/ui/script_console.h"

namespace viewer {

namespace {

AddonAPI* api = nullptr;
Mumble::Data* mumble_link = nullptr;
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

// The last scene published, and the one before it, kept to be reused once nothing else
// holds it. A scene is thousands of items, and allocating one a frame is a waste.
std::shared_ptr<Scene> current_scene;
std::shared_ptr<Scene> spare_scene;

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
script::MumbleState mumble_state;
store::KeyValues script_storage;
bool scripts_stale = true;

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

void saveStates() {
    if (category_states.revision() != saved_categories && category_states.save(categoriesFile()))
        saved_categories = category_states.revision();
    if (script_storage.dirty()) script_storage.save(storageFile());
    last_save = std::chrono::steady_clock::now();
}

// User.SetClipboard. Pathing's consent setting, whose default lets packs copy, comes with the
// rest of its interaction settings; until then every copy is allowed, and said.
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
    live = runtime::liveMap(installed, map_id);
    scripts_stale = true;
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
    mumble_state = script::mumbleState(mumble_link);
    runScripts();
    if (std::chrono::steady_clock::now() - last_save > kSaveInterval) saveStates();

    if (visible_revision != category_states.revision() || visible_tree != installed.revision) {
        visible = runtime::visibleNodes(installed.tree, category_states);
        visible_revision = category_states.revision();
        visible_tree = installed.revision;
    }

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

    std::shared_ptr<Scene> next = spare_scene && spare_scene.use_count() == 1 ? spare_scene : std::make_shared<Scene>();
    buildScene(live, visible, camera, static_cast<float>(ImGui::GetTime()), texture, next.get());
    spare_scene = std::move(current_scene);
    current_scene = next;
    publish(std::move(next));
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

    const auto script_menu = [] {
        if (persisted.run_scripts) ui::script_console::drawMenu(scripts.get());
    };
    // A player's own toggle is written at once, not with the scripts' changes.
    if (ui::category_menu::draw(installed.tree, &category_states, &markers_window, loadingStatus(), script_menu)) saveStates();
    ui::script_console::draw(*scripts, installed, &console_window);
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

    // Nexus makes the directory; the files are ours.
    const char* directory = api->Paths.GetAddonDirectory(ADDON_FOLDER);
    folder = directory ? std::filesystem::path(directory) : std::filesystem::path();
    persisted = settings::load(folder);
    editing_this_session = persisted.enable_editing;
    category_states.load(categoriesFile());
    saved_categories = category_states.revision();
    script_storage.load(storageFile());

    script::Host host;
    host.installed = &installed;
    host.live = &live;
    host.category_states = &category_states;
    host.storage = &script_storage;
    host.mumble = &mumble_state;
    host.set_clipboard = setClipboard;
    host.show_message = [](const std::string& text) {
        if (api) api->UI.SendAlert(text.c_str());
    };
    scripts = std::make_unique<script::Engine>(host);
    scripts_stale = true;

    discoverAndLoad();
    good_render_sink::load();

    api->Renderer.Register(ERenderType_PreRender, preRender);
    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
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
    api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
    api->InputBinds.Deregister(KEYBIND_MARKERS_ID);

    loader.stop();
    saveStates();
    scripts.reset();
    publish(nullptr);
    current_scene.reset();
    spare_scene.reset();
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
