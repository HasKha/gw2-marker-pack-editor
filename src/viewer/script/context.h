#pragma once

#include <sol/sol.hpp>

#include <chrono>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "viewer/script/console.h"
#include "viewer/script/engine.h"

// The engine's own state, as the bindings see it: one per Lua state, reached from any
// lua_State through script::context(). Built with the state and thrown away with it.
namespace viewer::script {

struct Context {
    Context(const Host& host, Console& console, Census& census)
        : host(host)
        , console(console)
        , census(census) {}

    const Host& host;
    Console& console;
    Census& census;

    // The globals every chunk runs in: Pathing's PathingGlobal.
    sol::environment env;

    // Every chunk loaded, by its source name without the '@', to the pack it came from.
    std::unordered_map<std::string, size_t> pack_of_source;

    // Pathing's Require cache: a chunk that loaded is never run again for the same pack.
    std::map<std::pair<size_t, std::string>, sol::object> required;

    struct TickListener {
        sol::protected_function function;
        size_t pack = 0;
    };
    std::vector<TickListener> tick_listeners;

    std::vector<MenuNode> menus;
    std::vector<sol::protected_function> menu_clicks; // one per menus entry; may be empty

    std::map<std::string, std::string> watches;
    std::vector<std::string> info;

    // One userdata per object, so that a script keying a table with a marker, or comparing
    // two, finds the same one — as NeoLua does, handing out the .NET object itself.
    std::unordered_map<size_t, sol::object> marker_objects;
    std::unordered_map<size_t, sol::object> trail_objects;
    std::unordered_map<size_t, sol::object> category_objects;
    std::unordered_map<size_t, sol::object> menu_objects;
    std::unordered_map<std::string, sol::object> guid_objects; // by the 16 raw bytes

    bool warned_packs = false; // the deprecated `Packs` global warns once

    // math.random's, as NeoLua's System.Random: seeded at random, and again by randomseed.
    std::mt19937 random{std::random_device{}()};

    std::vector<double> frame_seconds; // per pack, this frame so far

    // When the call into Lua now running is stopped: a script stuck in a loop would otherwise
    // stall the game's frame for good. See Host::call_budget.
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();

    // The pack a call was made from: the chunk of the innermost Lua function.
    size_t packOf(const std::string& source) const {
        const auto it = pack_of_source.find(source);
        return it == pack_of_source.end() ? kNoPack : it->second;
    }
    static constexpr size_t kNoPack = static_cast<size_t>(-1);
};

// Pack:Require, and the global require: `name` of pack `pack`, ".lua" added when it lacks
// one, loaded and run the first time and handed back as the chunk every time after, as
// PathingGlobal.Require does. Nil when it is missing or fails.
sol::object requireScript(sol::state_view lua, size_t pack, const std::string& name);

} // namespace viewer::script
