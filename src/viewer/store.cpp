#include "viewer/store.h"

#include "util/json_file.h"

namespace viewer::store {

bool CategoryStates::enabled(const std::string& id, bool fallback) const {
    const auto it = states_.find(id);
    return it == states_.end() ? fallback : it->second;
}

void CategoryStates::set(const std::string& id, bool enabled, bool fallback) {
    const auto it = states_.find(id);
    if (enabled == fallback) {
        // Forgotten even when it shows no different: a remembered choice equal to the default.
        if (it == states_.end()) return;
        states_.erase(it);
    } else {
        if (it != states_.end() && it->second == enabled) return;
        states_[id] = enabled;
    }
    ++revision_;
}

void CategoryStates::remember(const std::string& id, bool enabled) {
    states_[id] = enabled;
    ++revision_;
}

void CategoryStates::load(const std::filesystem::path& file) {
    states_.clear();
    ++revision_;

    const std::optional<nlohmann::json> parsed = util::json_file::readObject(file);
    if (!parsed) return;
    const auto categories = parsed->find("categories");
    if (categories == parsed->end() || !categories->is_object()) return;

    for (const auto& [id, value] : categories->items()) {
        if (value.is_boolean()) states_.emplace(id, value.get<bool>());
    }
}

bool CategoryStates::save(const std::filesystem::path& file) const {
    nlohmann::json out;
    out["categories"] = nlohmann::json::object();
    for (const auto& [id, enabled] : states_) out["categories"][id] = enabled;
    return util::json_file::write(file, out);
}

const std::string& KeyValues::upsert(const std::string& key, std::string value) {
    auto [it, inserted] = values_.try_emplace(key);
    if (inserted || it->second != value) {
        it->second = std::move(value);
        dirty_ = true;
    }
    return it->second;
}

std::optional<std::string> KeyValues::read(const std::string& key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

void KeyValues::erase(const std::string& key) {
    if (values_.erase(key) > 0) dirty_ = true;
}

void KeyValues::load(const std::filesystem::path& file) {
    values_.clear();
    dirty_ = false;

    const std::optional<nlohmann::json> parsed = util::json_file::readObject(file);
    if (!parsed) return;
    for (const auto& [key, value] : parsed->items()) {
        if (value.is_string()) values_.emplace(key, value.get<std::string>());
    }
}

bool KeyValues::save(const std::filesystem::path& file) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [key, value] : values_) out[key] = value;
    if (!util::json_file::write(file, out)) return false;
    dirty_ = false;
    return true;
}

} // namespace viewer::store
