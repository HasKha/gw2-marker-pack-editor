#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pack::zip {

// Normalizes a resource path the way TmfLib's AsyncSafeZipArchive does:
//   backslashes to forward slashes, collapse doubled slashes, lowercase, trim
//   surrounding whitespace, strip leading dots.
// Blish HUD resolves every icon/texture/traildata reference through this, so a pack can
// spell the same file three different ways and still load. We have to match it or icons
// that work in Blish will silently go missing here.
std::string normalizePath(std::string_view path);

// A marker pack archive opened for reading. Entry names are normalized on open, so
// lookups are case- and separator-insensitive.
class Archive
{
public:
    Archive() = default;
    ~Archive();

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    bool open(const std::filesystem::path& file, std::string* error);
    void close();
    bool isOpen() const { return open_; }

    // Normalized names of every file entry, in archive order.
    const std::vector<std::string>& entries() const { return entries_; }

    // Normalized names ending in the given extension, e.g. ".xml". Extension must be
    // lowercase.
    std::vector<std::string> entriesWithExtension(std::string_view extension) const;

    bool contains(std::string_view path) const;

    // Reads an entry whole. path is normalized internally.
    bool read(std::string_view path, std::vector<uint8_t>* out, std::string* error) const;
    bool readText(std::string_view path, std::string* out, std::string* error) const;

    const std::filesystem::path& sourceFile() const { return source_; }

private:
    // mz_zip_archive is an incomplete type here on purpose: miniz.h pulls in a lot and
    // every consumer of this header would otherwise pay for it.
    struct Impl;

    Impl* impl_ = nullptr;
    bool open_ = false;
    std::filesystem::path source_;
    std::vector<std::string> entries_;
    std::unordered_map<std::string, uint32_t> index_; // normalized name -> entry index
};

} // namespace pack::zip
