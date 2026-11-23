#include "viewer/blish_import.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>

#include "util/json_file.h"
#include "util/path_text.h"
#include "viewer/registry.h"

namespace viewer::blish_import {

namespace {

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Every line of `file`, without its line ending; nothing when there is no such file.
std::vector<std::string> lines(const std::filesystem::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

bool isBlank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c); });
}

} // namespace

std::vector<std::pair<std::string, bool>> readCategoryChoices(const std::filesystem::path& states_folder) {
    // By id, so a category both files name ends as the later one says, as Pathing's dictionary does.
    std::map<std::string, bool> choices;
    std::error_code ec;
    const std::filesystem::path unified = states_folder / "category_preferences.txt";
    if (std::filesystem::exists(unified, ec)) {
        for (const std::string& line : lines(unified)) {
            if (line.size() < 2 || isBlank(line)) continue;
            choices[lowercase(line.substr(1))] = line.front() == '+';
        }
    } else {
        for (const std::string& line : lines(states_folder / "categories.txt")) {
            if (!isBlank(line)) choices[lowercase(line)] = false;
        }
        for (const std::string& line : lines(states_folder / "invcategories.txt")) {
            if (!isBlank(line)) choices[lowercase(line)] = true;
        }
    }
    return {choices.begin(), choices.end()};
}

Found find(const std::filesystem::path& markers) {
    Found found;
    std::error_code ec;
    if (markers.empty() || !std::filesystem::is_directory(markers, ec)) return found;

    // Archives only: the folder itself is a pack when it holds loose XML, and that is Blish
    // HUD's folder, not a pack of ours to copy.
    for (const std::filesystem::path& pack : registry::discoverPacks(markers)) {
        if (std::filesystem::is_regular_file(pack, ec)) found.packs.push_back(pack);
    }

    const std::filesystem::path data = markers / "data";
    found.categories = readCategoryChoices(data / "states");

    if (const std::optional<nlohmann::json> kv = util::json_file::readObject(data / "kv" / "kv.json")) {
        for (const auto& [key, value] : kv->items()) {
            if (value.is_string()) found.storage.emplace_back(key, value.get<std::string>());
        }
    }

    const std::filesystem::path timers = data / "states" / "timers.txt";
    if (std::filesystem::is_regular_file(timers, ec)) found.timers = timers;
    return found;
}

Copied copyPacks(const std::vector<std::filesystem::path>& packs, const std::filesystem::path& folder) {
    Copied result;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        result.errors.push_back("could not make " + util::pathText(folder) + ": " + ec.message());
        return result;
    }

    // Archives are found at any depth and copied by file name, so two in different folders may
    // want the same one: the first has it, and the others are told apart as errors rather than
    // counted as kept or written over it.
    std::set<std::string> targets;
    for (const std::filesystem::path& source : packs) {
        const std::filesystem::path target = folder / source.filename();
        if (!targets.insert(lowercase(util::pathText(source.filename()))).second) {
            result.errors.push_back(util::pathText(source) + ": another pack of that name is imported already");
            continue;
        }

        std::error_code ours_ec;
        std::error_code theirs_ec;
        const std::filesystem::file_time_type ours = std::filesystem::last_write_time(target, ours_ec);
        const std::filesystem::file_time_type theirs = std::filesystem::last_write_time(source, theirs_ec);
        if (!ours_ec && !theirs_ec && ours >= theirs) {
            ++result.kept;
            continue;
        }

        std::filesystem::path part = target;
        part += ".part";
        ec.clear();
        std::filesystem::copy_file(source, part, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) std::filesystem::rename(part, target, ec);
        if (ec) {
            result.errors.push_back(util::pathText(source.filename()) + ": " + ec.message());
            std::filesystem::remove(part, ec);
            continue;
        }
        ++result.copied;
    }
    return result;
}

} // namespace viewer::blish_import
