#include "viewer/viewer.h"

#include <imgui.h>
// For the rectangle of the button ImGui last saw clicked: where our icon in Nexus's bar is.
#include <IconsFontAwesome7.h>
#include <d3d11.h>
#include <imgui_internal.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pack/source.h"
#include "util/path_text.h"
#include "viewer/account.h"
#include "viewer/achievements.h"
#include "viewer/api_key.h"
#include "viewer/behaviors/festivals.h"
#include "viewer/behaviors/states.h"
#include "viewer/behaviors/update.h"
#include "viewer/blish_import.h"
#include "viewer/camera.h"
#include "viewer/catalog.h"
#include "viewer/downloads.h"
#include "viewer/game_binds.h"
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
#include "viewer/ui/fonts.h"
#include "viewer/ui/icon_data.h"
#include "viewer/ui/import_window.h"
#include "viewer/ui/interaction.h"
#include "viewer/ui/main_menu.h"
#include "viewer/ui/map_menu.h"
#include "viewer/ui/options.h"
#include "viewer/ui/pack_catalog.h"
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

// The game's device, which our font's atlas is uploaded to. Taken from Nexus's swap chain once
// there is one; ours to release.
ID3D11Device* font_device = nullptr;

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

// The pack open in the editor this frame, as EditorHooks::edited_live hands it over: drawn in
// the world and on the maps with the installed packs. Its markers belong to no node of our
// tree, which the editor's own decides for, so nothing of ours turns them off.
runtime::LiveMap* edited_live = nullptr;
TextureLookup edited_texture;
map_scene::TrailCache edited_map_trails;
const std::vector<char> kNoNodes;

// When packs were last checked for a write since they were read.
std::chrono::steady_clock::time_point last_change_check{};

bool console_window = false;
bool catalog_window = false;
bool options_window = false;

// The Blish HUD import: what Pathing has, what the player chose to bring, and the copy while it
// runs, which reads what Pathing kept too so that the frame only has to apply it.
bool import_window = false;
blish_import::Found import_found;
ui::import_window::Choices import_choices;
struct Imported {
    blish_import::Copied copied;
    blish_import::Found states; // what of Pathing's states was chosen
};
std::unique_ptr<http::Background<Imported>> import_job;
std::string import_status;

// The public catalog: its list, asked for at load; the downloads into our folder; the per-pack
// choices; every pack on disk as the catalog sees them; and whether this session's automatic
// updates have been started.
std::unique_ptr<http::Background<std::vector<catalog::Package>>> catalog_request;
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

// The API key in the options: what the player has typed, the check of what they registered
// while it runs, and what the last check said, until the key is removed. A check carries the
// key it was asked about, which is what is saved.
char key_field[128] = {};
struct KeyCheck {
    std::string key;
    api_key::Check check;
};
std::unique_ptr<http::Background<KeyCheck>> key_check;
std::string key_status;
// What the registered key says of the account: achievements and this week's raids.
account::Fetcher account_progress;
// What achievements are, for their tooltips.
achievements::Catalog achievement_details;

// Our icon in Nexus's bar was clicked: the menu opens under it on the next render. Set from
// Nexus's input handling, on a thread of its own, as is the bind turning All Markers on or off.
std::atomic<bool> menu_requested{false};
std::atomic<bool> all_markers_requested{false};
constexpr const char* kMenuPopup = "##marker_pack_menu";

// Pathing's smart categories: the menus whose hidden categories were asked for, until the map
// changes.
std::set<size_t> menu_show_all;
int menu_show_all_map = 0;

// The interact key, heard in the window procedure, which runs on the game's window thread
// while the frame runs on its render thread.
//
// The key is the game's, as Nexus keeps it: each of the two binds packed as its virtual key and,
// from bit 8, Shift, Ctrl and Alt; 0 for none. F, the game's default, when Nexus's file does not
// name Interact at all. Bound to the mouse or to keys not known here, it has no key: F then may
// well be some other action's, and a marker interacting on it would be a surprise.
std::atomic<uint32_t> interact_binds[2] = {'F', 0};
std::filesystem::path game_binds_file;
std::filesystem::file_time_type game_binds_written{};

uint32_t pack(const game_binds::Key& key) {
    return static_cast<uint32_t>(key.virtual_key) | (key.shift ? 1u << 8 : 0u) | (key.ctrl ? 2u << 8 : 0u) | (key.alt ? 4u << 8 : 0u);
}

// Reads Nexus's game binds again when the file changed: the player rebinding Interact in Nexus.
void readInteractBind() {
    std::error_code ec;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(game_binds_file, ec);
    if (ec || written == game_binds_written) return;
    game_binds_written = written;
    const std::vector<game_binds::Key> keys =
        game_binds::read(game_binds_file, game_binds::ACTION_INTERACT).value_or(std::vector<game_binds::Key>{game_binds::Key{'F'}});
    interact_binds[0] = keys.size() > 0 ? pack(keys[0]) : 0;
    interact_binds[1] = keys.size() > 1 ? pack(keys[1]) : 0;
}

// Whether `key`, just pressed, is one of the interact binds, with exactly its modifiers held: Ctrl+F
// is not F, and is likely some other action's.
bool isInteract(WPARAM key) {
    const auto held = [](int virtual_key) { return (GetKeyState(virtual_key) & 0x8000) != 0; };
    const uint32_t mods = (held(VK_SHIFT) ? 1u : 0u) | (held(VK_CONTROL) ? 2u : 0u) | (held(VK_MENU) ? 4u : 0u);
    return std::any_of(std::begin(interact_binds), std::end(interact_binds), [&](const std::atomic<uint32_t>& bind) {
        const uint32_t packed = bind.load();
        return packed != 0 && static_cast<WPARAM>(packed & 0xFF) == key && (packed >> 8) == mods;
    });
}

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
    readInteractBind();
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

// Lists every pack in our folder. Blish HUD's are drawn once imported into it, not from theirs.
void discover() {
    discovered.clear();
    if (!folder.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(packsFolder(), ec);
        discovered = registry::discoverPacks(packsFolder());
    }
    local_packs = catalog::localPacks(discovered);
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
        if (!runtime::isInstalled(installed, path)) added.push_back(path);
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

// The interact key's name as the keyboard writes it, for the prompt. With no key the prompt is
// still clicked, so that is what it says.
std::string keyName(int virtual_key) {
    if (virtual_key == 0) return "Click";
    const LONG scan = static_cast<LONG>(MapVirtualKeyW(static_cast<UINT>(virtual_key), MAPVK_VK_TO_VSC)) << 16;
    char name[64] = {};
    if (GetKeyNameTextA(scan, name, static_cast<int>(sizeof(name))) > 0) return name;
    return "?";
}

// Hears the interact key and Escape, and passes both on: the game interacts with what it has
// in front of it as well, as it does with Blish HUD running.
UINT onWndProc(HWND window, UINT message, WPARAM key, LPARAM flags) {
    // The editor first: a click it picks a marker with is not the game's.
    if (editor.wnd_proc && editor.wnd_proc(window, message, key, flags) == 0) return 0;
    const bool first_press = (flags & (LPARAM{1} << 30)) == 0;
    if (message == WM_KEYDOWN && first_press && isInteract(key)) interact_pressed = true;
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

// Takes what the loader has finished. A pack read again replaces its copy only now, so it is
// drawn all the while.
void updatePacks() {
    while (std::unique_ptr<registry::LoadedPack> loaded = loader.take()) {
        if (!loaded->error.empty()) api->Log(ELogLevel_WARNING, ADDON_NAME, (util::pathText(loaded->path) + ": " + loaded->error).c_str());
        runtime::uninstall(&installed, loaded->path);
        runtime::install(&installed, std::move(loaded));
    }
}

// Reads again every pack written since it was read: saved by the editor, or by anything else.
// Every few seconds, not every frame, and not while the loader is reading.
void reloadChangedPacks() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_change_check < std::chrono::seconds(2) || loader.busy()) return;
    last_change_check = now;
    std::vector<std::filesystem::path> changed;
    for (const std::unique_ptr<runtime::InstalledPack>& pack : installed.packs) {
        if (registry::writtenAt(pack->loaded->path) != pack->loaded->written) changed.push_back(pack->loaded->path);
    }
    if (!changed.empty()) loader.add(std::move(changed));
}

// Puts each download that arrived in place of its old copy. Not while the loader is busy: the
// pack it is reading may be the one being replaced.
void installDownloads() {
    if (loader.busy()) return;
    bool any = false;
    for (const catalog::Downloads::Finished& finished : downloads.take()) {
        any = true;
        // An installed pack keeps its archive open, and Windows will not replace an open file.
        runtime::uninstall(&installed, finished.target);
        if (!MoveFileExW(finished.part.c_str(), finished.target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            downloads.fail(finished, "could not replace " + util::pathText(finished.target.filename()));
        }
    }
    if (any) syncPacks();
}

// Deletes one of our packs from disk.
void deletePack(const std::filesystem::path& path) {
    runtime::uninstall(&installed, path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) api->Log(ELogLevel_WARNING, ADDON_NAME, ("could not delete " + util::pathText(path) + ": " + ec.message()).c_str());
    syncPacks();
}

void startCatalogRequest() {
    catalog_request = std::make_unique<http::Background<std::vector<catalog::Package>>>();
    catalog_request->start([](const http::Cancel& cancel) -> std::optional<std::vector<catalog::Package>> {
        const std::optional<std::string> body = http::get(catalog::REPO_URL, cancel, std::chrono::seconds(15));
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
        if (local && catalog::state(package, local) == catalog::State::Stale && auto_update.enabled(package.name)) {
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

// Registers the key the check was about once it says the key is fit, else says why not.
// From the frame rather than the options page, so a key is kept even if the page closed
// while the check ran.
void finishKeyCheck() {
    if (!key_check || !key_check->finished()) return;
    // The job always answers; one that did not counts as the API not answering.
    const KeyCheck done = key_check->get() ? *key_check->get() : KeyCheck{};
    key_check.reset();
    switch (done.check.outcome) {
    case api_key::Outcome::Valid:
        persisted.api_key = done.key;
        account_progress.setKey(persisted.api_key, utcNow());
        std::fill(std::begin(key_field), std::end(key_field), char{});
        key_status = settings::save(folder, persisted)
                         ? "Key added."
                         : "Key added, but it could not be saved: you will need to add it again next time.";
        break;
    case api_key::Outcome::MissingPermissions:
        key_status = "This key is missing permissions: " + done.check.missing + ". Make a new one with \"account\" and \"progression\" selected.";
        break;
    case api_key::Outcome::Rejected: key_status = "This key was not accepted. Check that you copied all of it."; break;
    case api_key::Outcome::NoAnswer: key_status = "Could not reach the GW2 API to check the key. Try again in a moment."; break;
    }
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

    std::string status = "Loading the list of packs...";
    const bool failed = !packages && catalog_request && catalog_request->finished();
    if (failed) status = "Could not load the list of packs.";
    if (packages) {
        status = std::to_string(packages->size()) + " packs available";
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

// Opens the import, reading again what Blish HUD has now.
void openImport() {
    if (import_job) {
        import_window = true;
        return;
    }
    import_found = blish_import::find(registry::blishMarkersFolder());
    import_choices = {};
    import_status.clear();
    import_window = true;
}

// Copies the chosen packs on a worker, and reads the chosen states with them.
void startImport() {
    std::vector<std::filesystem::path> packs;
    for (size_t i = 0; i < import_found.packs.size(); ++i) {
        if (i < import_choices.packs.size() && import_choices.packs[i]) packs.push_back(import_found.packs[i]);
    }
    Imported chosen;
    chosen.states.categories = import_found.categories;
    chosen.states.storage = import_found.storage;
    chosen.states.timers = import_found.timers;

    import_status = "Importing...";
    import_job = std::make_unique<http::Background<Imported>>();
    import_job->start([packs, chosen, into = packsFolder()](const http::Cancel&) -> std::optional<Imported> {
        Imported done = chosen;
        done.copied = blish_import::copyPacks(packs, into);
        return done;
    });
}

// Once the copy is done: Pathing's states into ours, written at once, and the new packs loaded.
void finishImport() {
    if (!import_job || !import_job->finished()) return;
    const Imported* done = import_job->get();
    if (!done) {
        import_job.reset();
        import_status = "The import did not finish.";
        return;
    }

    for (const auto& [id, on] : done->states.categories) category_states.remember(id, on);
    for (const auto& [key, value] : done->states.storage) script_storage.upsert(key, value);
    if (!done->states.timers.empty()) {
        behaviors::BehaviorStates theirs;
        theirs.load(done->states.timers);
        behavior_states.merge(theirs);
    }
    saveStates();

    // A pack kept because ours was as new is imported as far as the player can tell.
    const size_t imported = done->copied.copied + done->copied.kept;
    import_status = imported == 1 ? "Imported 1 marker pack." : "Imported " + std::to_string(imported) + " marker packs.";
    for (const std::string& error : done->copied.errors) {
        import_status += "\nCould not import " + error;
        api->Log(ELogLevel_WARNING, ADDON_NAME, ("Blish HUD import: " + error).c_str());
    }
    import_job.reset();
    syncPacks();
}

void drawImport() {
    finishImport();
    const bool was_open = import_window;
    const ui::import_window::Answer answer =
        ui::import_window::draw(import_found, &import_choices, import_status, import_job != nullptr, &import_window);
    if (answer == ui::import_window::Answer::Import) startImport();
    // Answered, or closed: it does not open by itself again.
    if (!persisted.import_offered && (answer != ui::import_window::Answer::None || (was_open && !import_window))) {
        persisted.import_offered = true;
        settings::save(folder, persisted);
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

// Opens the menu under our icon, or above it in the lower half of the screen, and likewise left
// or right. Nexus's bar is an ImGui window, and ImGui keeps the rectangle of the button last
// clicked as its nav focus (SetFocusID in imgui.cpp): our icon, while it holds the mouse. The
// mouse itself, failing that.
void placeMenu() {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImRect icon(mouse, mouse);
    if (const ImGuiWindow* window = g.NavWindow) {
        const ImRect& relative = window->NavRectRel[0];
        const ImRect clicked(
            window->Pos.x + relative.Min.x,
            window->Pos.y + relative.Min.y,
            window->Pos.x + relative.Max.x,
            window->Pos.y + relative.Max.y);
        if (clicked.Contains(mouse)) icon = clicked;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool below = icon.GetCenter().y < display.y * 0.5f;
    const bool rightward = icon.GetCenter().x < display.x * 0.5f;
    ImGui::SetNextWindowPos(
        ImVec2(rightward ? icon.Min.x : icon.Max.x, below ? icon.Max.y : icon.Min.y),
        ImGuiCond_Always,
        ImVec2(rightward ? 0.0f : 1.0f, below ? 0.0f : 1.0f));
}

// The menu for a left click on our icon.
void drawMenuPopup() {
    if (menu_requested.exchange(false)) {
        placeMenu();
        ImGui::OpenPopup(kMenuPopup);
    }
    if (ImGui::BeginPopup(kMenuPopup)) {
        drawMenuItems();
        ImGui::EndPopup();
    }
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
    options.occlusion = static_cast<Occlusion>(persisted.occlusion);
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
        std::vector<map_scene::Layer> layers{map_scene::Layer{&live, &visible, texture, &map_trails}};
        if (edited_live) layers.push_back(map_scene::Layer{edited_live, &kNoNodes, edited_texture, &edited_map_trails});
        map_scene::build(layers, *details, mumble_state, mapOptions(), fade, uiScale(), next.get());
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
    context.live = {&live};
    if (edited_live) context.live.push_back(edited_live); // the map scene's layers, in order
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
    reloadChangedPacks();
    installDownloads();
    if (sync_pending) syncPacks();
    autoUpdate();
    account_progress.update(std::chrono::steady_clock::now(), utcNow());
    achievement_details.update(std::chrono::steady_clock::now());

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));

    refreshLiveMap(camera.valid ? static_cast<int>(camera.map_id) : live.map_id);
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
    runtime::startFadeIns(&live, visible, ImGui::GetTime());
    edited_live = editor.edited_live ? editor.edited_live(live.map_id, &edited_texture) : nullptr;
    if (edited_live) runtime::startFadeIns(edited_live, kNoNodes, ImGui::GetTime());
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

    std::vector<SceneLayer> layers{SceneLayer{&live, &visible, texture, false}};
    if (edited_live) layers.push_back(SceneLayer{edited_live, &kNoNodes, edited_texture, true});

    const std::shared_ptr<Scene> next = scenes.take();
    buildScene(layers, camera, static_cast<float>(ImGui::GetTime()), worldOptions(), next.get());
    scenes.keep(next);
    publish(next);

    buildMapScene(texture);
}

void drawOptionsWindow();

// The overlay draws what GoodRender did not reach: everything with no service, or a loading
// screen or the full map. Behind our own windows and the editor's, but over the game.
void drawWorld() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));
    const std::shared_ptr<const Scene> scene = published();

    const good_render_sink::Drawn drawn = good_render_sink::takeDrawn();
    // Not with the map up: this is over the game's UI, so over the map. The scene is still built
    // then, for GoodRender, which draws it under the map.
    if (scene && camera.valid && !camera.map_open && !(drawn.occluded && drawn.over)) {
        overlay::drawScene(ImGui::GetBackgroundDrawList(), camera, *scene, !drawn.occluded, !drawn.over);
    }
    if (!drawn.map) {
        map_scene::drawPublished(ImGui::GetBackgroundDrawList(), mumble_link, glm::vec2(display.x, display.y));
    }
}

// Our windows and ImGui's: what is drawn with our font pushed.
void drawFrame() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = fromMumble(mumble_link, glm::vec2(display.x, display.y));

    drawMapMenu();

    drawMenuPopup();
    // Before the windows it can open, so they show in the frame it was clicked in.
    drawOptionsWindow();
    ui::script_console::draw(*scripts, installed, folder.empty() ? folder : folder / ui::script_console::SAVE_FILE_NAME, &console_window);
    drawCatalog();
    drawImport();
    finishKeyCheck();
    // Either may have removed a pack, and the prompt below calls into the live map.
    refreshLiveMap(live.map_id);

    if (camera.valid && !camera.map_open &&
        ui::interaction::draw(
            screen,
            static_cast<settings::InfoDisplay>(persisted.info_display),
            keyName(static_cast<int>(interact_binds[0].load() & 0xFF)),
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
    takeFontDevice();
    // The world and the map before our font is pushed: see fonts::push.
    drawWorld();
    ui::fonts::push();
    drawFrame();
    ui::fonts::pop();
    // Over the world as well as in its windows, so it pushes the font around its windows itself.
    if (editor.render) editor.render();
}

void onMenuKeybind(const char*, bool is_release) {
    if (!is_release) menu_requested = true;
}

void onAllMarkersKeybind(const char*, bool is_release) {
    if (!is_release) all_markers_requested = true;
}

// Puts the menu where the options say: in Nexus's menu, and behind our icon in Nexus's bar. Safe
// from the options page, Nexus's or our window's: Nexus holds its quick access lock only while
// drawing the bar.
void applyMenuPlaces() {
    static bool in_nexus = false;
    static bool icon = false;
    if (persisted.menu_in_nexus != in_nexus) {
        if (persisted.menu_in_nexus) {
            api->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawNexusMenuItems);
        } else {
            api->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
        }
        in_nexus = persisted.menu_in_nexus;
    }
    if (persisted.menu_icon != icon) {
        if (persisted.menu_icon) {
            api->QuickAccess.Add(SHORTCUT_ID, ICON_TEXTURE_ID, ICON_HOVER_TEXTURE_ID, KEYBIND_MENU_ID, ADDON_NAME);
            api->QuickAccess.AddContextMenu(SHORTCUT_MENU_ID, SHORTCUT_ID, drawMenuItems);
        } else {
            api->QuickAccess.RemoveContextMenu(SHORTCUT_MENU_ID);
            api->QuickAccess.Remove(SHORTCUT_ID);
        }
        icon = persisted.menu_icon;
    }
}

void removeMenuPlaces() {
    const bool in_nexus = persisted.menu_in_nexus;
    const bool icon = persisted.menu_icon;
    persisted.menu_in_nexus = false;
    persisted.menu_icon = false;
    applyMenuPlaces();
    persisted.menu_in_nexus = in_nexus;
    persisted.menu_icon = icon;
}

void drawOptionsPage() {
    bool places = ImGui::Checkbox("Show the menu in Nexus's menu", &persisted.menu_in_nexus);
    places |= ImGui::Checkbox("Show an icon in Nexus's bar", &persisted.menu_icon);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click it to open the Markus menu.");
    if (places) {
        applyMenuPlaces();
        settings::save(folder, persisted);
    }
    ImGui::Separator();

    if (ImGui::Button(ui::fonts::label(ICON_FA_FILE_IMPORT, "Import from Blish HUD...").c_str())) openImport();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies your marker packs from Blish HUD, with the categories you turned on or off\nand the markers you hid.");

    const bool scripts_changed = ImGui::Checkbox("Run pack scripts", &persisted.run_scripts);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Some packs come with scripts that add menus or change their markers as you play.");
    if (scripts_changed) {
        settings::save(folder, persisted);
        // Off throws every handler away, and what scripts did to the map with them; on starts
        // them again from pack.lua.
        scripts->reset();
        replaceLiveMap(live.map_id);
    }

    if (ImGui::CollapsingHeader("Markers")) {
        if (ui::options::drawPack(&persisted, &show_hidden)) settings::save(folder, persisted);
    }
    if (ImGui::CollapsingHeader("Minimap") && ui::options::drawMinimap(&persisted)) settings::save(folder, persisted);
    if (ImGui::CollapsingHeader("Full map") && ui::options::drawFullMap(&persisted)) settings::save(folder, persisted);
    if (ImGui::CollapsingHeader("GW2 API key")) {
        switch (ui::options::drawApiKey(persisted.api_key, key_field, sizeof(key_field), key_check != nullptr, key_status)) {
        case ui::options::KeyAction::None: break;
        case ui::options::KeyAction::OpenApplications: openInBrowser(api_key::APPLICATIONS_URL); break;
        case ui::options::KeyAction::Register:
            if (std::string key = api_key::trimmed(key_field); key.empty()) {
                key_status = "Paste a key in the field first.";
            } else {
                key_status.clear();
                key_check = std::make_unique<http::Background<KeyCheck>>();
                key_check->start([key = std::move(key)](const http::Cancel& cancel) -> std::optional<KeyCheck> {
                    return KeyCheck{key, api_key::ask(key, cancel)};
                });
            }
            break;
        case ui::options::KeyAction::Remove:
            persisted.api_key.clear();
            settings::save(folder, persisted);
            // The progress kept was the key's: it goes with it.
            account_progress.setKey(persisted.api_key, utcNow());
            account_progress.clearCache();
            key_status.clear();
            break;
        case ui::options::KeyAction::ClearCache:
            account_progress.clearCache();
            achievement_details.clear();
            key_status = "Saved API data cleared.";
            break;
        }
    }

    if (ImGui::Button(ui::fonts::label(ICON_FA_ROTATE_RIGHT, "Reload packs").c_str())) reloadPacks();
    ImGui::SameLine();
    ImGui::Text("%s", loadingStatus().c_str());

    ImGui::Checkbox("Script console", &console_window);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Messages and errors from pack scripts, for pack authors.");
    ImGui::SameLine();
    ImGui::Checkbox("Marker Packs", &catalog_window);
}

void drawOptions() {
    takeFontDevice();
    ui::fonts::push();
    drawOptionsPage();
    ui::fonts::pop();
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
    account_progress.open(folder.empty() ? folder : folder / account::CACHE_FILE_NAME, persisted.api_key, utcNow());
    achievement_details.open(folder.empty() ? folder : folder / achievements::CACHE_FILE_NAME);
    category_states.load(categoriesFile());
    saved_categories = category_states.revision();
    script_storage.load(storageFile());
    behavior_states.load(timersFile());
    if (const char* game = api->Paths.GetGameDirectory()) {
        game_binds_file = std::filesystem::path(game) / "addons" / "Nexus" / game_binds::FILE_NAME;
        readInteractBind();
    }
    festivals.start();
    maps.start(mapsFile());

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
    scripts = std::make_unique<script::Engine>(host);
    scripts_stale = true;

    auto_update.load(catalogFile());
    discoverAndLoad();
    startCatalogRequest();
    // Offered once, by itself, when Blish HUD has anything to bring.
    if (!persisted.import_offered) {
        import_found = blish_import::find(registry::blishMarkersFolder());
        import_window = !import_found.empty();
        if (!import_window) persisted.import_offered = true;
    }
    good_render_sink::load(mumble_link);

    api->Renderer.Register(ERenderType_PreRender, preRender);
    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, drawOptions);
    api->WndProc.Register(onWndProc);
    // "(null)": no default bind; the player sets one in Nexus. Our icon presses it.
    // Nexus presses a shortcut's bind for a left click on it and has no other way to say so, so
    // the menu has a bind: unset, and named for what it is there for.
    api->InputBinds.RegisterWithString(KEYBIND_MENU_ID, onMenuKeybind, "(null)");
    api->Localization.Set(KEYBIND_MENU_ID, "en", "Open Markus menu");
    api->InputBinds.RegisterWithString(KEYBIND_ALL_MARKERS_ID, onAllMarkersKeybind, "(null)");
    api->Localization.Set(KEYBIND_ALL_MARKERS_ID, "en", "Toggle Markers"); // Pathing's name for it
    api->Textures.GetOrCreateFromMemory(ICON_TEXTURE_ID, const_cast<unsigned char*>(ui::icon_data::kIcon), sizeof(ui::icon_data::kIcon));
    api->Textures.GetOrCreateFromMemory(
        ICON_HOVER_TEXTURE_ID,
        const_cast<unsigned char*>(ui::icon_data::kIconHover),
        sizeof(ui::icon_data::kIconHover));
    applyMenuPlaces();
}

void unload() {
    // The service first: once unsubscribe returns no moment can be reading the scene.
    good_render_sink::unload();

    api->Renderer.Deregister(preRender);
    api->Renderer.Deregister(render);
    api->Renderer.Deregister(drawOptions);
    api->WndProc.Deregister(onWndProc);
    removeMenuPlaces();
    ui::fonts::unload();
    if (font_device) {
        font_device->Release();
        font_device = nullptr;
    }
    api->InputBinds.Deregister(KEYBIND_MENU_ID);
    api->InputBinds.Deregister(KEYBIND_ALL_MARKERS_ID);

    loader.stop();
    downloads.stop();
    catalog_request.reset();
    key_check.reset();
    account_progress.stop();
    achievement_details.stop();
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

void connectEditor(const EditorHooks& hooks) {
    editor = hooks;
}

} // namespace viewer
