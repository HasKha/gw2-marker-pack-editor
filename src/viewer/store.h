#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

// What outlives a session about the packs themselves, in `<addons>/Markus/`.
namespace viewer::store {

// Moves the addon's folder from `from`, its old name, to `to`, so a player's packs and settings
// follow the addon across the rename. Only when `from` is a folder and `to` is missing or empty:
// Nexus creates `to` as soon as anything asks for it, and a `to` with something in it is the
// new name's own and wins. Returns what went wrong, empty when nothing did or there was
// nothing to move; `to` exists afterwards either way.
std::string adoptFolder(const std::filesystem::path& from, const std::filesystem::path& to);

constexpr const char* CATEGORIES_FILE_NAME = "categories.json";
constexpr const char* STORAGE_FILE_NAME = "kv.json"; // Pathing's own name for it

// Which categories the player turned on or off, by id.
//
// Only a choice that differs from the pack's defaulttoggle is kept, as Pathing keeps it
// (State/CategoryStates.cs SetInactive): turning a category back to its default forgets the
// choice, so a pack update that changes the default is followed.
class CategoryStates
{
public:
    // Whether the category `id` is on, `fallback` being its pack's default.
    bool enabled(const std::string& id, bool fallback) const;

    // `fallback` is the pack's default, as for enabled().
    void set(const std::string& id, bool enabled, bool fallback);

    // Keeps a choice made elsewhere — in Pathing, imported — as it was made, whatever the pack's
    // default: the pack may not be loaded to say. set() forgets it once it equals the default.
    void remember(const std::string& id, bool enabled);

    // Moves whenever a state changes, so whoever derives visibility from these knows to
    // derive it again.
    uint64_t revision() const { return revision_; }

    // A missing or unreadable file leaves no choices made.
    void load(const std::filesystem::path& file);
    // The choices as the file holds them, and that written to `file`.
    std::string text() const;
    bool save(const std::filesystem::path& file) const;

private:
    std::unordered_map<std::string, bool> states_;
    uint64_t revision_ = 0;
};

// What scripts keep through Storage: one flat string-to-string store across every pack, as
// Pathing's State/KvStates.cs is. Namespacing the keys is the script API's business.
//
// The file is Pathing's kv.json layout, a plain JSON object of strings.
class KeyValues
{
public:
    // The value now stored, as KvStates.UpsertValue returns it.
    const std::string& upsert(const std::string& key, std::string value);
    std::optional<std::string> read(const std::string& key) const;
    void erase(const std::string& key);

    // Moves whenever a value changes. Pathing writes at most every five seconds rather than on
    // every change; a script may write every tick.
    uint64_t revision() const { return revision_; }

    void load(const std::filesystem::path& file);
    // The values as the file holds them, and that written to `file`.
    std::string text() const;
    bool save(const std::filesystem::path& file) const;

private:
    std::unordered_map<std::string, std::string> values_;
    uint64_t revision_ = 0;
};

} // namespace viewer::store
