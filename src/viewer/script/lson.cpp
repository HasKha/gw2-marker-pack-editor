#include "viewer/script/lson.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>

#include "viewer/script/object.h"

namespace viewer::script {

namespace {

constexpr int kMaxDepth = 100;

bool isIdentifierStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentifierChar(char c) {
    return isIdentifierStart(c) || (c >= '0' && c <= '9');
}

// A name as the reader takes it: NeoLua writes a key bare when .NET's Char.IsLetter and
// IsLetterOrDigit take all of it, so a character name with an accent, "Zhé", is a bare key in
// Teh's Trails' stored tables. Every byte of UTF-8 past ASCII is taken as part of a name, which
// reads all of that and more; the writer keeps to ASCII, which NeoLua reads as well.
bool isNameStart(char c) {
    return isIdentifierStart(c) || static_cast<unsigned char>(c) >= 0x80;
}

bool isNameChar(char c) {
    return isIdentifierChar(c) || static_cast<unsigned char>(c) >= 0x80;
}

bool isKeyword(std::string_view word) {
    static constexpr std::string_view kKeywords[] = {"and",      "break",  "do",   "else", "elseif", "end",  "false", "for",
                                                     "function", "goto",   "if",   "in",   "local",  "nil",  "not",   "or",
                                                     "repeat",   "return", "then", "true", "until",  "while"};
    for (std::string_view keyword : kKeywords) {
        if (word == keyword) return true;
    }
    return false;
}

// ── Writing ───────────────────────────────────────────────────────────────────

class Writer
{
public:
    Writer(lua_State* L, bool pretty, const std::string& indent)
        : lua(L)
        , pretty(pretty)
        , indent(indent) {}

    void table(const sol::table& table, int level) {
        if (level > kMaxDepth) raise(lua, "table is nested too deeply for LSON");

        if (table.begin() == table.end()) {
            out += "{}";
            return;
        }
        out += '{';

        // The array part, bare, as long as it runs unbroken from 1.
        lua_Integer length = 0;
        while (table.raw_get<sol::object>(length + 1).get_type() != sol::type::lua_nil) ++length;

        bool first = true;
        const auto separate = [&] {
            if (!first) out += ',';
            first = false;
            newline(level);
        };

        for (lua_Integer i = 1; i <= length; ++i) {
            separate();
            value(table.raw_get<sol::object>(i), level + 1);
        }
        for (const auto& [key, item] : table) {
            if (key.get_type() == sol::type::number && isInteger(lua, key)) {
                const lua_Integer index = key.as<lua_Integer>();
                if (index >= 1 && index <= length) continue;
            }
            separate();
            this->key(key, level);
            out += pretty ? " = " : "=";
            value(item, level + 1);
        }

        newline(level - 1);
        out += '}';
    }

    std::string take() { return std::move(out); }

private:
    void newline(int level) {
        if (!pretty) return;
        out += '\n';
        for (int i = 0; i < level; ++i) out += indent;
    }

    void key(const sol::object& key, int level) {
        if (key.get_type() == sol::type::string) {
            const std::string name = key.as<std::string>();
            if (!name.empty() && isIdentifierStart(name[0]) && std::all_of(name.begin(), name.end(), isIdentifierChar) &&
                !isKeyword(name)) {
                out += name;
                return;
            }
        }
        out += '[';
        value(key, level + 1);
        out += ']';
    }

    void value(const sol::object& value, int level) {
        switch (value.get_type()) {
        case sol::type::boolean: out += value.as<bool>() ? "true" : "false"; break;
        case sol::type::number: number(value); break;
        case sol::type::string: string(value.as<std::string>()); break;
        case sol::type::table: table(value.as<sol::table>(), level); break;
        default: raise(lua, std::string("type '") + lua_typename(lua, static_cast<int>(value.get_type())) + "' is not supported in LSON");
        }
    }

    // Integers as they are; a float as its shortest text with ".0" when that reads as an
    // integer, so it stays a float — Lua.RtWriteValue's WriteNumber. NaN is nil and the
    // infinities the strings NeoLua writes for them.
    void number(const sol::object& value) {
        if (isInteger(lua, value)) {
            out += std::to_string(value.as<lua_Integer>());
            return;
        }
        const double number = value.as<double>();
        if (std::isnan(number)) {
            out += "nil";
        } else if (std::isinf(number)) {
            out += number > 0 ? "\"+Infinity\"" : "\"-Infinity\"";
        } else {
            char buffer[32];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(number));
            const std::string_view text(buffer, static_cast<size_t>(result.ptr - buffer));
            out += text;
            if (text.find_first_of(".eE") == std::string_view::npos) out += ".0";
        }
    }

    void string(const std::string& text) {
        out += '"';
        for (char c : text) {
            switch (c) {
            case '\0': out += "\\x00"; break;
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
            }
        }
        out += '"';
    }

    lua_State* lua;
    bool pretty;
    const std::string& indent;
    std::string out;
};

// ── Reading ───────────────────────────────────────────────────────────────────

class Reader
{
public:
    Reader(sol::state_view lua, std::string_view text)
        : lua(lua)
        , text(text) {}

    sol::table table(int depth) {
        if (depth > kMaxDepth) fail("table is nested too deeply");
        expect('{');
        sol::table result = lua.create_table();
        lua_Integer next_index = 1;

        skipSpace();
        if (peek() == '}') {
            ++at;
            return result;
        }
        for (;;) {
            field(result, &next_index, depth);
            skipSpace();
            if (peek() == ',' || peek() == ';') {
                ++at;
                skipSpace();
                if (peek() == '}') break; // an optional last separator
                continue;
            }
            break;
        }
        expect('}');
        return result;
    }

    void end() {
        skipSpace();
        if (at != text.size()) fail("unexpected text after the table");
    }

private:
    [[noreturn]] void fail(const std::string& what) { raise(lua.lua_state(), "LSON: " + what + " at offset " + std::to_string(at)); }

    char peek() const { return at < text.size() ? text[at] : '\0'; }

    void skipSpace() {
        for (;;) {
            while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
            if (text.substr(at, 2) != "--") return;
            while (at < text.size() && text[at] != '\n') ++at;
        }
    }

    void expect(char c) {
        skipSpace();
        if (peek() != c) fail(std::string("'") + c + "' expected");
        ++at;
    }

    std::string_view identifier() {
        const size_t start = at;
        while (at < text.size() && isNameChar(text[at])) ++at;
        return text.substr(start, at - start);
    }

    void field(sol::table& table, lua_Integer* next_index, int depth) {
        skipSpace();
        if (peek() == '[') {
            ++at;
            const sol::object key = value(depth);
            if (key.get_type() == sol::type::lua_nil) fail("nil cannot be a key");
            expect(']');
            expect('=');
            table.raw_set(key, value(depth));
            return;
        }
        if (isNameStart(peek())) {
            const size_t start = at;
            const std::string_view name = identifier();
            skipSpace();
            if (!isKeyword(name) && peek() == '=') {
                ++at;
                table.raw_set(std::string(name), value(depth));
                return;
            }
            at = start; // a keyword value: nil, true, false
        }
        table.raw_set((*next_index)++, value(depth));
    }

    sol::object value(int depth) {
        skipSpace();
        const char c = peek();
        if (c == '{') return table(depth + 1);
        if (c == '"' || c == '\'') return sol::make_object(lua, quoted());
        if (c == '-' || c == '.' || (c >= '0' && c <= '9')) return number();
        if (isNameStart(c)) {
            const std::string_view word = identifier();
            if (word == "nil") return sol::make_object(lua, sol::lua_nil);
            if (word == "true") return sol::make_object(lua, true);
            if (word == "false") return sol::make_object(lua, false);
            fail("unexpected '" + std::string(word) + "'");
        }
        fail("a value expected");
    }

    sol::object number() {
        const bool negative = peek() == '-';
        if (negative) {
            ++at;
            skipSpace();
        }
        // Everything that could be part of a numeral; from_chars then says whether it is one.
        const size_t start = at;
        while (at < text.size()) {
            const char c = text[at];
            const bool exponent_sign = (c == '+' || c == '-') && at > start && (text[at - 1] == 'e' || text[at - 1] == 'E');
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && !exponent_sign) break;
            ++at;
        }
        const std::string_view digits = text.substr(start, at - start);
        if (digits.empty()) fail("a number expected");
        const bool hex = digits.starts_with("0x") || digits.starts_with("0X");

        if (hex) {
            uint64_t value = 0;
            const auto result = std::from_chars(digits.data() + 2, digits.data() + digits.size(), value, 16);
            if (result.ptr != digits.data() + digits.size()) fail("malformed number");
            const lua_Integer integer = static_cast<lua_Integer>(value);
            return sol::make_object(lua, negative ? -integer : integer);
        }
        if (digits.find_first_of(".eE") == std::string_view::npos) {
            lua_Integer value = 0;
            const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (result.ptr == digits.data() + digits.size() && result.ec == std::errc())
                return sol::make_object(lua, negative ? -value : value);
        }
        double value = 0.0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (result.ptr != digits.data() + digits.size()) fail("malformed number");
        return sol::make_object(lua, negative ? -value : value);
    }

    // A short string with Lua's escapes.
    std::string quoted() {
        const char quote = text[at++];
        std::string out;
        while (at < text.size() && text[at] != quote) {
            char c = text[at++];
            if (c == '\n') fail("unfinished string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= text.size()) break;
            c = text[at++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'a': out += '\a'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'v': out += '\v'; break;
            case '\n': out += '\n'; break;
            case 'x': {
                unsigned value = 0;
                const auto result = std::from_chars(text.data() + at, text.data() + std::min(at + 2, text.size()), value, 16);
                if (result.ptr != text.data() + at + 2) fail("malformed \\x escape");
                at += 2;
                out += static_cast<char>(value);
                break;
            }
            default:
                if (c >= '0' && c <= '9') {
                    unsigned value = static_cast<unsigned>(c - '0');
                    for (int i = 0; i < 2 && at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])); ++i) {
                        value = value * 10 + static_cast<unsigned>(text[at++] - '0');
                    }
                    if (value > 255) fail("escape too large");
                    out += static_cast<char>(value);
                } else {
                    out += c; // \\, \", \' and anything else as itself
                }
            }
        }
        if (peek() != quote) fail("unfinished string");
        ++at;
        return out;
    }

    sol::state_view lua;
    std::string_view text;
    size_t at = 0;
};

} // namespace

std::string toLson(lua_State* L, const sol::table& table, bool pretty, const std::string& indent) {
    Writer writer(L, pretty, indent);
    writer.table(table, 1);
    return writer.take();
}

sol::table fromLson(sol::state_view lua, std::string_view text) {
    Reader reader(lua, text);
    sol::table table = reader.table(0);
    reader.end();
    return table;
}

} // namespace viewer::script
