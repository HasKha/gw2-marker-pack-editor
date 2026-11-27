// Markus's Lua held to NeoLua's, as Pathing builds it: every case of lua_parity/cases.lua run in a
// fresh state, what it printed and whether it raised compared with what scripts/neolua_oracle wrote
// in expected.txt, and the libraries' names compared with its surface.txt. See docs/testing.md.
//
// A difference is either fixed or listed below with its reason, and a listed one that has gone
// away fails too, so the lists stay the true account of where the two differ.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "corpus.h"
#include "viewer/game/mumble.h"
#include "viewer/live/runtime.h"
#include "viewer/packs/registry.h"
#include "viewer/script/engine.h"
#include "viewer/store.h"

namespace {

using viewer::script::Level;

const std::filesystem::path PARITY_DIR = MARKUS_LUA_PARITY_DIR;

// What a case did: the lines it printed, and whether it ended in an error.
struct Outcome {
    std::vector<std::string> lines;
    bool error = false;

    bool operator==(const Outcome&) const = default;
};

struct Case {
    std::string name;
    std::string code;
};

// Reasons several cases share.
constexpr const char* SINGLE_FLOATS =
    "NeoLua's floats are single precision as Pathing builds it: 0.1 prints as 0.100000001490116, 1/3 as 0.3333333";
constexpr const char* COROUTINE_STATUS = "a coroutine that has finished, or failed, is still suspended";
constexpr const char* NOT_IN_LIBRARY = "NeoLua has it and Markus lacks it";
constexpr const char* NET_TYPED_RESULT = "the result is a .NET type that NeoLua's type() calls userdata, though it prints as the number";
constexpr const char* SWAPPED_COMPARISON = "NeoLua answers \"a\" > true and true < \"a\" apart, where Lua compiles both to one test";
constexpr const char* NIL_BEFORE_STRING =
    "nil compared with a string throws NullReferenceException, but not the other way round, which Lua compiles to the same test";

// Where matching NeoLua would take another interpreter, so Markus keeps Lua 5.4's way.
const std::map<std::string, std::string> INTERPRETER_DIFFERENCES = {
    {"number-single-precision", SINGLE_FLOATS},
    {"number-large-single-precision", SINGLE_FLOATS},
    {"number-small-single-precision", SINGLE_FLOATS},
    {"number-concat-single-precision", SINGLE_FLOATS},
    {"number-tostring-single-precision", SINGLE_FLOATS},
    {"integer-overflow", "NeoLua's integers are Int32 as Pathing builds it, and wrap or widen to Int64 by operation"},
    {"arithmetic-floor-division-negative", "NeoLua's // rounds toward zero"},
    {"arithmetic-modulo", "NeoLua's % is .NET's remainder, with the sign of the dividend"},
    {"arithmetic-modulo-huge", "NeoLua's % is .NET's remainder, and math.huge is not infinity"},
    {"coercion-equality", "NeoLua's == converts between a string and a number"},
    {"string-comparison", "NeoLua's < compares strings by culture, where Lua compares bytes"},
    {"string-length-utf8", "NeoLua's strings are UTF-16, so # counts UTF-16 units, where Lua counts bytes"},
    {"table-length-hole", "NeoLua's # stops at the first nil"},
    {"pairs-order", "NeoLua's pairs goes in the order keys were added; Lua's order is its hash's, and changes between runs"},
    {"concat-table", "NeoLua's .. writes a table as 'table'"},
    {R"(op nil / 1)", NET_TYPED_RESULT},
    {R"(op false / 1)", NET_TYPED_RESULT},
    {R"(op true / 1)", NET_TYPED_RESULT},
    {R"(op nil // 2.5)", NET_TYPED_RESULT},
    {R"(op nil & 2.5)", NET_TYPED_RESULT},
    {R"(op nil | 2.5)", NET_TYPED_RESULT},
    {R"(op nil ~ 2.5)", NET_TYPED_RESULT},
    {R"(op nil << 2.5)", NET_TYPED_RESULT},
    {R"(op nil >> 2.5)", NET_TYPED_RESULT},
    {R"(op -false)", NET_TYPED_RESULT},
    {R"(op -true)", NET_TYPED_RESULT},
    {R"(op "a" < true)", SWAPPED_COMPARISON},
    {R"(op "a" <= true)", SWAPPED_COMPARISON},
    {R"(op "a" > true)", SWAPPED_COMPARISON},
    {R"(op "a" >= true)", SWAPPED_COMPARISON},
    {"nil-floor-division-type", "NeoLua's // gives an integer, of floats too"},
    {"globals-g-is-the-environment", "NeoLua compiles _G to the environment, which has no _G of its own: _G._G is nil"},
};

// Where NeoLua fails and Markus does not. A pack that works in Pathing never gets there, so
// failing as NeoLua does would gain nothing.
const std::map<std::string, std::string> NEOLUA_FAILURES = {
    {"arithmetic-float-floor-division-by-zero", "DivideByZeroException"},
    {"power-negative-exponent", "2 ^ -1 does not parse"},
    {"string-escape-unicode", "\\u{} does not parse"},
    {"string-gsub-function", "the function is handed a .NET Group, which has no upper"},
    {"string-format-nil", "%s of nil throws NullReferenceException"},
    {"string-format-float-as-integer", "%d of a float throws FormatException"},
    {"string-format-fraction-as-integer", "%d of a float throws FormatException"},
    {"table-pack", "table.pack cannot be called"},
    {"table-move", "table.move throws NullReferenceException"},
    {R"(op nil < "1")", NIL_BEFORE_STRING},
    {R"(op nil < "a")", NIL_BEFORE_STRING},
    {R"(op nil <= "1")", NIL_BEFORE_STRING},
    {R"(op nil <= "a")", NIL_BEFORE_STRING},
    {R"(op nil > "1")", NIL_BEFORE_STRING},
    {R"(op nil > "a")", NIL_BEFORE_STRING},
    {R"(op nil >= "1")", NIL_BEFORE_STRING},
    {R"(op nil >= "a")", NIL_BEFORE_STRING},
};

// Where Markus could be NeoLua without another interpreter, and is not yet.
const std::map<std::string, std::string> LIBRARY_DIFFERENCES = {
    {"string-format-values", "%s writes a boolean as .NET does: True"},
    {"tonumber", "tonumber of text that is no number returns nothing, where Lua returns nil"},
    {"select-positive", "select(n, ...) starts one past n in NeoLua 1.3.20-beta.1, fixed after it (neolua #198)"},
    {"select-float-index", "select starts one past n, and rounds its index half to even"},
    {"table-insert-float-position", "insert puts a value at a float position under that key, out of the sequence"},
    {"string-upper-lower", "upper and lower are .NET's, Unicode's cases"},
    {"string-find-empty", "a pattern that matches empty finds nothing"},
    {"string-match-anchor", "a pattern is a .NET regular expression, where ^ anywhere is an anchor"},
    {"string-match-balanced", "a %b match returns an empty capture after the match"},
    {"string-match-frontier", "%f matches nothing"},
    {"string-gsub-function-nil", "the function is handed a .NET Group, which equals no string"},
    {"string-format-float", "string.format rounds half away from zero, as .NET does, and writes three exponent digits"},
    {"string-dotnet-members", "a string has .NET's members: Replace, Length, ToUpper"},
    {"type-libraries", "a library is a .NET type, which type() calls userdata"},
    {"math-floor-type", "floor and ceil return floats"},
    {"math-fmod-modf", "modf of a negative number returns a positive fraction"},
    {"math-tointeger", "tointeger rounds a fraction, half to even, where Lua returns nil"},
    {"math-ult", "ult(-1, 2) is true"},
    {"table-concat-nested", "concat writes a table as 'table'"},
    {"table-insert-out-of-range", "insert past the end does nothing"},
    {"table-insert-remove", "remove from an empty table returns nothing, not nil"},
    {"table-sort", "sort compares strings by culture, as NeoLua's < does"},
    {"table-library-extras", NOT_IN_LIBRARY},
    {"coroutine-basic", COROUTINE_STATUS},
    {"coroutine-status", COROUTINE_STATUS},
    {"coroutine-error", COROUTINE_STATUS},
    {"coroutine-wrap", "a wrapped coroutine returns resume's true before what it yielded"},
    {"bit32", NOT_IN_LIBRARY},
    {"globals-neolua", NOT_IN_LIBRARY},
};

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Each "-- case: <name>" line starts a case that runs to the next, as the oracle reads them.
std::vector<Case> readCases() {
    constexpr std::string_view marker = "-- case: ";
    std::vector<Case> cases;
    for (const std::string& line : readLines(PARITY_DIR / "cases.lua")) {
        if (line.starts_with(marker)) {
            cases.push_back(Case{.name = line.substr(marker.size())});
        } else if (!cases.empty()) {
            cases.back().code += line + '\n';
        }
    }
    return cases;
}

// "== <name>" starts a case's lines; "!error <what>" says it raised, the text there for a reader.
std::map<std::string, Outcome> readExpected() {
    std::map<std::string, Outcome> expected;
    Outcome* current = nullptr;
    for (const std::string& line : readLines(PARITY_DIR / "expected.txt")) {
        if (line.starts_with("== ")) {
            current = &expected[line.substr(3)];
        } else if (current && line.starts_with("!error")) {
            current->error = true;
        } else if (current) {
            current->lines.push_back(line);
        }
    }
    return expected;
}

// "<library> <name>" lines, `_G` for the globals.
std::set<std::string> readSurface() {
    std::set<std::string> surface;
    for (const std::string& line : readLines(PARITY_DIR / "surface.txt")) {
        if (!line.starts_with('#')) surface.insert(line);
    }
    return surface;
}

// An engine over an empty pack, which is what Engine::run needs to name a script's source.
struct Sandbox {
    Sandbox()
        : temp("lua_parity") {
        corpus::writeFile(temp / "pack" / "markers.xml", "<OverlayData/>");
        viewer::runtime::install(&installed, std::make_unique<viewer::registry::LoadedPack>(viewer::registry::loadPack(temp / "pack")));
        viewer::script::Host host;
        host.installed = &installed;
        host.live = &live;
        host.category_states = &states;
        host.storage = &storage;
        host.mumble = &mumble;
        host.call_budget = std::chrono::milliseconds(500);
        engine = std::make_unique<viewer::script::Engine>(host);
    }

    Outcome run(const std::string& code) {
        engine->reset();
        engine->console().clear();
        Outcome outcome;
        outcome.error = !engine->run(0, "cases.lua", code);
        for (const viewer::script::Message& message : engine->console().messages()) {
            if (message.level != Level::Info) continue;
            std::istringstream text(message.text);
            for (std::string line; std::getline(text, line);) outcome.lines.push_back(line);
            if (message.text.empty() || message.text.back() == '\n') outcome.lines.emplace_back();
        }
        return outcome;
    }

    corpus::TempDir temp;
    viewer::runtime::Installed installed;
    viewer::runtime::LiveMap live;
    viewer::store::CategoryStates states;
    viewer::store::KeyValues storage;
    viewer::MumbleState mumble;
    std::unique_ptr<viewer::script::Engine> engine;
};

std::string describe(const Outcome& outcome) {
    std::string text;
    for (const std::string& line : outcome.lines) text += "  | " + line + '\n';
    if (outcome.error) text += "  !error\n";
    return text;
}

const std::string* acceptedReason(const std::string& name) {
    for (const auto* list : {&INTERPRETER_DIFFERENCES, &NEOLUA_FAILURES, &LIBRARY_DIFFERENCES}) {
        if (const auto found = list->find(name); found != list->end()) return &found->second;
    }
    return nullptr;
}

} // namespace

TEST_CASE("Lua runs each parity case as NeoLua does, or the difference is listed", "[viewer][script][lua_parity]") {
    const std::vector<Case> cases = readCases();
    const std::map<std::string, Outcome> expected = readExpected();
    REQUIRE_FALSE(cases.empty());

    Sandbox sandbox;
    std::set<std::string> names;
    for (const Case& parity_case : cases) {
        names.insert(parity_case.name);
        const auto oracle = expected.find(parity_case.name);
        INFO("case " << parity_case.name);
        if (oracle == expected.end()) {
            FAIL_CHECK("no expected output: run scripts/neolua_oracle again");
            continue;
        }

        const Outcome ours = sandbox.run(parity_case.code);
        INFO("NeoLua:\n" << describe(oracle->second) << "Markus:\n" << describe(ours));
        if (const std::string* reason = acceptedReason(parity_case.name)) {
            INFO("listed as a difference: " << *reason);
            CHECK_FALSE(ours == oracle->second);
        } else {
            CHECK(ours == oracle->second);
        }
    }

    // A listed name with no case is a list gone stale.
    for (const auto* list : {&INTERPRETER_DIFFERENCES, &NEOLUA_FAILURES, &LIBRARY_DIFFERENCES}) {
        for (const auto& [name, reason] : *list) {
            INFO("listed case " << name);
            CHECK(names.contains(name));
        }
    }
}

TEST_CASE("Lua's libraries and globals have NeoLua's names, or the difference is listed", "[viewer][script][lua_parity]") {
    // Pathing's own globals, outside the region of PathingGlobal the oracle copies; script_test
    // covers each.
    const std::set<std::string> pathing_api =
        {"Debug", "Event", "I", "Menu", "Mumble", "Pathing", "PathingVersion", "Storage", "User", "World", "os", "require"};
    // Names one side has and the other lacks, as "<library> <name>", and why.
    constexpr const char* not_yet = "not yet matched";
    const std::map<std::string, std::string> accepted = {
        {"_G bit32", not_yet},
        {"_G mpairs", not_yet},
        {"_G nextKey", not_yet},
        {"_G rawarray", not_yet},
        {"_G rawmembers", not_yet},
        {"bit32 arshift", not_yet},
        {"bit32 band", not_yet},
        {"bit32 bnot", not_yet},
        {"bit32 bor", not_yet},
        {"bit32 btest", not_yet},
        {"bit32 bxor", not_yet},
        {"bit32 extract", not_yet},
        {"bit32 lrotate", not_yet},
        {"bit32 lshift", not_yet},
        {"bit32 replace", not_yet},
        {"bit32 rrotate", not_yet},
        {"bit32 rshift", not_yet},
        {"table FromJson", not_yet},
        {"table ToJson", not_yet},
        {"table ToLsonCore", not_yet},
        {"table collect", not_yet},
        {"table merge", not_yet},
        {"string __TranslateRegEx", "NeoLua's own helper, public by accident; no script calls it"},
        {"table csMetaTable", "a .NET constant, \"__metatable\"; no script reads it"},
    };

    std::set<std::string> ours;
    {
        Sandbox sandbox;
        const Outcome listed = sandbox.run(R"(
for name in pairs(_ENV) do print("_G " .. name) end
for _, library in ipairs({ "bit32", "coroutine", "math", "string", "table" }) do
  local members = rawget(_ENV, library)
  if members then
    for name in pairs(members) do print(library .. " " .. name) end
  end
end)");
        REQUIRE_FALSE(listed.error);
        for (const std::string& line : listed.lines) {
            if (!(line.starts_with("_G ") && pathing_api.contains(line.substr(3)))) ours.insert(line);
        }
    }
    const std::set<std::string> theirs = readSurface();

    std::set<std::string> different;
    std::set_symmetric_difference(ours.begin(), ours.end(), theirs.begin(), theirs.end(), std::inserter(different, different.end()));
    for (const std::string& name : different) {
        INFO((theirs.contains(name) ? "NeoLua has, Markus lacks: " : "Markus has, NeoLua lacks: ") << name);
        CHECK(accepted.contains(name));
    }
    for (const auto& [name, reason] : accepted) {
        INFO("listed as a difference: " << name << ", " << reason);
        CHECK(different.contains(name));
    }
}
