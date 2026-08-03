#include "pack/zip.h"

#include <miniz.h>

namespace pack::zip {

struct Archive::Impl {
    mz_zip_archive zip{};
};

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

} // namespace pack::zip
