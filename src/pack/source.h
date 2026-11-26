#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pack {

// Normalizes a resource path the way TmfLib's AsyncSafeZipArchive does:
//   backslashes to forward slashes, collapse doubled slashes, lowercase, trim
//   surrounding whitespace, strip leading dots.
// Blish HUD resolves every icon/texture/traildata reference through this, so a pack can
// spell the same file three different ways and still load. We have to match it or icons
// that work in Blish will silently go missing here.
//
// Applied to directory sources too, so a folder and the zip built from it behave the same.
std::string normalizePath(std::string_view path);

// Read-only access to a pack's files, whether it lives in a zip or as a folder on disk.
// TmfLib draws the same line with IDataReader / ZipArchiveReader / DirectoryReader.
class DataSource
{
public:
    virtual ~DataSource() = default;

    // Normalized names of every file, in no guaranteed order.
    virtual const std::vector<std::string>& entries() const = 0;

    // The entry's name with its original spelling and case. Lookups are normalized, but a
    // save has to put the file back under the name the pack actually used — rewriting
    // every path to lowercase would be a gratuitous diff. Empty if not found.
    virtual std::string originalName(std::string_view path) const = 0;
    virtual bool contains(std::string_view path) const = 0;
    // Safe from several threads at once, as everything const here is.
    [[nodiscard]] virtual bool read(std::string_view path, std::vector<uint8_t>* out, std::string* error) const = 0;

    // Where the pack came from: the .zip file, or the folder.
    virtual const std::filesystem::path& root() const = 0;

    // True for a folder. Matters on save: a folder is written back in place, a zip is
    // rebuilt. It also matters for loading — Blish HUD only reliably loads zipped packs,
    // so a folder is an editing convenience, not a shippable pack.
    virtual bool isDirectory() const = 0;

    [[nodiscard]] bool readText(std::string_view path, std::string* out, std::string* error) const;

    // Normalized names ending in the given lowercase extension, e.g. ".xml".
    std::vector<std::string> entriesWithExtension(std::string_view extension) const;
};

// Opens a .zip/.taco archive.
std::unique_ptr<DataSource> openZip(const std::filesystem::path& file, std::string* error);

// Opens an unzipped pack folder. Skips dot-directories, so pointing this at a git
// checkout does not drag in .git.
std::unique_ptr<DataSource> openDirectory(const std::filesystem::path& folder, std::string* error);

// Picks by what the path actually is.
std::unique_ptr<DataSource> open(const std::filesystem::path& path, std::string* error);

} // namespace pack
