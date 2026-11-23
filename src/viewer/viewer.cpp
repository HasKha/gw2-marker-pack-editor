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
#include "viewer/catalog.h"
#include "viewer/downloads.h"
#include "viewer/edited.h"
#include "viewer/edits.h"
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
#include "viewer/ui/pack_catalog.h"
#include "viewer/ui/pack_list.h"
#include "viewer/ui/script_console.h"
#include "viewer/utc.h"

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

// The pack open in the editor, drawn from its model. Its behaviors run, with no scripts and a
// screen of their own that is not drawn: a marker's info and prompt index the live map it is
// in, and the one the player sees belongs to the installed packs'.
edited::Report edited_report;
edited::EditedLive edited_live;
behaviors::Screen edited_screen;

bool markers_window = false;
bool console_window = false;
bool catalog_window = false;
bool packs_window = false;

// Edit's copy of a pack into the edits folder, which runs on a worker, where it goes, and what
// Edit last had to say.
std::unique_ptr<Background<std::string>> edit_copy;
std::filesystem::path edit_copy_target;
std::string edit_status;

// The public catalog: its list, asked for at load; the downloads into our folder; the per-pack
// choices; every pack on disk as the catalog sees them; and whether this session's automatic
// updates have been started.
std::unique_ptr<Background<std::vector<catalog::Package>>> catalog_request;
catalog::Downloads downloads;
catalog::AutoUpdate auto_update;
std::vector<catalog::LocalPack> local_packs;
bool auto_updated = false;
// The packs on disk changed while the loader was busy: they are synced once it is done.
bool sync_pending = false;

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

std::filesystem::path packsFolder() {
    return folder.empty() ? folder : folder / PACKS_FOLDER;
}

std::filesystem::path editsFolder() {
    return folder.empty() ? folder : folder / edits::FOLDER;
}

std::filesystem::path catalogFile() {
    return folder.empty() ? folder : folder / catalog::STATE_FILE_NAME;
}

std::filesystem::path mapsFile() {
    return folder.empty() ? folder : folder / map::CACHE_FILE_NAME;
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

// Lists every pack: the personal copies, our own folder's and, unless turned off, Blish HUD's,
// each hiding the later ones' packs of the same name. The catalog sees the last two only: a
// personal copy is the player's, not the catalog's.
void discover() {
    std::vector<std::filesystem::path> copies;
    std::vector<std::filesystem::path> ours;
    if (!folder.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(packsFolder(), ec);
        ours = registry::discoverPacks(packsFolder());
        if (std::filesystem::is_directory(editsFolder(), ec)) copies = registry::discoverPacks(editsFolder());
    }
    std::vector<std::filesystem::path> blish;
    if (persisted.read_blish_folder) {
        const std::filesystem::path blish_folder = registry::blishMarkersFolder();
        std::error_code ec;
        if (!blish_folder.empty() && std::filesystem::is_directory(blish_folder, ec)) blish = registry::discoverPacks(blish_folder);
    }
    local_packs = catalog::localPacks(registry::mergeSources({ours, blish}), packsFolder());
    discovered = registry::mergeSources({copies, ours, blish});
}

bool isDiscovered(const std::filesystem::path& path) {
    return std::any_of(discovered.begin(), discovered.end(), [&](const std::filesystem::path& known) {
        return registry::samePack(known, path);
    });
}

void discoverAndLoad() {
    discover();
    loader.add(discovered);
}

// Brings the installed packs in line with the folders after the catalog wrote or deleted one:
// what is gone is dropped, what is new is loaded, and nothing else is read again. Waits for
// the loader, which may be reading a pack that is about to go.
void syncPacks() {
    if (loader.busy()) {
        sync_pending = true;
        return;
    }
    sync_pending = false;
    discover();

    std::vector<std::filesystem::path> gone;
    for (const std::unique_ptr<runtime::InstalledPack>& pack : installed.packs) {
        if (!isDiscovered(pack->loaded->path)) gone.push_back(pack->loaded->path);
    }
    for (const std::filesystem::path& path : gone) runtime::uninstall(&installed, path);

    std::vector<std::filesystem::path> added;
    for (const std::filesystem::path& path : discovered) {
        if (runtime::isInstalled(installed, path) || registry::samePack(path, edit_applied) || registry::samePack(path, edit_requested)) {
            continue;
        }
        added.push_back(path);
    }
    loader.add(std::move(added));
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

    behaviors::Host edited_host = host;
    edited_host.script = nullptr;
    edited_host.screen = &edited_screen;
    // Its markers belong to no node of the shared tree: the editor's tree decided what is in it.
    behaviors::update(&edited_live.live(), {}, edited_host, frame);

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
    if (!edit_applied.empty() && isDiscovered(edit_applied)) loader.add({edit_applied});
    if (!edit_requested.empty()) runtime::uninstall(&installed, edit_requested);
    edit_applied = edit_requested;
}

// Puts each download that arrived in place of its old copy. Not while the loader is busy: the
// pack it is reading may be the one being replaced.
void installDownloads() {
    if (loader.busy()) return;
    bool any = false;
    for (const catalog::Downloads::Finished& finished : downloads.take()) {
        any = true;
        if (registry::samePack(finished.target, edit_applied)) {
            downloads.fail(finished, "it is open in the editor: close it and download again");
            continue;
        }
        // An installed pack keeps its archive open, and Windows will not replace an open file.
        runtime::uninstall(&installed, finished.target);
        if (!MoveFileExW(finished.part.c_str(), finished.target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            downloads.fail(finished, "could not replace " + finished.target.filename().string());
        }
    }
    if (any) syncPacks();
}

// Deletes one of our own packs, or a personal copy, from disk. Blish HUD's folder is only ever
// read.
void deletePack(const std::filesystem::path& path) {
    if (registry::samePack(path, edit_applied)) return;
    runtime::uninstall(&installed, path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) api->Log(ELogLevel_WARNING, ADDON_NAME, ("could not delete " + path.string() + ": " + ec.message()).c_str());
    syncPacks();
}

void startCatalogRequest() {
    catalog_request = std::make_unique<Background<std::vector<catalog::Package>>>();
    catalog_request->start([]() -> std::optional<std::vector<catalog::Package>> {
        const std::optional<std::string> body = http::get(catalog::REPO_URL, std::chrono::seconds(15));
        return body ? catalog::parse(*body) : std::nullopt;
    });
}

// Pathing's "Keep Updated": once a session, as soon as the catalog has answered, each of our
// packs older than the catalog's is downloaded again.
void autoUpdate() {
    const std::vector<catalog::Package>* packages = catalog_request ? catalog_request->get() : nullptr;
    if (auto_updated || !packages) return;
    auto_updated = true;
    for (const catalog::Package& package : *packages) {
        const catalog::LocalPack* local = catalog::findLocal(package, local_packs);
        if (local && local->ours && catalog::state(package, local) == catalog::State::Stale && auto_update.enabled(package.name)) {
            downloads.start(package, packsFolder());
        }
    }
}

// A web page in the player's browser.
void openInBrowser(const std::string& url) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (length <= 0) return;
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), length);
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void drawCatalog() {
    if (!catalog_window) return;
    const std::vector<catalog::Package>* packages = catalog_request ? catalog_request->get() : nullptr;

    std::vector<ui::pack_catalog::Row> rows;
    size_t stale = 0;
    if (packages) {
        rows.reserve(packages->size());
        for (const catalog::Package& package : *packages) {
            ui::pack_catalog::Row row;
            row.package = &package;
            row.local = catalog::findLocal(package, local_packs);
            row.state = catalog::state(package, row.local);
            row.download = downloads.status(package.file_name);
            row.keep_updated = auto_update.enabled(package.name);
            if (row.state == catalog::State::Stale) ++stale;
            rows.push_back(row);
        }
    }

    std::string status = "Asking the catalog...";
    const bool failed = !packages && catalog_request && catalog_request->finished();
    if (failed) status = "Could not reach the catalog.";
    if (packages) {
        status = std::to_string(packages->size()) + " packs in the catalog";
        if (stale > 0) status += ", " + std::to_string(stale) + " with an update";
    }

    bool retry = false;
    for (const ui::pack_catalog::Action& action : ui::pack_catalog::draw(rows, status, failed, &retry, &catalog_window)) {
        const ui::pack_catalog::Row& row = rows[action.row];
        switch (action.kind) {
        case ui::pack_catalog::Action::Kind::Download: downloads.start(*row.package, packsFolder()); break;
        case ui::pack_catalog::Action::Kind::Delete:
            if (row.local) deletePack(row.local->path);
            break;
        case ui::pack_catalog::Action::Kind::KeepUpdated:
            auto_update.set(row.package->name, action.value);
            auto_update.save(catalogFile());
            break;
        case ui::pack_catalog::Action::Kind::OpenPage: openInBrowser(row.package->info); break;
        }
    }
    if (retry) startCatalogRequest();
}

// Hands `path` to the editor, which draws it from then on, and lists the packs again: a new
// personal copy stands in for the pack it was copied from.
void openInEditor(const std::filesystem::path& path) {
    edit_status = editor.open_pack(path);
    if (!edit_status.empty()) return;
    edit_requested = path;
    syncPacks();
}

// Edit: a personal copy in place, any other pack by way of a copy into the edits folder, made on
// a worker since a large pack takes a while.
void editPack(const std::filesystem::path& path) {
    if (!editor.open_pack || edit_copy) return;
    const std::filesystem::path target = edits::target(path, editsFolder());
    if (target.empty()) {
        edit_status = "A folder pack is edited in place: open it from the editor's File menu.";
        return;
    }
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        openInEditor(target);
        return;
    }
    edit_status = "Copying " + path.filename().string() + " into your edits...";
    edit_copy_target = target;
    edit_copy = std::make_unique<Background<std::string>>();
    edit_copy->start([from = path, to = target]() -> std::optional<std::string> { return edits::copy(from, to); });
}

// Opens the copy Edit made, once it is made.
void finishEditCopy() {
    if (!edit_copy || !edit_copy->finished()) return;
    const std::string error = edit_copy->get() ? *edit_copy->get() : "the copy did not finish";
    edit_copy.reset();
    if (error.empty()) {
        openInEditor(edit_copy_target);
    } else {
        edit_status = error;
    }
}

std::string loadingStatus() {
    if (!loader.busy()) return std::to_string(installed.packs.size()) + " packs";
    return "Loading packs: " + std::to_string(installed.packs.size()) + " of " + std::to_string(discovered.size());
}

// Which folder a pack is in, as the player knows it.
std::string packFolderName(const std::filesystem::path& path, const std::filesystem::path& blish_folder) {
    if (registry::isInside(path, editsFolder())) return "your edits";
    if (registry::isInside(path, packsFolder())) return "downloaded";
    if (registry::isInside(path, blish_folder)) return "Blish HUD's";
    return path.parent_path().string();
}

void drawPackList() {
    finishEditCopy();
    if (!packs_window) return;

    std::vector<ui::pack_list::Row> rows;
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path blish_folder = registry::blishMarkersFolder();
    const auto add = [&](const std::filesystem::path& path, const std::string& error, bool in_editor) {
        rows.push_back(
            {path.filename().string(), packFolderName(path, blish_folder), error, registry::isInside(path, editsFolder()), in_editor});
        paths.push_back(path);
    };
    if (!edit_applied.empty()) add(edit_applied, "", true);
    for (const std::unique_ptr<runtime::InstalledPack>& pack : installed.packs) add(pack->loaded->path, pack->loaded->error, false);

    const std::string status = edit_status.empty() ? loadingStatus() : edit_status;
    const std::optional<ui::pack_list::Action> action = ui::pack_list::draw(rows, editor.open_pack && !edit_copy, status, &packs_window);
    if (!action) return;
    switch (action->kind) {
    case ui::pack_list::Action::Kind::Edit: editPack(paths[action->row]); break;
    case ui::pack_list::Action::Kind::Delete: deletePack(paths[action->row]); break;
    }
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
    installDownloads();
    if (sync_pending) syncPacks();
    autoUpdate();

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));

    if (camera.valid && (live.map_id != static_cast<int>(camera.map_id) || live.revision != installed.revision)) {
        replaceLiveMap(static_cast<int>(camera.map_id));
    }
    const edited::TrailLookup edited_trail = [](const std::string& path, bool* deferred) {
        return editor.trail ? editor.trail(path, deferred) : nullptr;
    };
    const int edited_map = camera.valid ? static_cast<int>(camera.map_id) : edited_live.live().map_id;
    // What the old entities put on screen goes with them, as with the installed packs' live map.
    if (edited_live.update(edited_report, edited_map, edited_trail)) edited_screen = behaviors::Screen{};
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

    const TextureLookup edited_texture = [](size_t, const std::string& path) -> ImTextureID {
        return editor.texture ? editor.texture(path) : nullptr;
    };
    static const std::vector<char> kNoNodes;
    const std::vector<SceneLayer> layers{
        SceneLayer{&live, &visible, texture, false},
        SceneLayer{&edited_live.live(), &kNoNodes, edited_texture, true},
    };

    const std::shared_ptr<Scene> next = scenes.take();
    buildScene(layers, camera, static_cast<float>(ImGui::GetTime()), worldOptions(), next.get());
    scenes.keep(next);
    publish(next);

    buildMapScene(texture);
}

void render() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));
    const std::shared_ptr<const Scene> scene = published();

    // The overlay only on a frame GoodRender did not reach: no service, a loading screen.
    // Behind our own windows and the editor's, but over the game.
    if (!good_render_sink::takeDrawn() && scene && camera.valid) overlay::drawScene(ImGui::GetBackgroundDrawList(), camera, *scene);
    // Over the world however it was drawn: the editor's outlines, handles and missing images.
    if (editor.draw_overlay && scene && camera.valid && !camera.map_open) editor.draw_overlay(camera, *scene);
    if (!good_render_sink::takeMapDrawn()) {
        map_scene::drawPublished(ImGui::GetBackgroundDrawList(), mumble_link, glm::vec2(display.x, display.y));
    }
    drawMapMenu();

    const auto script_menu = [] {
        if (persisted.run_scripts) ui::script_console::drawMenu(scripts.get());
    };
    // A player's own toggle is written at once, not with the scripts' changes.
    if (ui::category_menu::draw(installed.tree, &category_states, &markers_window, loadingStatus(), script_menu)) saveStates();
    ui::script_console::draw(*scripts, installed, &console_window);
    drawCatalog();
    drawPackList();

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
    ImGui::SameLine();
    ImGui::Checkbox("Pack catalog", &catalog_window);
    ImGui::SameLine();
    ImGui::Checkbox("Packs", &packs_window);

    if (editor.draw_options) {
        ImGui::Separator();
        editor.draw_options();
    }
}

void drawQuickAccess() {
    if (ImGui::MenuItem("Markers")) markers_window = !markers_window;
    if (ImGui::MenuItem("Script console")) console_window = !console_window;
    if (ImGui::MenuItem("Pack catalog")) catalog_window = !catalog_window;
    if (ImGui::MenuItem("Packs")) packs_window = !packs_window;
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

    auto_update.load(catalogFile());
    discoverAndLoad();
    startCatalogRequest();
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
    downloads.stop();
    catalog_request.reset();
    edit_copy.reset();
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
    edited_live.clear();
    edited_report = {};
    edited_screen = behaviors::Screen{};
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

void setEditedPack(const edited::Report& report) {
    edited_report = report;
    edit_requested = report.path;
}

} // namespace viewer
