#include "pack/zip.h"

#include <miniz.h>

namespace pack::zip {

struct Archive::Impl {
    mz_zip_archive zip{};
};

Archive::Archive() = default;

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

    impl_ = std::make_unique<Impl>();
    if (!mz_zip_reader_init_file(&impl_->zip, file.string().c_str(), 0)) {
        if (error) *error = "Could not open archive: " + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip)));
        impl_.reset();
        return false;
    }

    open_ = true;
    source_ = file;

    const mz_uint count = mz_zip_reader_get_num_files(&impl_->zip);
    entries_.reserve(count);

    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&impl_->zip, i)) continue;

        // Length first, then the copy. miniz truncates into whatever buffer it is given and
        // reports the truncated length, so a fixed buffer would turn an over-long path into
        // a different entry — one a save would then write back under the wrong name. With a
        // zero size it copies nothing and returns the real length (plus the terminator).
        const mz_uint length = mz_zip_reader_get_filename(&impl_->zip, i, nullptr, 0);
        if (length <= 1) continue; // 0 is an error, 1 is an empty name

        std::string name(length, '\0');
        const mz_uint written = mz_zip_reader_get_filename(&impl_->zip, i, name.data(), length);
        if (written == 0) continue;
        name.resize(written - 1);

        std::string normalized = normalizePath(name);
        if (normalized.empty()) continue;

        original_names_.emplace(normalized, name);
        entries_.push_back(normalized);
        // First entry wins on a duplicate name, matching how a reader scanning forward
        // would resolve it.
        index_.emplace(std::move(normalized), static_cast<uint32_t>(i));
    }

    return true;
}

std::string Archive::originalName(std::string_view path) const {
    auto it = original_names_.find(normalizePath(path));
    return it == original_names_.end() ? std::string() : it->second;
}

void Archive::close() {
    if (impl_) {
        if (open_) mz_zip_reader_end(&impl_->zip);
        impl_.reset();
    }
    open_ = false;
    source_.clear();
    entries_.clear();
    index_.clear();
    original_names_.clear();
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
