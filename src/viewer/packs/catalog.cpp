#include "viewer/packs/catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>

#include "util/json_file.h"
#include "util/path_text.h"

namespace viewer::catalog {

namespace {

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool endsWith(std::string_view text, std::string_view end) {
    return text.size() >= end.size() && text.substr(text.size() - end.size()) == end;
}

bool isWebAddress(std::string_view url) {
    const std::string lower = lowercase(std::string(url.substr(0, 8)));
    return lower.starts_with("https://") || lower.starts_with("http://");
}

// Newtonsoft, which Pathing reads the catalog with, matches property names ignoring case.
const nlohmann::json* member(const nlohmann::json& object, std::string_view key) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (lowercase(it.key()) == lowercase(std::string(key))) return &it.value();
    }
    return nullptr;
}

std::string text(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = member(object, key);
    return value && value->is_string() ? value->get<std::string>() : std::string();
}

template <class T>
T number(const nlohmann::json& object, std::string_view key) {
    const nlohmann::json* value = member(object, key);
    return value && value->is_number() ? value->get<T>() : T{};
}

} // namespace

std::optional<std::vector<Package>> parse(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_array()) return std::nullopt;
    std::vector<Package> packages;
    for (const nlohmann::json& entry : body) {
        if (!entry.is_object()) continue;
        Package package;
        package.name = text(entry, "Name");
        package.description = text(entry, "Description");
        package.download = text(entry, "Download");
        package.info = text(entry, "Info");
        package.file_name = text(entry, "FileName");
        package.categories = text(entry, "Categories");
        package.author = text(entry, "AuthorName");
        package.size_mb = number<float>(entry, "Size");
        package.total_downloads = number<int>(entry, "TotalDownloads");
        package.last_update = parseUtc(text(entry, "LastUpdate")).value_or(0);
        if (package.name.empty() || !safeFileName(package.file_name) || !isWebAddress(package.download)) continue;
        if (!isWebAddress(package.info)) package.info.clear();
        packages.push_back(std::move(package));
    }
    return packages;
}

bool safeFileName(std::string_view name) {
    if (name.empty() || name.size() > 200) return false;
    if (name.find_first_of("/\\:*?\"<>|") != std::string_view::npos) return false;
    if (name.front() == '.' || name.back() == '.' || name.back() == ' ') return false;
    if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20; })) return false;
    const std::string lower = lowercase(std::string(name));
    // Names Windows keeps for devices, whatever the extension.
    static constexpr std::string_view kDevices[] = {"con", "prn", "aux", "nul", "com", "lpt"};
    const std::string stem = util::pathText(util::pathFromText(lower).stem());
    for (std::string_view device : kDevices) {
        if (stem == device || (stem.size() == 4 && stem.starts_with(device) && std::isdigit(static_cast<unsigned char>(stem[3]))))
            return false;
    }
    return endsWith(lower, ".zip") || endsWith(lower, ".taco");
}

std::vector<LocalPack> localPacks(const std::vector<std::filesystem::path>& paths) {
    std::vector<LocalPack> locals;
    for (const std::filesystem::path& path : paths) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) continue;
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, ec);
        if (ec) continue;
        LocalPack local;
        local.path = path;
        local.written = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::clock_cast<std::chrono::system_clock>(written).time_since_epoch())
                            .count();
        locals.push_back(std::move(local));
    }
    return locals;
}

const LocalPack* findLocal(const Package& package, const std::vector<LocalPack>& locals) {
    const std::string stem = lowercase(util::pathText(util::pathFromText(package.file_name).stem()));
    for (const LocalPack& local : locals) {
        if (lowercase(util::pathText(local.path.stem())) == stem) return &local;
    }
    return nullptr;
}

const Package* findPackage(const LocalPack& local, const std::vector<Package>& packages) {
    const std::string stem = lowercase(util::pathText(local.path.stem()));
    for (const Package& package : packages) {
        if (lowercase(util::pathText(util::pathFromText(package.file_name).stem())) == stem) return &package;
    }
    return nullptr;
}

State state(const Package& package, const LocalPack* local) {
    if (!local) return State::Absent;
    return package.last_update > local->written ? State::Stale : State::Current;
}

void AutoUpdate::set(const std::string& package, bool on) {
    if (on) {
        off_.erase(package);
    } else {
        off_.insert(package);
    }
}

void AutoUpdate::load(const std::filesystem::path& file) {
    off_.clear();
    const std::optional<nlohmann::json> parsed = util::json_file::readObject(file);
    if (!parsed) return;
    const auto it = parsed->find("not_kept_updated");
    if (it == parsed->end() || !it->is_array()) return;
    for (const nlohmann::json& name : *it) {
        if (name.is_string()) off_.insert(name.get<std::string>());
    }
}

bool AutoUpdate::save(const std::filesystem::path& file) const {
    nlohmann::json out;
    out["not_kept_updated"] = off_;
    return util::json_file::write(file, out);
}

} // namespace viewer::catalog
