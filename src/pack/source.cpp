#include "pack/source.h"

#include <cctype>
#include <fstream>
#include <unordered_map>

#include "pack/zip.h"
#include "util/path_text.h"

namespace pack {

namespace {

// A pack folder under version control is the normal case for editing, so walking into
// .git (or .vscode, or .github) would cost far more than it finds.
bool isSkippedDirectory(const std::filesystem::path& name) {
    const std::filesystem::path::string_type text = name.filename().native();
    return !text.empty() && text.front() == '.';
}

class DirectorySource : public DataSource
{
public:
    bool open(const std::filesystem::path& folder, std::string* error) {
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec)) {
            if (error) *error = "Not a folder: " + util::pathText(folder);
            return false;
        }

        root_folder = std::filesystem::canonical(folder, ec);
        if (ec) root_folder = folder;

        std::filesystem::recursive_directory_iterator it(root_folder, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) {
            if (error) *error = "Could not read folder: " + ec.message();
            return false;
        }

        for (std::filesystem::recursive_directory_iterator end; it != end; it.increment(ec)) {
            if (ec) break;

            if (it->is_directory(ec)) {
                if (isSkippedDirectory(it->path())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;

            const std::filesystem::path relative = std::filesystem::relative(it->path(), root_folder, ec);
            if (ec) continue;

            std::string normalized = normalizePath(relative.generic_string());
            if (normalized.empty()) continue;

            original_names.emplace(normalized, relative.generic_string());
            entry_names.push_back(normalized);
            index.emplace(std::move(normalized), it->path());
        }

        return true;
    }

    const std::vector<std::string>& entries() const override { return entry_names; }

    std::string originalName(std::string_view path) const override {
        auto it = original_names.find(normalizePath(path));
        return it == original_names.end() ? std::string() : it->second;
    }

    bool contains(std::string_view path) const override { return index.find(normalizePath(path)) != index.end(); }

    bool read(std::string_view path, std::vector<uint8_t>* out, std::string* error) const override {
        auto it = index.find(normalizePath(path));
        if (it == index.end()) {
            if (error) *error = "No such file: " + std::string(path);
            return false;
        }

        std::ifstream file(it->second, std::ios::binary | std::ios::ate);
        if (!file) {
            if (error) *error = "Could not open " + util::pathText(it->second);
            return false;
        }

        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        out->resize(static_cast<size_t>(size));
        if (size > 0 && !file.read(reinterpret_cast<char*>(out->data()), size)) {
            if (error) *error = "Could not read " + util::pathText(it->second);
            out->clear();
            return false;
        }

        return true;
    }

    const std::filesystem::path& root() const override { return root_folder; }
    bool isDirectory() const override { return true; }

private:
    std::filesystem::path root_folder;
    std::vector<std::string> entry_names;
    std::unordered_map<std::string, std::filesystem::path> index;
    std::unordered_map<std::string, std::string> original_names;
};

} // namespace

std::string normalizePath(std::string_view path) {
    std::string out;
    out.reserve(path.size());

    for (char c : path) {
        char normalized = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        // Collapse doubled slashes as we go rather than in a second pass.
        if (normalized == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(normalized);
    }

    auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    size_t begin = 0;
    size_t end = out.size();
    while (begin < end && is_space(out[begin])) ++begin;
    while (end > begin && is_space(out[end - 1])) --end;
    out = out.substr(begin, end - begin);

    size_t dots = 0;
    while (dots < out.size() && out[dots] == '.') ++dots;
    out.erase(0, dots);

    return out;
}

bool DataSource::readText(std::string_view path, std::string* out, std::string* error) const {
    std::vector<uint8_t> bytes;
    if (!read(path, &bytes, error)) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

std::vector<std::string> DataSource::entriesWithExtension(std::string_view extension) const {
    std::vector<std::string> out;
    for (const std::string& entry : entries()) {
        if (entry.size() >= extension.size() && entry.compare(entry.size() - extension.size(), extension.size(), extension) == 0) {
            out.push_back(entry);
        }
    }
    return out;
}

std::unique_ptr<DataSource> openZip(const std::filesystem::path& file, std::string* error) {
    auto archive = std::make_unique<zip::Archive>();
    if (!archive->open(file, error)) return nullptr;
    return archive;
}

std::unique_ptr<DataSource> openDirectory(const std::filesystem::path& folder, std::string* error) {
    auto source = std::make_unique<DirectorySource>();
    if (!source->open(folder, error)) return nullptr;
    return source;
}

std::unique_ptr<DataSource> open(const std::filesystem::path& path, std::string* error) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return openDirectory(path, error);
    return openZip(path, error);
}

} // namespace pack
