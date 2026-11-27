#include "viewer/viewer.h"

#include <IconsFontAwesome7.h>
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

// ── Parts of a session ──────────────────────────────────────────────────────────

// The category toggles, script Storage and behaviors' timers: read at load, and written on the
// worker whenever they changed, at most every few seconds, as Pathing flushes its states, since a
// script may change them every tick.
struct KeptStates {
    static constexpr std::chrono::seconds kSaveInterval{5};

    store::CategoryStates categories;
    store::KeyValues storage;
    behaviors::BehaviorStates timers;

    std::filesystem::path folder;
    util::KeptFile categories_file;
    util::KeptFile storage_file;
    util::KeptFile timers_file;
    std::chrono::steady_clock::time_point last_save;

    // Reads all three from `folder`, where they are written after. An empty folder keeps nothing.
    void load(std::filesystem::path from) {
        folder = std::move(from);
        categories.load(file(store::CATEGORIES_FILE_NAME));
        categories_file.loaded(categories.revision());
        storage.load(file(store::STORAGE_FILE_NAME));
        storage_file.loaded(storage.revision());
        timers.load(file(behaviors::TIMERS_FILE_NAME));
        timers_file.loaded(timers.revision());
    }

    // Hands what changed over to `jobs` to be written, dropping the timers that ran out first.
    void save(util::JobQueue* jobs) {
        categories_file.save(jobs, file(store::CATEGORIES_FILE_NAME), categories.revision(), [this] { return categories.text(); });
        storage_file.save(jobs, file(store::STORAGE_FILE_NAME), storage.revision(), [this] { return storage.text(); });
        timers.expire(utcNow());
        timers_file.save(jobs, file(behaviors::TIMERS_FILE_NAME), timers.revision(), [this] { return timers.text(); });
        last_save = std::chrono::steady_clock::now();
    }

    bool due() const { return std::chrono::steady_clock::now() - last_save > kSaveInterval; }

    std::filesystem::path file(const char* name) const { return folder.empty() ? folder : folder / name; }
};

// Which nodes of the tree show by the category toggles, worked out again only when the toggles
// or the tree moved.
struct VisibleNodes {
    std::vector<char> nodes;
    uint64_t states_revision = ~0ull;
    uint64_t tree_revision = ~0ull;

    void update(const runtime::Installed& installed, const store::CategoryStates& states) {
        if (states_revision == states.revision() && tree_revision == installed.revision) return;
        nodes = runtime::visibleNodes(installed.tree, states);
        states_revision = states.revision();
        tree_revision = installed.revision;
    }
};

// For the Development section: what built the last scene, how long Present's work took, and what
// GoodRender's moments drew in the last frame. For the slow-frame log: when the last Present
// began, where the frame since went, and which slow frames make a line.
struct FrameStats {
    CameraSource scene_camera = CameraSource::None;
    ui::diagnostics::FrameTimes present_times;
    good_render_sink::Drawn last_drawn;
    std::chrono::steady_clock::time_point last_present{};
    frame_log::Steps steps;
    frame_log::Limiter slow_frames;
};

// Our windows, each drawn while its flag is set.
struct Windows {
    bool console = false;
    bool catalog = false;
    bool options = false;
    bool import = false;
};

struct Menu {
    // Where the menu is placed in Nexus now: its own menu, and behind our icon in its bar.
    bool in_nexus = false;
    bool icon = false;
    // Pathing's smart categories: the menus whose hidden categories were asked for, until the
    // map changes.
    std::set<size_t> show_all;
    int show_all_map = 0;
};

// The pack open in the editor this frame, as EditorHooks::edited_live hands it over: drawn in the
// world and on the maps with the installed packs. Its markers belong to no node of our tree, which
// the editor's own decides for, so nothing of ours turns them off.
struct EditedPack {
    runtime::LiveMap* live = nullptr;
    TextureLookup texture;
};

// ── The session ─────────────────────────────────────────────────────────────────

// The viewer from load to unload. Nexus calls in through the functions after it, which reach the
// one running session.
//
// Members are destroyed bottom to top, and that is the teardown: each stops its own threads as it
// goes. So the job queue comes before everything that hands it work, and is the last to stop,
// letting a write queued in teardown land and a pack's queued image reads, with the pack gone,
// read nothing; what a part points at comes before it.
class Session
{
public:
    Session(AddonAPI* nexus, Mumble::Data* mumble);
    // The last save of the states, and nothing published for a sink to draw.
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Registers with Nexus, and lets GoodRender and RTAPI in, once the session is reachable.
    void attach();
    // The other way, before the session goes. The service first: once unsubscribe returns no
    // moment can be reading the scene.
    void detach();

    void preRender();
    void render();
    void drawOptions();
    // The menu's items, wherever it is shown: Nexus's menu, our icon's, or our own popup.
    void drawItems(bool separator);

private:
    std::filesystem::path inFolder(const std::filesystem::path& name) const;
    std::future<bool> saveSettings();
    void saveStates();
    bool setClipboard(const std::string& text, const std::string& message);
    void runScripts();
    void replaceLiveMap(int map_id);
    void refreshLiveMap(int map_id);
    behaviors::Host behaviorHost();
    behaviors::Frame behaviorFrame();
    void drawCatalog();
    void openImport();
    void drawImport();
    void takeFontDevice();
    void drawMenuPopup();
    float uiScale();
    bool inGameplay() const;
    WorldOptions worldOptions();
    ImTextureID aboveBelowImage(bool above);
    Maps::Frame mapFrame(const TextureLookup& texture);
    void drawMapMenu();
    void logSlowFrame(std::chrono::steady_clock::time_point started);
    void drawWorld();
    void drawFrame();
    void placeMenu(bool in_nexus, bool icon);
    ui::diagnostics::Figures diagnosticFigures();
    void drawOptionsPage();
    void drawOptionsWindow();

    AddonAPI* api_ = nullptr;
    Mumble::Data* mumble_link_ = nullptr;
    // Read for its Scaling and IsGameplay only: its Width and Height go stale (see gameCamera).
    const NexusLinkData* nexus_link_ = nullptr;
    std::filesystem::path folder_;
    // The game's device, which our font's atlas is uploaded to. Taken from Nexus's swap chain once
    // there is one.
    Microsoft::WRL::ComPtr<ID3D11Device> font_device_;
    // After the device, so it goes first: its atlas was made on it.
    ui::fonts::Fonts fonts_;
    // Subscribed from attach to detach.
    std::optional<good_render_sink::Subscription> good_render_;

    // Every read and write of a file after load: the frame hands them over and takes the answer
    // on a later frame, never waiting for the disk itself.
    util::JobQueue jobs_;

    // What the options page edits and saves, against what this session loaded with.
    settings::Settings persisted_;
    KeptStates states_;

    // What the GW2 API says: the festivals on; what the registered key says of the account
    // (achievements and this week's raids); what achievements are, for their tooltips; and the
    // key in the options.
    behaviors::FestivalFetcher festivals_;
    account::Fetcher account_progress_;
    achievements::Catalog achievement_details_;
    api_key::Registration registration_;

    runtime::Installed installed_;
    PackFolder pack_folder_;
    runtime::LiveMap live_;
    VisibleNodes visible_;
    MumbleState mumble_state_;
    // What behaviors put on screen.
    behaviors::Screen screen_;
    // Pathing's "Temporarily Show Ghost Markers", which it turns off at every start: so a session
    // setting, never saved.
    bool show_hidden_ = false;

    // Scripts: one engine for every pack, built again with every live map, as Pathing's is with
    // every map. `scripts_stale_` holds from the moment the live map is replaced until the engine
    // has been reset onto it — a script's handles are indices into the live map, so it must not
    // tick in between. After everything its host points at.
    std::unique_ptr<script::Engine> scripts_;
    bool scripts_stale_ = true;

    catalog::Library library_;
    // The Blish HUD import, and what the player chose to bring.
    blish_import::Importer importer_;
    ui::import_window::Choices import_choices_;

    Recycled<Scene> scenes_;
    Maps maps_;
    EditedPack edited_;
    FrameStats stats_;
    Windows windows_;
    ui::pack_catalog::View catalog_view_;
    ui::map_menu::View map_menu_view_;
    Menu menu_;
};

std::unique_ptr<Session> session;

// ── What Nexus calls ────────────────────────────────────────────────────────────

void preRender() {
    session->preRender();
}

void render() {
    session->render();
}

void drawOptions() {
    session->drawOptions();
}

void drawMenuItems() {
    session->drawItems(true);
}

void drawNexusMenuItems() {
    session->drawItems(false);
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

// ── The session's work ──────────────────────────────────────────────────────────

Session::Session(AddonAPI* nexus, Mumble::Data* mumble)
    : api_(nexus)
    , mumble_link_(mumble) {
    nexus_link_ = static_cast<const NexusLinkData*>(api_->DataLink.Get("DL_NEXUS_LINK"));

    // Nexus makes the directory; the files are ours.
    const char* directory = api_->Paths.GetAddonDirectory(ADDON_FOLDER);
    folder_ = directory ? std::filesystem::path(directory) : std::filesystem::path();
    // Before anything reads from it. The editor asked for the folder already, so Nexus has made
    // it, but reads nothing from it before Open Editor.
    if (!folder_.empty()) {
        const std::filesystem::path addons = (folder_.has_filename() ? folder_ : folder_.parent_path()).parent_path();
        const std::string error = store::adoptFolder(addons / OLD_ADDON_FOLDER, folder_);
        if (!error.empty()) api_->Log(ELogLevel_WARNING, ADDON_NAME, error.c_str());
    }
    persisted_ = settings::load(folder_);
    // Read here, before there is a frame to stall; from here on every file goes through `jobs_`.
    account_progress_.open(inFolder(account::CACHE_FILE_NAME), persisted_.api_key, utcNow(), &jobs_);
    achievement_details_.open(inFolder(achievements::CACHE_FILE_NAME), &jobs_);
    states_.load(folder_);
    if (const char* game = api_->Paths.GetGameDirectory()) {
        interact_key.open(std::filesystem::path(game) / "addons" / "Nexus" / game_binds::FILE_NAME);
    }
    festivals_.start();
    maps_.open(inFolder(map::CACHE_FILE_NAME));

    script::Host host;
    host.installed = &installed_;
    host.live = &live_;
    host.category_states = &states_.categories;
    host.storage = &states_.storage;
    host.mumble = &mumble_state_;
    host.account = &account_progress_.progress();
    // User.SetClipboard: only "Never" stops a script, as in Pathing's Lib/User.cs.
    host.set_clipboard = [this](const std::string& text, const std::string& message) {
        return persisted_.clipboard_consent != static_cast<int>(behaviors::ClipboardConsent::Never) && setClipboard(text, message);
    };
    host.show_message = [this](const std::string& text) { api_->UI.SendAlert(text.c_str()); };
    installed_.api = api_;
    installed_.jobs = &jobs_;
    installed_.uploads = &texture_uploads;
    scripts_ = std::make_unique<script::Engine>(host);

    library_.open(inFolder(catalog::STATE_FILE_NAME), &jobs_);
    pack_folder_.open(inFolder(PACKS_FOLDER), &jobs_, api_);
    // Offered once, by itself, when Blish HUD has anything to bring.
    if (!persisted_.import_offered) {
        importer_.find(registry::blishMarkersFolder());
        windows_.import = !importer_.found().empty();
        if (!windows_.import) persisted_.import_offered = true;
    }
}

Session::~Session() {
    saveStates();
    publish(nullptr);
    maps_.stop();
}

void Session::attach() {
    good_render_.emplace(mumble_link_);
    rtapi::load(api_);

    api_->Renderer.Register(ERenderType_PreRender, viewer::preRender);
    api_->Renderer.Register(ERenderType_Render, viewer::render);
    api_->Renderer.Register(ERenderType_OptionsRender, viewer::drawOptions);
    api_->WndProc.Register(onWndProc);
    // "(null)": no default bind; the player sets one in Nexus. Our icon presses it.
    // Nexus presses a shortcut's bind for a left click on it and has no other way to say so, so
    // the menu has a bind: unset, and named for what it is there for.
    api_->InputBinds.RegisterWithString(KEYBIND_MENU_ID, onMenuKeybind, "(null)");
    api_->Localization.Set(KEYBIND_MENU_ID, "en", "Open Markus Menu");
    api_->InputBinds.RegisterWithString(KEYBIND_ALL_MARKERS_ID, onAllMarkersKeybind, "(null)");
    api_->Localization.Set(KEYBIND_ALL_MARKERS_ID, "en", "Toggle Markers"); // Pathing's name for it
    api_->Textures.GetOrCreateFromMemory(ICON_TEXTURE_ID, const_cast<unsigned char*>(ui::icon_data::kIcon), sizeof(ui::icon_data::kIcon));
    api_->Textures.GetOrCreateFromMemory(
        ICON_HOVER_TEXTURE_ID,
        const_cast<unsigned char*>(ui::icon_data::kIconHover),
        sizeof(ui::icon_data::kIconHover));
    placeMenu(persisted_.menu_in_nexus, persisted_.menu_icon);
}

void Session::detach() {
    good_render_.reset();
    rtapi::unload();

    api_->Renderer.Deregister(viewer::preRender);
    api_->Renderer.Deregister(viewer::render);
    api_->Renderer.Deregister(viewer::drawOptions);
    api_->WndProc.Deregister(onWndProc);
    placeMenu(false, false);
    api_->InputBinds.Deregister(KEYBIND_MENU_ID);
    api_->InputBinds.Deregister(KEYBIND_ALL_MARKERS_ID);
}

// `name` in our folder, or nothing while there is no folder.
std::filesystem::path Session::inFolder(const std::filesystem::path& name) const {
    return folder_.empty() ? folder_ : folder_ / name;
}

// Writes `persisted_` as it is now, on the worker: whether it landed, which only the API key's
// message waits for.
std::future<bool> Session::saveSettings() {
    return jobs_.ask([folder = folder_, saved = persisted_] { return settings::save(folder, saved); });
}

// Every few seconds: what changed is handed over to be written, and Nexus's binds are looked at
// again.
void Session::saveStates() {
    states_.save(&jobs_);
    jobs_.post([] { interact_key.read(); });
}

// The clipboard, for a `copy` marker and User.SetClipboard alike, telling the player with
// `message`. Whether a pack may copy at all is the caller's to ask.
bool Session::setClipboard(const std::string& text, const std::string& message) {
    const bool copied = desktop::copy(text);
    if (copied) api_->UI.SendAlert(message.c_str());
    return copied;
}

// Where the scripts are: freshly reset onto the live map once every pack is in, as Pathing
// runs them once a map's packs have loaded; then ticked every frame.
void Session::runScripts() {
    if (!persisted_.run_scripts) return;
    if (scripts_stale_) {
        if (pack_folder_.loading()) return;
        scripts_->reset();
        scripts_->loadPacks();
        scripts_stale_ = false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    scripts_->tick(ImGui::GetTime(), io.DeltaTime);
}

void Session::replaceLiveMap(int map_id) {
    // ReappearOnMapChange waits for the map, not for the packs to change under it.
    if (map_id != live_.map_id) states_.timers.mapChanged();
    live_ = runtime::liveMap(installed_, map_id);
    scripts_stale_ = true;
    // Every entity is new, and what the old ones put on screen goes with them, as Pathing
    // unloads its entities.
    screen_ = behaviors::Screen{};
}

// The live map, and the scripts' handles into it, index the installed packs: whatever installs
// or removes one leaves them pointing at packs that moved, until they are built again. Whether
// or not there is a camera — the menu removes packs at character select too.
void Session::refreshLiveMap(int map_id) {
    if (live_.map_id != map_id || live_.revision != installed_.revision) replaceLiveMap(map_id);
}

behaviors::Host Session::behaviorHost() {
    behaviors::Host host;
    host.states = &states_.timers;
    host.categories = &states_.categories;
    host.tree = &installed_.tree;
    host.screen = &screen_;
    host.copy = [this](const std::string& text, const std::string& message) { return setClipboard(text, message); };
    // Script.Update waits until every pack is in, and so do the engine's handles.
    if (persisted_.run_scripts && !scripts_stale_) {
        host.script = [this](const behaviors::ScriptCall& call, behaviors::Entity entity, behaviors::Hook hook, bool flag) {
            return scripts_->callBehavior(call, entity, hook, flag);
        };
    }
    host.options.auto_hide = persisted_.allow_auto_hide;
    host.options.animate = persisted_.allow_animation;
    host.options.show_hidden = show_hidden_;
    host.options.clipboard = static_cast<behaviors::ClipboardConsent>(persisted_.clipboard_consent);
    return host;
}

behaviors::Frame Session::behaviorFrame() {
    return behaviors::Frame{&mumble_state_, utcNow(), ImGui::GetTime(), &festivals_.active(), &account_progress_.progress()};
}

void Session::drawCatalog() {
    if (!windows_.catalog) return;
    const ui::pack_catalog::Listing listing = ui::pack_catalog::list(library_, pack_folder_.local());
    bool retry = false;
    for (const ui::pack_catalog::Action& action :
         ui::pack_catalog::draw(&catalog_view_, listing.rows, listing.status, listing.failed, &retry, &windows_.catalog)) {
        const ui::pack_catalog::Row* row = action.row;
        switch (action.kind) {
        case ui::pack_catalog::Action::Kind::Download: library_.downloads().start(*row->package, pack_folder_.folder()); break;
        case ui::pack_catalog::Action::Kind::Delete:
            if (row->local) pack_folder_.remove(&installed_, row->local->path);
            break;
        case ui::pack_catalog::Action::Kind::KeepUpdated: library_.setKeepUpdated(row->package->name, action.value); break;
        case ui::pack_catalog::Action::Kind::OpenPage: desktop::openInBrowser(row->package->info); break;
        case ui::pack_catalog::Action::Kind::OpenFolder: desktop::openFolder(pack_folder_.folder()); break;
        }
    }
    if (retry) library_.requestList();
}

// Opens the import, reading again what Blish HUD has now: the window opens once the worker has
// read it (drawImport).
void Session::openImport() {
    if (importer_.running()) {
        windows_.import = true;
    } else {
        importer_.findOnWorker(registry::blishMarkersFolder(), &jobs_);
    }
}

void Session::drawImport() {
    if (importer_.takeFound()) {
        import_choices_ = {};
        windows_.import = true;
    }

    // Once the copy is done: Pathing's states into ours, written at once, and the new packs loaded.
    if (const std::optional<blish_import::Importer::Done> done = importer_.finish()) {
        for (const auto& [id, on] : done->states.categories) states_.categories.remember(id, on);
        for (const auto& [key, value] : done->states.storage) states_.storage.upsert(key, value);
        states_.timers.merge(done->timers);
        saveStates();
        for (const std::string& error : done->copied.errors)
            api_->Log(ELogLevel_WARNING, ADDON_NAME, ("Blish HUD import: " + error).c_str());
        pack_folder_.rescan();
    }

    const bool was_open = windows_.import;
    const ui::import_window::Answer answer =
        ui::import_window::draw(importer_.found(), &import_choices_, importer_.status(), importer_.running(), &windows_.import);
    if (answer == ui::import_window::Answer::Import) {
        const std::vector<std::filesystem::path>& found = importer_.found().packs;
        std::vector<std::filesystem::path> chosen;
        for (size_t i = 0; i < found.size(); ++i) {
            if (i < import_choices_.packs.size() && import_choices_.packs[i]) chosen.push_back(found[i]);
        }
        importer_.start(std::move(chosen), pack_folder_.folder());
    }
    // Answered, or closed: it does not open by itself again.
    if (!persisted_.import_offered && (answer != ui::import_window::Answer::None || (was_open && !windows_.import))) {
        persisted_.import_offered = true;
        saveSettings();
    }
}

void Session::drawItems(bool separator) {
    if (live_.map_id != menu_.show_all_map) {
        menu_.show_all.clear();
        menu_.show_all_map = live_.map_id;
    }
    // Walked while the menu is open only, and a map holds thousands at most.
    const std::vector<char> on_map = persisted_.smart_categories ? runtime::nodesOnMap(installed_.tree, live_) : std::vector<char>{};

    ui::main_menu::Context context;
    context.tree = &installed_.tree;
    context.states = &states_.categories;
    context.scripts = persisted_.run_scripts ? scripts_.get() : nullptr;
    context.manage_packs = [this] { windows_.catalog = true; };
    context.open_options = [this] { windows_.options = true; };
    if (editor.start) context.open_editor = editor.start;
    context.on_map = persisted_.smart_categories ? &on_map : nullptr;
    context.show_all = &menu_.show_all;
    context.account = &account_progress_.progress();
    context.auto_hide = persisted_.allow_auto_hide;
    context.achievements = &achievement_details_;
    context.achievement_tooltips = persisted_.achievement_tooltips;
    context.separator = separator;
    // Our font for its icons, pushed here since Nexus draws its menu and our icon's outside our render.
    ui::fonts::push();
    // A player's own toggle is written at once, not with the scripts' changes.
    if (ui::main_menu::drawItems(context)) saveStates();
    ui::fonts::pop();
}

// Hands the fonts the game's device as soon as Nexus has a swap chain to take it from.
void Session::takeFontDevice() {
    if (font_device_ || !api_->SwapChain) return;
    auto* swap_chain = static_cast<IDXGISwapChain*>(api_->SwapChain);
    if (SUCCEEDED(swap_chain->GetDevice(IID_PPV_ARGS(&font_device_)))) fonts_.setDevice(font_device_.Get());
}

// The menu for a left click on our icon.
void Session::drawMenuPopup() {
    if (menu_requested.exchange(false)) {
        ui::main_menu::placeUnderIcon();
        ImGui::OpenPopup(kMenuPopup);
    }
    if (ImGui::BeginPopup(kMenuPopup)) {
        drawItems(true);
        ImGui::EndPopup();
    }
}

// Screen pixels per UI pixel, as the game draws its minimap and map: the interface size, and
// the DPI scale the options say to take.
float Session::uiScale() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    float dpi_scale = 1.0f;
    switch (static_cast<settings::DpiScaling>(persisted_.map_dpi_scaling)) {
    case settings::DpiScaling::None: break;
    case settings::DpiScaling::Nexus:
        if (nexus_link_ && nexus_link_->Scaling > 0.0f) return nexus_link_->Scaling;
        break;
    case settings::DpiScaling::System: {
        // Windows' display scale. The system's, not the game window's monitor's as Nexus and Blish
        // HUD ask: the two differ only across monitors of different scales, which Custom is there for.
        const UINT dpi = GetDpiForSystem();
        dpi_scale = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
        break;
    }
    case settings::DpiScaling::Custom: dpi_scale = persisted_.map_dpi_custom; break;
    }
    return map::uiScale(mumble_state_.ui_size, dpi_scale, glm::vec2(display.x, display.y));
}

// Nexus's IsGameplay, which is off on a loading screen: MumbleLink is not written there, and
// keeps the last frame before it. Taken as on without the link, rather than never drawing.
bool Session::inGameplay() const {
    return !nexus_link_ || nexus_link_->IsGameplay;
}

WorldOptions Session::worldOptions() {
    WorldOptions options;
    options.enabled = persisted_.world_pathables;
    options.max_opacity = persisted_.max_opacity;
    options.view_distance = persisted_.view_distance;
    options.max_trail_speed = persisted_.max_trail_speed;
    options.marker_scale = persisted_.marker_scale;
    options.combat_fade = persisted_.combat_fade;
    options.in_combat = mumble_state_.is_in_combat;
    options.in_gameplay = inGameplay();
    options.show_hidden = show_hidden_;
    options.occlusion = static_cast<Occlusion>(persisted_.occlusion);
    return options;
}

// The above or below image the maps draw on an icon's corner, which the options page shows too.
// Nexus returns the texture it already made for an identifier, so asking every frame decodes
// each once; null until it is ready.
ImTextureID Session::aboveBelowImage(bool above) {
    const char* id = above ? "TEX_MarkerPackEditor_Above" : "TEX_MarkerPackEditor_Below";
    const unsigned char* bytes = above ? ui::map_icon_data::kAbove : ui::map_icon_data::kBelow;
    const size_t size = above ? sizeof(ui::map_icon_data::kAbove) : sizeof(ui::map_icon_data::kBelow);
    const Texture* texture = api_->Textures.GetOrCreateFromMemory(id, const_cast<unsigned char*>(bytes), size);
    return texture ? static_cast<ImTextureID>(texture->Resource) : nullptr;
}

// What the maps are handed at Present: the live maps, as the world scene is, and the options.
Maps::Frame Session::mapFrame(const TextureLookup& texture) {
    Maps::Frame frame;
    frame.mumble = &mumble_state_;
    frame.in_gameplay = inGameplay();
    frame.world_drawn = stats_.last_drawn.world;
    if (good_render_ && good_render_->service() == good_render_sink::Service::Serving) frame.game_map = good_render_sink::frameMap();
    frame.seconds = ImGui::GetTime();
    frame.live = &live_;
    frame.visible = &visible_.nodes;
    frame.texture = texture;
    frame.edited = edited_.live;
    frame.edited_texture = edited_.texture;

    map_scene::Options& options = frame.options;
    options.enabled = persisted_.map_pathables;
    options.map_markers = static_cast<map_scene::Visibility>(persisted_.map_marker_visibility);
    options.map_trails = static_cast<map_scene::Visibility>(persisted_.map_trail_visibility);
    options.minimap_markers = static_cast<map_scene::Visibility>(persisted_.minimap_marker_visibility);
    options.minimap_trails = static_cast<map_scene::Visibility>(persisted_.minimap_trail_visibility);
    options.map_opacity = persisted_.map_opacity;
    options.minimap_opacity = persisted_.minimap_opacity;
    options.above_below = persisted_.map_above_below;
    options.fade_far_trails = persisted_.map_fade_far_trails;
    options.trail_width = persisted_.map_trail_width;
    options.show_hidden = show_hidden_;

    frame.ui_scale = uiScale();
    frame.above = aboveBelowImage(true);
    frame.below = aboveBelowImage(false);
    return frame;
}

// The map's tooltip and menu, over what was published this frame. None on the compass while the
// full map is up or MumbleLink's fields are still its, nor on the full map while it zooms.
void Session::drawMapMenu() {
    // Taken every frame, so a click no menu could open on is not kept for a later one.
    const bool clicked = right_clicked.exchange(false);
    const std::shared_ptr<const map_scene::MapScene> scene = map_scene::published(mumble_state_.is_map_open);
    if (!scene || scene->full_map != mumble_state_.is_map_open || scene->zoom != 1.0f || scene->keep_compass) return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const std::optional<map::View> view = map::view(mumble_state_, glm::vec2(display.x, display.y), scene->ui_scale);
    if (!view) return;

    ui::map_menu::Context context;
    context.scene = scene.get();
    context.view = &*view;
    context.live = {&live_};
    if (edited_.live) context.live.push_back(edited_.live); // the map scene's layers, in order
    context.tree = &installed_.tree;
    context.states = &states_.categories;
    context.player = mumble_state_.player_position;
    context.tooltips = persisted_.map_tooltips;
    context.right_clicked = clicked;
    context.copy = [this](const std::string& text) { setClipboard(text, "Copied: " + text); };
    if (ui::map_menu::draw(context, &map_menu_view_)) saveStates();
}

// The frame that ended with the Present beginning at `started`, logged when our work in it took
// long. Loading screens and the game in the background included: what we do there is ours all
// the same.
void Session::logSlowFrame(std::chrono::steady_clock::time_point started) {
    size_t skipped = 0;
    if (!persisted_.log_slow_frames || !stats_.slow_frames.take(stats_.steps.total(), persisted_.slow_frame_ms, started, &skipped)) return;
    frame_log::Frame frame;
    frame.interval = std::chrono::duration<double, std::milli>(started - stats_.last_present).count();
    frame.steps = stats_.steps;
    double slowest = 0.0;
    for (size_t pack = 0; pack < installed_.packs.size(); ++pack) {
        if (const double seconds = scripts_->frameSeconds(pack); seconds > slowest) {
            slowest = seconds;
            frame.slowest_scripts = util::pathText(installed_.packs[pack]->loaded->path.filename());
        }
    }
    frame.map_id = live_.map_id;
    if (const std::shared_ptr<const Scene> scene = published()) frame.quads = scene->items.size();
    frame.lua_bytes = scripts_->heapBytes();
    frame.jobs = jobs_.unfinished();
    frame.uploads = texture_uploads.images();
    frame.upload_pixels = texture_uploads.pixels();
    frame.loading_packs = pack_folder_.loading();
    frame.skipped = skipped;
    api_->Log(ELogLevel_INFO, ADDON_NAME, frame_log::line(frame).c_str());
}

// Everything is decided here, at Present, before the next frame is drawn: GoodRender's
// moments only draw what this publishes, because a moment runs inside the game's frame and
// anything slow there stalls the game.
void Session::preRender() {
    const auto started = std::chrono::steady_clock::now();
    stats_.steps.moments = good_render_sink::takeMomentMilliseconds();
    if (stats_.last_present != std::chrono::steady_clock::time_point{}) logSlowFrame(started);
    stats_.last_present = started;
    stats_.steps = {};

    frame_log::Laps laps;
    texture_uploads.nextFrame();
    pack_folder_.update(&installed_, &library_.downloads());
    library_.updateStale(pack_folder_.local(), pack_folder_.folder());
    account_progress_.update(std::chrono::steady_clock::now(), utcNow());
    achievement_details_.update(std::chrono::steady_clock::now());
    laps.lap(&stats_.steps.packs);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link_, rtapi::data(), glm::vec2(display.x, display.y));

    refreshLiveMap(camera.valid ? static_cast<int>(camera.map_id) : live_.map_id);
    laps.lap(&stats_.steps.live_map);
    mumble_state_ = mumbleState(mumble_link_);
    runScripts();
    laps.lap(&stats_.steps.scripts);

    // Behaviors see what scripts did this frame, and the scene what both did.
    visible_.update(installed_, states_.categories);
    laps.lap(&stats_.steps.visibility);

    // One frame of behaviors, after the scripts have ticked.
    const behaviors::Host host = behaviorHost();
    const behaviors::Frame frame = behaviorFrame();
    behaviors::update(&live_, visible_.nodes, host, frame);
    laps.lap(&stats_.steps.behaviors);

    // Not while typing — in chat, or in one of our own text fields.
    const bool typing = mumble_state_.is_text_input_focused || ImGui::GetIO().WantTextInput;
    if (interact_pressed.exchange(false) && !typing) behaviors::interact(&live_, host, frame);
    // UiStates clears the info on Escape.
    if (escape_released.exchange(false)) screen_.info.clear();
    laps.lap(&stats_.steps.interaction);

    visible_.update(installed_, states_.categories);
    runtime::startFadeIns(&live_, visible_.nodes, ImGui::GetTime());
    laps.lap(&stats_.steps.visibility);
    if (states_.due()) saveStates();
    laps.lap(&stats_.steps.saves);
    edited_.live = editor.edited_live ? editor.edited_live(live_.map_id, &edited_.texture) : nullptr;
    if (edited_.live) runtime::startFadeIns(edited_.live, kNoNodes, ImGui::GetTime());
    laps.lap(&stats_.steps.editor);

    const TextureLookup texture = [this](size_t pack, const std::string& path) { return runtime::packImage(installed_, pack, path); };

    std::vector<SceneLayer> layers{SceneLayer{&live_, &visible_.nodes, texture, false}};
    if (edited_.live) layers.push_back(SceneLayer{edited_.live, &kNoNodes, edited_.texture, true});

    const std::shared_ptr<Scene> next = scenes_.take();
    buildScene(layers, camera, static_cast<float>(ImGui::GetTime()), worldOptions(), next.get());
    scenes_.keep(next);
    publish(next);
    stats_.scene_camera = camera.source;
    laps.lap(&stats_.steps.scene);

    maps_.build(mapFrame(texture));
    laps.lap(&stats_.steps.maps);
    stats_.present_times.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
}

// The overlay draws what GoodRender did not reach: everything with no service, or a loading
// screen or the full map; and the full map always. Behind our own windows and the editor's, but
// over the game.
void Session::drawWorld() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link_, rtapi::data(), glm::vec2(display.x, display.y));
    const std::shared_ptr<const Scene> scene = published();

    const good_render_sink::Drawn drawn = good_render_sink::takeDrawn();
    stats_.last_drawn = drawn;
    // Not with the map up: this is over the game's UI, so over the map. The scene is still built
    // then, for GoodRender, which draws it under the map.
    if (scene && camera.valid && !camera.map_open && !(drawn.occluded && drawn.over)) {
        overlay::drawScene(ImGui::GetBackgroundDrawList(), camera, *scene, !drawn.occluded, !drawn.over);
    }
    // Not the minimap with the full map up, which covers it.
    if (!drawn.map && !camera.map_open) {
        map_scene::drawPublished(ImGui::GetBackgroundDrawList(), mumble_link_, glm::vec2(display.x, display.y), false);
    }
    map_scene::drawPublished(ImGui::GetBackgroundDrawList(), mumble_link_, glm::vec2(display.x, display.y), true);
}

// Our windows and ImGui's: what is drawn with our font pushed.
void Session::drawFrame() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const Camera camera = gameCamera(mumble_link_, rtapi::data(), glm::vec2(display.x, display.y));

    drawMapMenu();

    drawMenuPopup();
    // Before the windows it can open, so they show in the frame it was clicked in.
    drawOptionsWindow();
    ui::script_console::draw(*scripts_, installed_, inFolder(ui::script_console::SAVE_FILE_NAME), jobs_, &windows_.console);
    drawCatalog();
    drawImport();
    // Registers a key the check found fit. From the frame rather than the options page, so a key
    // is kept even if the page closed while the check ran.
    if (std::optional<std::string> key = registration_.takeAccepted()) {
        persisted_.api_key = std::move(*key);
        account_progress_.setKey(persisted_.api_key, utcNow());
        registration_.saving(saveSettings());
    }
    // Either may have removed a pack, and the prompt below calls into the live map.
    refreshLiveMap(live_.map_id);

    if (camera.valid && !camera.map_open &&
        ui::interaction::draw(
            screen_,
            static_cast<settings::InfoDisplay>(persisted_.info_display),
            interact_key.name(),
            persisted_.show_interact_prompt,
            &achievement_details_)) {
        behaviors::interactWith(&live_, screen_.prompt->marker, behaviorHost(), behaviorFrame());
    }

    if (all_markers_requested.exchange(false)) {
        states_.categories.set(runtime::ALL_MARKERS, !states_.categories.enabled(runtime::ALL_MARKERS, true), true);
        saveStates();
    }
}

void Session::render() {
    frame_log::Laps laps;
    takeFontDevice();
    // The world and the map before our font is pushed: see fonts::push.
    drawWorld();
    ui::fonts::push();
    drawFrame();
    ui::fonts::pop();
    // Over the world as well as in its windows, so it pushes the font around its windows itself.
    if (editor.render) editor.render();
    laps.lap(&stats_.steps.render);
}

// Puts the menu in Nexus's menu when `in_nexus`, and behind our icon in Nexus's bar when `icon`,
// taking it out of either otherwise. Safe from the options page, Nexus's or our window's: Nexus
// holds its quick access lock only while drawing the bar.
void Session::placeMenu(bool in_nexus, bool icon) {
    if (in_nexus != menu_.in_nexus) {
        if (in_nexus) {
            api_->QuickAccess.AddContextMenu(QUICKACCESS_ID, nullptr, drawNexusMenuItems);
        } else {
            api_->QuickAccess.RemoveContextMenu(QUICKACCESS_ID);
        }
        menu_.in_nexus = in_nexus;
    }
    if (icon != menu_.icon) {
        if (icon) {
            api_->QuickAccess.Add(SHORTCUT_ID, ICON_TEXTURE_ID, ICON_HOVER_TEXTURE_ID, KEYBIND_MENU_ID, ADDON_NAME);
            api_->QuickAccess.AddContextMenu(SHORTCUT_MENU_ID, SHORTCUT_ID, drawMenuItems);
        } else {
            api_->QuickAccess.RemoveContextMenu(SHORTCUT_MENU_ID);
            api_->QuickAccess.Remove(SHORTCUT_ID);
        }
        menu_.icon = icon;
    }
}

// What the Development section shows. The images' counts walk every pack's cache, so this is
// gathered only while the section is open.
ui::diagnostics::Figures Session::diagnosticFigures() {
    ui::diagnostics::Figures figures;
    figures.service = good_render_ ? good_render_->service() : good_render_sink::Service::None;
    figures.drawn = stats_.last_drawn;
    figures.camera = stats_.scene_camera;
    figures.full_map_up = mumble_state_.is_map_open;
    figures.full_map_from_game = maps_.fullMapFromGame();
    if (const std::shared_ptr<const Scene> scene = published()) figures.scene_items = scene->items.size();
    figures.present_ms = stats_.present_times.last();
    figures.present_peak_ms = stats_.present_times.peak();
    figures.map_id = live_.map_id;
    figures.map_described = maps_.find(live_.map_id) != nullptr;
    figures.in_gameplay = inGameplay();
    figures.markers = live_.markers.size();
    figures.trails = live_.trails.size();
    figures.packs_found = pack_folder_.found();
    figures.loading = pack_folder_.loading();
    for (const std::unique_ptr<runtime::InstalledPack>& pack : installed_.packs) {
        figures.packs.push_back(
            ui::diagnostics::PackFigures{util::pathText(pack->loaded->path.filename()), pack->loaded->error, pack->textures.counts()});
    }
    figures.uploads_images = texture_uploads.images();
    figures.uploads_pixels = texture_uploads.pixels();
    figures.jobs = jobs_.unfinished();
    figures.lua_bytes = scripts_->heapBytes();
    return figures;
}

// The page's sections, the first three open until the player closes them: what a new player
// sets up, the API key among it, before what fine-tunes the drawing.
void Session::drawOptionsPage() {
    if (ImGui::CollapsingHeader("Interface", ImGuiTreeNodeFlags_DefaultOpen) && ui::options::drawInterface(&persisted_)) {
        placeMenu(persisted_.menu_in_nexus, persisted_.menu_icon);
        saveSettings();
    }

    if (ImGui::CollapsingHeader("Marker Packs", ImGuiTreeNodeFlags_DefaultOpen)) {
        switch (ui::options::drawPacks(&persisted_, pack_folder_.status(installed_))) {
        case ui::options::PacksAction::None: break;
        case ui::options::PacksAction::ScriptsToggled:
            saveSettings();
            // Off throws every handler away, and what scripts did to the map with them; on starts
            // them again from pack.lua.
            scripts_->reset();
            replaceLiveMap(live_.map_id);
            break;
        case ui::options::PacksAction::Manage: windows_.catalog = true; break;
        case ui::options::PacksAction::Reload: pack_folder_.reload(&installed_); break;
        case ui::options::PacksAction::Import: openImport(); break;
        }
    }

    if (ImGui::CollapsingHeader("GW2 API Key", ImGuiTreeNodeFlags_DefaultOpen)) {
        switch (ui::options::drawApiKey(persisted_.api_key, registration_.field(), registration_.checking(), registration_.status())) {
        case ui::options::KeyAction::None: break;
        case ui::options::KeyAction::OpenApplications: desktop::openInBrowser(api_key::APPLICATIONS_URL); break;
        case ui::options::KeyAction::Register: registration_.submit(); break;
        case ui::options::KeyAction::Remove:
            persisted_.api_key.clear();
            saveSettings();
            // The progress kept was the key's: it goes with it.
            account_progress_.setKey(persisted_.api_key, utcNow());
            account_progress_.clearCache();
            registration_.setStatus({});
            break;
        case ui::options::KeyAction::ClearCache:
            account_progress_.clearCache();
            achievement_details_.clear();
            registration_.setStatus("Saved API data cleared.");
            break;
        }
    }

    if (ImGui::CollapsingHeader("Markers")) {
        if (ui::options::drawPack(&persisted_, &show_hidden_)) saveSettings();
    }
    if (ImGui::CollapsingHeader("Map and Minimap") && ui::options::drawMaps(&persisted_, aboveBelowImage(true), aboveBelowImage(false))) {
        saveSettings();
    }

    // For pack authors and bug reports.
    if (ImGui::CollapsingHeader("Development")) {
        if (ImGui::Button(ui::fonts::label(ICON_FA_TERMINAL, "Open Lua Script Console").c_str())) windows_.console = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Messages and errors from pack scripts.");
        ImGui::SameLine();
        ui::diagnostics::draw(ui::diagnostics::describe(diagnosticFigures()), [this] {
            if (ui::options::drawSlowFrameLog(&persisted_)) saveSettings();
        });
    }
}

void Session::drawOptions() {
    frame_log::Laps laps;
    takeFontDevice();
    ui::fonts::push();
    drawOptionsPage();
    ui::fonts::pop();
    laps.lap(&stats_.steps.render);
}

// The same page in a window of ours, from the menu's Options; our render has the fonts pushed
// already. Not titled "Options" alone: that is Nexus's window, in the same ImGui context.
void Session::drawOptionsWindow() {
    if (!windows_.options) return;
    ImGui::SetNextWindowSize(ImVec2(480.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Markus Options", &windows_.options)) drawOptionsPage();
    ImGui::End();
}

} // namespace

void load(AddonAPI* api, Mumble::Data* mumble_link) {
    session = std::make_unique<Session>(api, mumble_link);
    // Only once `session` is set: Nexus may call back as soon as a callback is registered.
    session->attach();
}

void unload() {
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
