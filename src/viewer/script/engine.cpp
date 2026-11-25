#include "viewer/script/engine.h"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <unordered_map>
#include <variant>

#include "pack/source.h"
#include "util/path_text.h"
#include "viewer/registry.h"
#include "viewer/runtime.h"
#include "viewer/script/api.h"
#include "viewer/script/context.h"

namespace viewer::script {

namespace {

using Clock = std::chrono::steady_clock;

// Instructions between looks at the clock: often enough to stop within the budget, rarely
// enough to cost nothing measurable.
constexpr int kHookInterval = 100000;

// How many frames frameSeconds averages over, as Pathing's RingBuffer of ten.
constexpr size_t kFrameHistory = 10;

void budgetHook(lua_State* L, lua_Debug*) {
    if (Clock::now() > context(L).deadline) {
        luaL_error(L, "script stopped: still running after %d ms", static_cast<int>(context(L).host.call_budget.count()));
    }
}

// "system" for a pack that is not installed: a handler may outlive its pack by a frame.
std::string packLabel(const Context& ctx, size_t pack) {
    const auto& packs = ctx.host.installed->packs;
    return pack < packs.size() ? util::pathText(packs[pack]->loaded->path.filename()) : std::string("system");
}

// The text of `pack`'s script at the normalized `path`, as the pack was read; null for a file
// it does not have.
const std::string* packScript(const Context& ctx, size_t pack, const std::string& path) {
    const auto& packs = ctx.host.installed->packs;
    if (pack >= packs.size()) return nullptr;
    const auto& scripts = packs[pack]->loaded->scripts;
    const auto it = scripts.find(path);
    return it == scripts.end() ? nullptr : &it->second;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// What a failed protected call says, located as Lua located it.
std::string errorText(const sol::protected_function_result& result) {
    const sol::error error = result;
    return error.what();
}

// Calls `function` with the budget armed, adding the time it took to `pack`'s frame.
template <class... Args>
sol::protected_function_result timedCall(Context& ctx, size_t pack, const sol::protected_function& function, Args&&... args) {
    const Clock::time_point start = Clock::now();
    ctx.deadline = start + ctx.host.call_budget;
    sol::protected_function_result result = function(std::forward<Args>(args)...);
    ctx.deadline = Clock::time_point::max();
    if (pack < ctx.frame_seconds.size()) ctx.frame_seconds[pack] += std::chrono::duration<double>(Clock::now() - start).count();
    return result;
}

// LuaResult.ToBoolean, which is Convert.ToBoolean: nil is false, a number is whether it is
// not zero, a string whether it says "true"; a table or anything else is false here, where
// .NET would throw.
bool dotNetBoolean(const sol::object& value) {
    switch (value.get_type()) {
    case sol::type::boolean: return value.as<bool>();
    case sol::type::number: return value.as<double>() != 0.0;
    case sol::type::string: {
        std::string text = lowercase(value.as<std::string>());
        const size_t first = text.find_first_not_of(" \t\r\n");
        const size_t last = text.find_last_not_of(" \t\r\n");
        return first != std::string::npos && text.substr(first, last - first + 1) == "true";
    }
    default: return false;
    }
}

sol::object scriptArgument(sol::state_view lua, const behaviors::ScriptArg& argument) {
    return std::visit(
        [&](const auto& value) -> sol::object {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return sol::make_object(lua, sol::lua_nil);
            } else {
                return sol::make_object(lua, value);
            }
        },
        argument);
}

// ScriptEngine.LoadScript, for text already in hand: compiled as `file` of `pack`, with `Pack`
// in scope as the chunk's parameter is in Pathing, run in the shared globals. The chunk, or
// nil when it does not compile or errors while running.
sol::object runChunk(sol::state_view lua, size_t pack, const std::string& file, std::string_view text) {
    Context& ctx = context(lua);
    const std::string source = packLabel(ctx, pack) + "/" + file;
    ctx.pack_of_source[source] = pack;

    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    // On the chunk's first line, so every line number stays the file's own.
    std::string code = "local Pack = ...; ";
    code += text;

    sol::load_result loaded = lua.load(code, "@" + source, sol::load_mode::text);
    if (!loaded.valid()) {
        const sol::error error = loaded;
        ctx.console.push(Level::Error, source, error.what());
        ctx.console.push(Level::Error, "system", "Failed to load " + source + ".");
        return sol::make_object(lua, sol::lua_nil);
    }

    sol::protected_function chunk = loaded;
    sol::set_environment(ctx.env, chunk);
    const sol::protected_function_result result = timedCall(ctx, pack, chunk, PackContext{pack});
    if (!result.valid()) {
        ctx.console.push(Level::Error, source, errorText(result));
        ctx.console.push(Level::Error, "system", "Failed to load " + source + ".");
        return sol::make_object(lua, sol::lua_nil);
    }

    return chunk;
}

// Reads `file` of `pack` and runs it. Nil, and a warning unless `quiet_when_missing`, for a
// file the pack does not have.
sol::object loadScript(sol::state_view lua, size_t pack, const std::string& file, bool quiet_when_missing) {
    Context& ctx = context(lua);
    const std::string path = pack::normalizePath(file);
    const std::string* text = packScript(ctx, pack, path);
    if (!text) {
        if (!quiet_when_missing) ctx.console.push(Level::Warn, "system", "Script '" + path + "' could not be found.");
        return sol::make_object(lua, sol::lua_nil);
    }

    const Clock::time_point start = Clock::now();
    sol::object chunk = runChunk(lua, pack, path, *text);
    if (chunk.valid()) {
        // Warned past half a second, as ScriptEngine.LoadScript does.
        const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        ctx.console.push(
            milliseconds > 500.0 ? Level::Warn : Level::Info,
            "system",
            packLabel(ctx, pack) + "/" + path + " loaded in " + std::to_string(static_cast<int>(milliseconds)) + " ms.");
    }
    return chunk;
}

} // namespace

sol::object requireScript(sol::state_view lua, size_t pack, const std::string& name) {
    std::string file = lowercase(name);
    if (!file.ends_with(".lua")) file += ".lua";

    Context& ctx = context(lua);
    const auto key = std::make_pair(pack, pack::normalizePath(file));
    if (const auto it = ctx.required.find(key); it != ctx.required.end()) return it->second;

    sol::object chunk = loadScript(lua, pack, file, false);
    if (chunk.valid()) ctx.required.emplace(key, chunk);
    return chunk;
}

struct Engine::State {
    State(const Host& host, Console& console, Census& census)
        : context(host, console, census) {}

    // Declared before the context, so destroyed after it: everything the context holds is a
    // reference into this state.
    sol::state lua;
    Context context;

    // script-* functions found by name, so a lookup that ignores case is paid once. Only
    // what was found is kept: a script may define a function later.
    std::unordered_map<std::string, sol::protected_function> behavior_functions;
    GameTime last_tick; // what script-tick hands its function: the GameTime of the last tick
};

Engine::Engine(Host host)
    : host_(std::move(host)) {
    reset();
}

Engine::~Engine() = default;

void Engine::reset() {
    state_.reset();
    state_ = std::make_unique<State>(host_, console_, census_);

    sol::state& lua = state_->lua;
    Context& ctx = state_->context;
    // What the sandbox copies from; no io, os, debug, package or utf8 is opened at all.
    lua.open_libraries(sol::lib::base, sol::lib::coroutine, sol::lib::math, sol::lib::string, sol::lib::table);
    setContext(lua.lua_state(), &ctx);
    lua_sethook(lua.lua_state(), budgetHook, LUA_MASKCOUNT, kHookInterval);

    // The collector starts a cycle once the heap is kGcPause percent of what the last left. At
    // Lua's 200 it never catches up with what scripts leave behind every frame: over a player's
    // packs standing still, each cycle began at twice the heap of the last, 3 MB to 44 MB over
    // eight minutes of frames, with a full collection leaving under 2 MB, and each cycle's
    // atomic step a frame of its own that doubled with it, to 76 ms. At 180 the heap still
    // reached 3 MB; at 150 it held under 1 MB, no frame of behaviors over 2 ms. The step
    // multiplier, Lua's 100 to 400, changed neither.
    constexpr int kGcPause = 150;
    constexpr int kGcStepMultiplier = 200;
    lua_gc(lua.lua_state(), LUA_GCINC, kGcPause, kGcStepMultiplier, 0);

    ctx.env = sol::environment(lua, sol::create);
    ctx.frame_seconds.assign(host_.installed ? host_.installed->packs.size() : 0, 0.0);
    bindValues(lua);
    bindWorld(lua);
    bindGlobals(lua, ctx.env);

    frame_history_.assign(ctx.frame_seconds.size(), {});
    console_.push(Level::System, "system", "Loaded new environment.");
}

void Engine::loadPacks() {
    for (size_t pack = 0; pack < host_.installed->packs.size(); ++pack) {
        // pack.lua is optional, and Pathing does not warn about one that is missing.
        loadScript(state_->lua, pack, "pack.lua", true);
    }
}

bool Engine::run(size_t pack, std::string_view file, std::string_view source) {
    const sol::object chunk = runChunk(state_->lua, pack, pack::normalizePath(file), source);
    return chunk.valid();
}

void Engine::tick(double total_seconds, double elapsed_seconds) {
    Context& ctx = state_->context;
    state_->last_tick = GameTime{total_seconds, elapsed_seconds};
    const sol::object game_time = sol::make_object(state_->lua, state_->last_tick);

    // The handlers there were when the tick began; one registered by a handler waits for the
    // next. Event.Update does the same with ToArray.
    const size_t count = ctx.tick_listeners.size();
    std::vector<char> failed(count, 0);
    for (size_t i = 0; i < count; ++i) {
        const Context::TickListener listener = ctx.tick_listeners[i];
        const sol::protected_function_result result = timedCall(ctx, listener.pack, listener.function, game_time);
        if (result.valid()) continue;

        const std::string source = listener.pack < ctx.frame_seconds.size() ? packLabel(ctx, listener.pack) : "system";
        ctx.console.push(Level::Error, source, errorText(result));
        ctx.console.push(Level::System, "system", "A tick callback of " + source + " was unregistered because it threw an exception.");
        failed[i] = 1;
    }
    for (size_t i = count; i-- > 0;) {
        if (failed[i]) ctx.tick_listeners.erase(ctx.tick_listeners.begin() + static_cast<std::ptrdiff_t>(i));
    }

    for (size_t pack = 0; pack < ctx.frame_seconds.size(); ++pack) {
        std::vector<double>& history = frame_history_[pack];
        if (history.size() == kFrameHistory) history.erase(history.begin());
        history.push_back(ctx.frame_seconds[pack]);
        ctx.frame_seconds[pack] = 0.0;
    }
}

const std::vector<MenuNode>& Engine::menus() const {
    return state_->context.menus;
}

void Engine::clickMenu(size_t index) {
    Context& ctx = state_->context;
    if (index >= ctx.menus.size() || ctx.menus[index].removed) return;
    if (ctx.menus[index].can_check) ctx.menus[index].checked = !ctx.menus[index].checked;

    // Copied: a handler that adds an entry moves every entry.
    const std::string name = ctx.menus[index].name;
    const sol::protected_function click = ctx.menu_clicks[index];
    if (!click.valid()) return;
    const sol::protected_function_result result = timedCall(ctx, Context::kNoPack, click, pushMenu(state_->lua, index));
    if (!result.valid()) ctx.console.push(Level::Error, "system", "Menu '" + name + "': " + errorText(result));
}

const std::map<std::string, std::string>& Engine::watches() const {
    return state_->context.watches;
}

const std::vector<std::string>& Engine::info() const {
    return state_->context.info;
}

std::optional<bool> Engine::callBehavior(const behaviors::ScriptCall& call, behaviors::Entity entity, behaviors::Hook hook, bool flag) {
    State& state = *state_;
    Context& ctx = state.context;
    sol::state& lua = state.lua;

    // CallMemberDirect with ignoreCase: the exact name first, then any that matches but for case.
    auto found = state.behavior_functions.find(call.name);
    if (found == state.behavior_functions.end()) {
        sol::object function = ctx.env[call.name];
        if (function.get_type() != sol::type::function) {
            const std::string wanted = lowercase(call.name);
            for (const auto& [key, value] : ctx.env) {
                if (key.get_type() == sol::type::string && value.get_type() == sol::type::function &&
                    lowercase(key.as<std::string>()) == wanted) {
                    function = value;
                    break;
                }
            }
        }
        if (function.get_type() != sol::type::function) return std::nullopt;
        found = state.behavior_functions.emplace(call.name, sol::protected_function(function)).first;
    }

    const runtime::LiveMap& live = *host_.live;
    const bool known = entity.trail ? entity.index < live.trails.size() : entity.index < live.markers.size();
    if (!known) return std::nullopt;
    const size_t pack = entity.trail ? live.trails[entity.index].pack : live.markers[entity.index].pack;

    std::vector<sol::object> arguments;
    arguments.push_back(entity.trail ? pushTrail(lua, entity.index) : pushMarker(lua, entity.index));
    if (hook == behaviors::Hook::Focus || hook == behaviors::Hook::Trigger) arguments.push_back(sol::make_object(lua, flag));
    if (hook == behaviors::Hook::Tick) arguments.push_back(sol::make_object(lua, state.last_tick));
    for (const behaviors::ScriptArg& argument : call.args) arguments.push_back(scriptArgument(lua, argument));

    const sol::protected_function_result result = timedCall(ctx, pack, found->second, sol::as_args(arguments));
    if (!result.valid()) {
        const std::string source = pack < ctx.frame_seconds.size() ? packLabel(ctx, pack) : "system";
        ctx.console.push(Level::Error, source, call.name + ": " + errorText(result));
        return std::nullopt;
    }
    if (result.return_count() == 0) return std::nullopt;
    return dotNetBoolean(result.get<sol::object>(0));
}

double Engine::frameSeconds(size_t pack) const {
    if (pack >= frame_history_.size() || frame_history_[pack].empty()) return 0.0;
    const std::vector<double>& history = frame_history_[pack];
    return std::accumulate(history.begin(), history.end(), 0.0) / static_cast<double>(history.size());
}

size_t Engine::heapBytes() const {
    lua_State* L = state_->lua.lua_state();
    return static_cast<size_t>(lua_gc(L, LUA_GCCOUNT)) * 1024 + static_cast<size_t>(lua_gc(L, LUA_GCCOUNTB));
}

} // namespace viewer::script
