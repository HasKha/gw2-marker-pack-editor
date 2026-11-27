#pragma once

#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "util/job_queue.h"
#include "util/kept_file.h"
#include "viewer/behaviors/festivals.h"
#include "viewer/behaviors/states.h"
#include "viewer/behaviors/update.h"
#include "viewer/frame_log.h"
#include "viewer/game/camera.h"
#include "viewer/game/mumble.h"
#include "viewer/gw2_api/account.h"
#include "viewer/gw2_api/achievements.h"
#include "viewer/gw2_api/api_key.h"
#include "viewer/live/runtime.h"
#include "viewer/maps/maps.h"
#include "viewer/packs/blish_import.h"
#include "viewer/packs/library.h"
#include "viewer/packs/pack_folder.h"
#include "viewer/render/good_render.h"
#include "viewer/render/recycled.h"
#include "viewer/render/scene.h"
#include "viewer/render/textures.h"
#include "viewer/script/engine.h"
#include "viewer/settings.h"
#include "viewer/store.h"
#include "viewer/ui/diagnostics.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/import_window.h"
#include "viewer/ui/map_menu.h"
#include "viewer/ui/pack_catalog.h"
#include "viewer/utc.h"
#include "viewer/viewer.h"

// The viewer's session, for viewer.cpp and the options page alone: nothing else includes it.
namespace viewer {

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

// The pack open in the editor this frame, as EditorHooks::edited_live hands it over: drawn in the
// world and on the maps with the installed packs. Its markers belong to no node of our tree, which
// the editor's own decides for, so nothing of ours turns them off.
struct EditedPack {
    runtime::LiveMap* live = nullptr;
    TextureLookup texture;
};

// ── The session ─────────────────────────────────────────────────────────────────

// The viewer from load to unload. Nexus calls in through viewer.cpp's functions, which reach the
// one running session. Its methods are viewer.cpp's, but for the options page's (options_page.cpp).
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
    // The other way, before the session goes: Nexus's render callbacks, then the service. Once
    // unsubscribe returns no moment can be reading the scene.
    void detach();

    void preRender();
    void render();
    void drawOptions();
    // The menu's items, wherever it is shown: Nexus's menu, our icon's, or our own popup.
    void drawItems(bool separator);

private:
    void showInNexusMenu(bool show);
    void showIcon(bool show);
    std::filesystem::path inFolder(const std::filesystem::path& name) const;
    std::future<bool> saveSettings();
    void saveStates();
    bool setClipboard(const std::string& text, const std::string& message);
    void replaceLiveMap(int map_id);
    void refreshLiveMap(int map_id);
    behaviors::Host behaviorHost();
    behaviors::Frame behaviorFrame();
    void takeFontDevice();
    float uiScale();
    bool inGameplay() const;
    ImTextureID aboveBelowImage(bool above);
    WorldOptions worldOptions();
    Maps::Frame mapFrame(const TextureLookup& texture);
    void logSlowFrame(std::chrono::steady_clock::time_point started);
    void drawMapMenu();
    void drawCatalog();
    void drawImport();
    // options_page.cpp: the whole page, each control acting on the session as it is used.
    void drawOptionsPage();

    AddonAPI* api = nullptr;
    Mumble::Data* mumble_link = nullptr;
    // Read for its Scaling and IsGameplay only: its Width and Height go stale (see gameCamera).
    const NexusLinkData* nexus_link = nullptr;
    std::filesystem::path folder;
    // The game's device, which our font's atlas is uploaded to. Taken from Nexus's swap chain once
    // there is one.
    Microsoft::WRL::ComPtr<ID3D11Device> font_device;
    // After the device, so it goes first: its atlas was made on it.
    ui::fonts::Fonts fonts;
    // Subscribed from attach to detach.
    std::optional<good_render_sink::Subscription> good_render;

    // Every read and write of a file after load: the frame hands them over and takes the answer
    // on a later frame, never waiting for the disk itself.
    util::JobQueue jobs;

    // What the options page edits and saves, against what this session loaded with.
    settings::Settings persisted;
    KeptStates states;

    // What the GW2 API says: the festivals on; what the registered key says of the account
    // (achievements and this week's raids); what achievements are, for their tooltips; and the
    // key in the options.
    behaviors::FestivalFetcher festivals;
    account::Fetcher account_progress;
    achievements::Catalog achievement_details;
    api_key::Registration registration;

    runtime::Installed installed;
    PackFolder pack_folder;
    runtime::LiveMap live_map;
    VisibleNodes visible;
    MumbleState mumble_state;
    // What behaviors put on screen.
    behaviors::Screen screen;
    // Pathing's "Temporarily Show Ghost Markers", which it turns off at every start: so a session
    // setting, never saved.
    bool show_hidden = false;

    // Scripts: one engine for every pack, built again with every live map, as Pathing's is with
    // every map. `scripts_stale` holds from the moment the live map is replaced until the engine
    // has been reset onto it — a script's handles are indices into the live map, so it must not
    // tick in between. After everything its host points at.
    std::unique_ptr<script::Engine> scripts;
    bool scripts_stale = true;

    catalog::Library library;
    // The Blish HUD import, and what the player chose to bring.
    blish_import::Importer importer;
    ui::import_window::Choices import_choices;

    Recycled<Scene> scenes;
    Maps maps;
    EditedPack edited;
    FrameStats stats;
    Windows windows;
    ui::pack_catalog::View catalog_view;
    ui::map_menu::View map_menu_view;

    // Where Nexus shows the menu now: see showInNexusMenu and showIcon.
    bool in_nexus_menu = false;
    bool icon_shown = false;
    // Pathing's smart categories: the menus whose hidden categories were asked for, until the
    // map changes.
    std::set<size_t> show_all_menus;
    int show_all_map = 0;
};

} // namespace viewer
