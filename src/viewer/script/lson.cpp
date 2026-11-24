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
        : L_(L)
        , pretty_(pretty)
        , indent_(indent) {}

    void table(const sol::table& table, int level) {
        if (level > kMaxDepth) raise(L_, "table is nested too deeply for LSON");

        if (table.begin() == table.end()) {
            out_ += "{}";
            return;
        }
        out_ += '{';

        // The array part, bare, as long as it runs unbroken from 1.
        lua_Integer length = 0;
        while (table.raw_get<sol::object>(length + 1).get_type() != sol::type::lua_nil) ++length;

        bool first = true;
        const auto separate = [&] {
            if (!first) out_ += ',';
            first = false;
            newline(level);
        };

        for (lua_Integer i = 1; i <= length; ++i) {
            separate();
            value(table.raw_get<sol::object>(i), level + 1);
        }
        for (const auto& [key, item] : table) {
            if (key.get_type() == sol::type::number && isInteger(L_, key)) {
                const lua_Integer index = key.as<lua_Integer>();
                if (index >= 1 && index <= length) continue;
            }
            separate();
            this->key(key, level);
            out_ += pretty_ ? " = " : "=";
            value(item, level + 1);
        }

        newline(level - 1);
        out_ += '}';
    }

    std::string take() { return std::move(out_); }

private:
    void newline(int level) {
        if (!pretty_) return;
        out_ += '\n';
        for (int i = 0; i < level; ++i) out_ += indent_;
    }

    void key(const sol::object& key, int level) {
        if (key.get_type() == sol::type::string) {
            const std::string name = key.as<std::string>();
            if (!name.empty() && isIdentifierStart(name[0]) && std::all_of(name.begin(), name.end(), isIdentifierChar) &&
                !isKeyword(name)) {
                out_ += name;
                return;
            }
        }
        out_ += '[';
        value(key, level + 1);
        out_ += ']';
    }

    void value(const sol::object& value, int level) {
        switch (value.get_type()) {
        case sol::type::boolean: out_ += value.as<bool>() ? "true" : "false"; break;
        case sol::type::number: number(value); break;
        case sol::type::string: string(value.as<std::string>()); break;
        case sol::type::table: table(value.as<sol::table>(), level); break;
        default: raise(L_, std::string("type '") + lua_typename(L_, static_cast<int>(value.get_type())) + "' is not supported in LSON");
        }
    }

    // Integers as they are; a float as its shortest text with ".0" when that reads as an
    // integer, so it stays a float — Lua.RtWriteValue's WriteNumber. NaN is nil and the
    // infinities the strings NeoLua writes for them.
    void number(const sol::object& value) {
        if (isInteger(L_, value)) {
            out_ += std::to_string(value.as<lua_Integer>());
            return;
        }
        const double number = value.as<double>();
        if (std::isnan(number)) {
            out_ += "nil";
        } else if (std::isinf(number)) {
            out_ += number > 0 ? "\"+Infinity\"" : "\"-Infinity\"";
        } else {
            char buffer[32];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(number));
            const std::string_view text(buffer, static_cast<size_t>(result.ptr - buffer));
            out_ += text;
            if (text.find_first_of(".eE") == std::string_view::npos) out_ += ".0";
        }
    }

    void string(const std::string& text) {
        out_ += '"';
        for (char c : text) {
            switch (c) {
            case '\0': out_ += "\\x00"; break;
            case '\\': out_ += "\\\\"; break;
            case '"': out_ += "\\\""; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default: out_ += c; break;
            }
        }
        out_ += '"';
    }

    lua_State* L_;
    bool pretty_;
    const std::string& indent_;
    std::string out_;
};

// ── Reading ───────────────────────────────────────────────────────────────────

class Reader
{
public:
    Reader(sol::state_view lua, std::string_view text)
        : lua_(lua)
        , text_(text) {}

    sol::table table(int depth) {
        if (depth > kMaxDepth) fail("table is nested too deeply");
        expect('{');
        sol::table result = lua_.create_table();
        lua_Integer next_index = 1;

        skipSpace();
        if (peek() == '}') {
            ++at_;
            return result;
        }
        for (;;) {
            field(result, &next_index, depth);
            skipSpace();
            if (peek() == ',' || peek() == ';') {
                ++at_;
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
        if (at_ != text_.size()) fail("unexpected text after the table");
    }

private:
    [[noreturn]] void fail(const std::string& what) { raise(lua_.lua_state(), "LSON: " + what + " at offset " + std::to_string(at_)); }

    char peek() const { return at_ < text_.size() ? text_[at_] : '\0'; }

    void skipSpace() {
        for (;;) {
            while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_]))) ++at_;
            if (text_.substr(at_, 2) != "--") return;
            while (at_ < text_.size() && text_[at_] != '\n') ++at_;
        }
    }

    void expect(char c) {
        skipSpace();
        if (peek() != c) fail(std::string("'") + c + "' expected");
        ++at_;
    }

    std::string_view identifier() {
        const size_t start = at_;
        while (at_ < text_.size() && isNameChar(text_[at_])) ++at_;
        return text_.substr(start, at_ - start);
    }

    void field(sol::table& table, lua_Integer* next_index, int depth) {
        skipSpace();
        if (peek() == '[') {
            ++at_;
            const sol::object key = value(depth);
            if (key.get_type() == sol::type::lua_nil) fail("nil cannot be a key");
            expect(']');
            expect('=');
            table.raw_set(key, value(depth));
            return;
        }
        if (isNameStart(peek())) {
            const size_t start = at_;
            const std::string_view name = identifier();
            skipSpace();
            if (!isKeyword(name) && peek() == '=') {
                ++at_;
                table.raw_set(std::string(name), value(depth));
                return;
            }
            at_ = start; // a keyword value: nil, true, false
        }
        table.raw_set((*next_index)++, value(depth));
    }

    sol::object value(int depth) {
        skipSpace();
        const char c = peek();
        if (c == '{') return table(depth + 1);
        if (c == '"' || c == '\'') return sol::make_object(lua_, quoted());
        if (c == '-' || c == '.' || (c >= '0' && c <= '9')) return number();
        if (isNameStart(c)) {
            const std::string_view word = identifier();
            if (word == "nil") return sol::make_object(lua_, sol::lua_nil);
            if (word == "true") return sol::make_object(lua_, true);
            if (word == "false") return sol::make_object(lua_, false);
            fail("unexpected '" + std::string(word) + "'");
        }
        fail("a value expected");
    }

    sol::object number() {
        const bool negative = peek() == '-';
        if (negative) {
            ++at_;
            skipSpace();
        }
        // Everything that could be part of a numeral; from_chars then says whether it is one.
        const size_t start = at_;
        while (at_ < text_.size()) {
            const char c = text_[at_];
            const bool exponent_sign = (c == '+' || c == '-') && at_ > start && (text_[at_ - 1] == 'e' || text_[at_ - 1] == 'E');
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && !exponent_sign) break;
            ++at_;
        }
        const std::string_view digits = text_.substr(start, at_ - start);
        if (digits.empty()) fail("a number expected");
        const bool hex = digits.starts_with("0x") || digits.starts_with("0X");

        if (hex) {
            uint64_t value = 0;
            const auto result = std::from_chars(digits.data() + 2, digits.data() + digits.size(), value, 16);
            if (result.ptr != digits.data() + digits.size()) fail("malformed number");
            const lua_Integer integer = static_cast<lua_Integer>(value);
            return sol::make_object(lua_, negative ? -integer : integer);
        }
        if (digits.find_first_of(".eE") == std::string_view::npos) {
            lua_Integer value = 0;
            const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (result.ptr == digits.data() + digits.size() && result.ec == std::errc())
                return sol::make_object(lua_, negative ? -value : value);
        }
        double value = 0.0;
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (result.ptr != digits.data() + digits.size()) fail("malformed number");
        return sol::make_object(lua_, negative ? -value : value);
    }

    // A short string with Lua's escapes.
    std::string quoted() {
        const char quote = text_[at_++];
        std::string out;
        while (at_ < text_.size() && text_[at_] != quote) {
            char c = text_[at_++];
            if (c == '\n') fail("unfinished string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) break;
            c = text_[at_++];
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
                const auto result = std::from_chars(text_.data() + at_, text_.data() + std::min(at_ + 2, text_.size()), value, 16);
                if (result.ptr != text_.data() + at_ + 2) fail("malformed \\x escape");
                at_ += 2;
                out += static_cast<char>(value);
                break;
            }
            default:
                if (c >= '0' && c <= '9') {
                    unsigned value = static_cast<unsigned>(c - '0');
                    for (int i = 0; i < 2 && at_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[at_])); ++i) {
                        value = value * 10 + static_cast<unsigned>(text_[at_++] - '0');
                    }
                    if (value > 255) fail("escape too large");
                    out += static_cast<char>(value);
                } else {
                    out += c; // \\, \", \' and anything else as itself
                }
            }
        }
        if (peek() != quote) fail("unfinished string");
        ++at_;
        return out;
    }

    sol::state_view lua_;
    std::string_view text_;
    size_t at_ = 0;
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
