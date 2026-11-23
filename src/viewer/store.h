#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

// What outlives a session about the packs themselves, in `<addons>/MarkerPackEditor/`.
namespace viewer::store {

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

    // Moves whenever a state changes, so whoever derives visibility from these knows to
    // derive it again.
    uint64_t revision() const { return revision_; }

    // A missing or unreadable file leaves no choices made.
    void load(const std::filesystem::path& file);
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

    // Whether anything changed since the last load or save. Pathing writes at most every
    // five seconds rather than on every change; a script may write every tick.
    bool dirty() const { return dirty_; }

    void load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file);

private:
    std::unordered_map<std::string, std::string> values_;
    bool dirty_ = false;
};

} // namespace viewer::store
