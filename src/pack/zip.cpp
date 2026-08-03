#include "pack/zip.h"

#include <miniz.h>

#include <algorithm>
#include <cctype>

namespace pack::zip {

struct Archive::Impl {
    mz_zip_archive zip{};
};

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

Archive::~Archive() {
    close();
}

bool Archive::open(const std::filesystem::path& file, std::string* error) {
    close();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        if (error) *error = "Not a file: " + file.string();
        return false;
    }

    impl_ = new Impl();
    if (!mz_zip_reader_init_file(&impl_->zip, file.string().c_str(), 0)) {
        if (error) *error = "Could not open archive: " + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip)));
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    open_ = true;
    source_ = file;

    const mz_uint count = mz_zip_reader_get_num_files(&impl_->zip);
    entries_.reserve(count);

    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&impl_->zip, i)) continue;

        char name[512];
        const mz_uint written = mz_zip_reader_get_filename(&impl_->zip, i, name, sizeof(name));
        if (written == 0) continue;

        std::string normalized = normalizePath(std::string_view(name, written - 1));
        if (normalized.empty()) continue;

        entries_.push_back(normalized);
        // First entry wins on a duplicate name, matching how a reader scanning forward
        // would resolve it.
        index_.emplace(std::move(normalized), static_cast<uint32_t>(i));
    }

    return true;
}

void Archive::close() {
    if (impl_) {
        if (open_) mz_zip_reader_end(&impl_->zip);
        delete impl_;
        impl_ = nullptr;
    }
    open_ = false;
    source_.clear();
    entries_.clear();
    index_.clear();
}

std::vector<std::string> Archive::entriesWithExtension(std::string_view extension) const {
    std::vector<std::string> out;
    for (const std::string& entry : entries_) {
        if (entry.size() >= extension.size() && entry.compare(entry.size() - extension.size(), extension.size(), extension) == 0) {
            out.push_back(entry);
        }
    }
    return out;
}

bool Archive::contains(std::string_view path) const {
    return index_.find(normalizePath(path)) != index_.end();
}

bool Archive::read(std::string_view path, std::vector<uint8_t>* out, std::string* error) const {
    if (!open_) {
        if (error) *error = "Archive is not open";
        return false;
    }

    auto it = index_.find(normalizePath(path));
    if (it == index_.end()) {
        if (error) *error = "No such entry: " + std::string(path);
        return false;
    }

    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&impl_->zip, it->second, &stat)) {
        if (error) *error = "Could not stat entry: " + std::string(path);
        return false;
    }

    out->resize(static_cast<size_t>(stat.m_uncomp_size));
    if (stat.m_uncomp_size == 0) return true;

    if (!mz_zip_reader_extract_to_mem(&impl_->zip, it->second, out->data(), out->size(), 0)) {
        if (error) {
            *error = "Could not extract " + std::string(path) + ": " + mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip));
        }
        out->clear();
        return false;
    }

    return true;
}

bool Archive::readText(std::string_view path, std::string* out, std::string* error) const {
    std::vector<uint8_t> bytes;
    if (!read(path, &bytes, error)) return false;
    out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

} // namespace pack::zip
