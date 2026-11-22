#include "viewer/store.h"

#include "viewer/json_file.h"

namespace viewer::store {

bool CategoryStates::enabled(const std::string& id, bool fallback) const {
    const auto it = states_.find(id);
    return it == states_.end() ? fallback : it->second;
}

void CategoryStates::set(const std::string& id, bool enabled, bool fallback) {
    if (this->enabled(id, fallback) == enabled) return;

    if (enabled == fallback) {
        states_.erase(id);
    } else {
        states_[id] = enabled;
    }
    ++revision_;
}

void CategoryStates::load(const std::filesystem::path& file) {
    states_.clear();
    ++revision_;

    const std::optional<nlohmann::json> parsed = json_file::readObject(file);
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
    return json_file::write(file, out);
}

} // namespace viewer::store
