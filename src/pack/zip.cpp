#include "pack/zip.h"

#include <miniz.h>

#include <cstdio>
#include <mutex>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <fcntl.h>
    #include <io.h>
    #include <windows.h>
#endif

#include "util/path_text.h"

namespace pack::zip {

struct Archive::Impl {
    mz_zip_archive zip{};
    // miniz keeps its place in the file and its last error in `zip`, so two reads at once
    // would corrupt each other: an archive is read on a worker and, in the editor, on the
    // frame too.
    std::mutex reading;
    // Ours to close: miniz leaves a file it was handed open.
    FILE* file = nullptr;
};

namespace {

// Opens `path` for reading so that it can be moved, replaced or deleted while it stays open: a
// pack drawn from its archive is read from as its icons come into view, and a save elsewhere
// replaces the archive by moving it aside (commitZip). fopen does not share deletion, which is
// what Windows asks of every handle before it moves a file. The handle keeps reading the file
// it opened, wherever that file went.
FILE* openShared(const std::filesystem::path& path) {
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (descriptor == -1) {
        CloseHandle(handle);
        return nullptr;
    }
    FILE* file = _fdopen(descriptor, "rb");
    if (!file) _close(descriptor);
    return file;
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

} // namespace

Archive::Archive() = default;

Archive::~Archive() {
    close();
}

bool Archive::open(const std::filesystem::path& file, std::string* error) {
    close();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        if (error) *error = "Not a file: " + util::pathText(file);
        return false;
    }

    impl = std::make_unique<Impl>();
    impl->file = openShared(file);
    if (!impl->file) {
        if (error) *error = "Could not open " + util::pathText(file);
        impl.reset();
        return false;
    }
    const uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec || !mz_zip_reader_init_cfile(&impl->zip, impl->file, static_cast<mz_uint64>(size), 0)) {
        if (error) *error = "Could not open archive: " + std::string(mz_zip_get_error_string(mz_zip_get_last_error(&impl->zip)));
        std::fclose(impl->file);
        impl.reset();
        return false;
    }

    opened = true;
    source = file;

    const mz_uint count = mz_zip_reader_get_num_files(&impl->zip);
    entry_names.reserve(count);

    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&impl->zip, i)) continue;

        // Length first, then the copy. miniz truncates into whatever buffer it is given and
        // reports the truncated length, so a fixed buffer would turn an over-long path into
        // a different entry — one a save would then write back under the wrong name. With a
        // zero size it copies nothing and returns the real length (plus the terminator).
        const mz_uint length = mz_zip_reader_get_filename(&impl->zip, i, nullptr, 0);
        if (length <= 1) continue; // 0 is an error, 1 is an empty name

        std::string name(length, '\0');
        const mz_uint written = mz_zip_reader_get_filename(&impl->zip, i, name.data(), length);
        if (written == 0) continue;
        name.resize(written - 1);

        std::string normalized = normalizePath(name);
        if (normalized.empty()) continue;

        original_names.emplace(normalized, name);
        entry_names.push_back(normalized);
        // First entry wins on a duplicate name, matching how a reader scanning forward
        // would resolve it.
        index.emplace(std::move(normalized), static_cast<uint32_t>(i));
    }

    return true;
}

std::string Archive::originalName(std::string_view path) const {
    auto it = original_names.find(normalizePath(path));
    return it == original_names.end() ? std::string() : it->second;
}

void Archive::close() {
    if (impl) {
        if (opened) mz_zip_reader_end(&impl->zip);
        if (impl->file) std::fclose(impl->file);
        impl.reset();
    }
    opened = false;
    source.clear();
    entry_names.clear();
    index.clear();
    original_names.clear();
}

bool Archive::contains(std::string_view path) const {
    return index.find(normalizePath(path)) != index.end();
}

bool Archive::read(std::string_view path, std::vector<uint8_t>* out, std::string* error) const {
    if (!opened) {
        if (error) *error = "Archive is not open";
        return false;
    }

    auto it = index.find(normalizePath(path));
    if (it == index.end()) {
        if (error) *error = "No such entry: " + std::string(path);
        return false;
    }

    const std::lock_guard<std::mutex> lock(impl->reading);
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&impl->zip, it->second, &stat)) {
        if (error) *error = "Could not stat entry: " + std::string(path);
        return false;
    }

    out->resize(static_cast<size_t>(stat.m_uncomp_size));
    if (stat.m_uncomp_size == 0) return true;

    if (!mz_zip_reader_extract_to_mem(&impl->zip, it->second, out->data(), out->size(), 0)) {
        if (error) {
            *error = "Could not extract " + std::string(path) + ": " + mz_zip_get_error_string(mz_zip_get_last_error(&impl->zip));
        }
        out->clear();
        return false;
    }

    return true;
}

} // namespace pack::zip
