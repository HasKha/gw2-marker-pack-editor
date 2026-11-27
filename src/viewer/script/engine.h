#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "viewer/behaviors/attributes.h"
#include "viewer/gw2_api/account.h"
#include "viewer/script/console.h"

namespace viewer::runtime {
struct Installed;
struct LiveMap;
} // namespace viewer::runtime

namespace viewer::store {
class CategoryStates;
class KeyValues;
} // namespace viewer::store

// Packs' Lua scripts, run the way Pathing's ScriptEngine runs them: one state for every pack,
// thrown away and built again on every map, each pack's pack.lua run into it, and OnTick
// handlers called every frame after. See docs/viewer.md#lua.
//
// sol2 stays behind this header: whoever runs scripts sees none of it.
namespace viewer {
struct MumbleState;
}

namespace viewer::script {

// The Pathing version scripts are told they run under, from the Pathing checkout the API was
// read from (its manifest.json). Hero's pack refuses to load below 1.9.0, and its fractal
// dailies below 1.10.5.
constexpr const char* kPathingVersion = "1.14.3";

// What scripts reach outside themselves. Owned by whoever owns the engine, and outlives it.
struct Host {
    runtime::Installed* installed = nullptr;
    runtime::LiveMap* live = nullptr;
    store::CategoryStates* category_states = nullptr;
    store::KeyValues* storage = nullptr;
    const MumbleState* mumble = nullptr;
    const account::Progress* account = nullptr; // what the API says of the player's account; null for nothing

    // User.SetClipboard: copies `text`, telling the player with `message`. False when the
    // player does not allow packs the clipboard. Null refuses.
    std::function<bool(const std::string& text, const std::string& message)> set_clipboard;

    // Debug.ShowMessage: a notification on screen. Null leaves it in the console only.
    std::function<void(const std::string& text)> show_message;

    // How long one call into Lua may run before it is stopped with an error: far past what a
    // working script needs — Hero's heaviest file loads in milliseconds — and short enough
    // that a script stuck in a loop costs the player a hitch rather than the game.
    std::chrono::milliseconds call_budget{2000};
};

constexpr size_t kNoMenu = static_cast<size_t>(-1);

// An entry scripts added under the "Scripts" menu: Pathing's Lib/Menu.cs.
struct MenuNode {
    std::string name;
    std::string tooltip;
    bool can_check = false;
    bool checked = false;
    size_t parent = kNoMenu;
    std::vector<size_t> children;
    bool removed = false; // by Menu:Remove; kept so indices stay valid
};

class Engine
{
public:
    explicit Engine(Host host);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // A fresh state, with no script loaded and no handler registered: what Pathing does on
    // every map (ScriptEngine.Reset). The console and the census are kept.
    void reset();

    // Runs pack.lua of every installed pack, in install order, as PackInitiator does once a
    // map's packs are in. A pack without one is skipped quietly.
    void loadPacks();

    // Every OnTick handler, with a GameTime of `total_seconds` since the game started and
    // `elapsed_seconds` since the last tick. A handler that errors is dropped, as Pathing
    // drops it, and the rest still run.
    void tick(double total_seconds, double elapsed_seconds);

    // Runs `source` as if it were `file` of pack `pack`, `Pack` and all: the console's eval,
    // and how tests run a script without a pack to hold it. False when it errors, which is
    // reported like any other script error.
    bool run(size_t pack, std::string_view file, std::string_view source);

    // Before the state, whose scripts write to it.
    Console console;
    const Census& census() const { return script_census; }

    // Menu entries scripts made; [0] is the "Scripts" root, which is never shown alone.
    const std::vector<MenuNode>& menus() const;
    // As Pathing's menu item does: flips `checked` when it can be checked, then calls the
    // entry's handler with the entry.
    void clickMenu(size_t index);

    const std::map<std::string, std::string>& watches() const; // Debug:Watch, as text
    const std::vector<std::string>& info() const; // User.ShowInfo

    // A script-* behavior's function, called as Pathing's ScriptEngine.CallFunction calls it:
    // a global looked up ignoring case, handed `entity`, then what `hook` adds — `flag` for
    // Focus and Trigger, the last tick's GameTime for Tick — then the attribute's arguments.
    // What it returned first, as Convert.ToBoolean reads it; nothing when it returned
    // nothing, does not exist, or failed, which goes to the console.
    std::optional<bool> callBehavior(const behaviors::ScriptCall& call, behaviors::Entity entity, behaviors::Hook hook, bool flag);

    // Seconds pack `pack`'s scripts took per frame, over the last ten frames, as Pathing
    // averages its FrameExecutionTime — but per pack, so that a slow script is named.
    double frameSeconds(size_t pack) const;

    // The Lua heap, garbage not yet collected included.
    size_t heapBytes() const;

private:
    struct State;

    Host host;
    Census script_census;
    std::unique_ptr<State> state;
    std::vector<std::vector<double>> frame_history; // per pack, the last ten frames
};

} // namespace viewer::script
