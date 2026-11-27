#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pack/source.h"

namespace pack::zip {

// A marker pack archive opened for reading. Entry names are normalized on open, so
// lookups are case- and separator-insensitive.
//
// Most code should go through pack::open() rather than naming this directly — a pack can
// equally be a folder on disk.
class Archive : public DataSource
{
public:
    // Both defined in the .cpp, not defaulted here: unique_ptr<Impl> needs Impl complete
    // wherever it might be destroyed, and that includes the constructor's unwind path.
    Archive();
    ~Archive() override;

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    bool open(const std::filesystem::path& file, std::string* error);
    void close();
    bool isOpen() const { return opened; }

    const std::vector<std::string>& entries() const override { return entry_names; }
    std::string originalName(std::string_view path) const override;
    bool contains(std::string_view path) const override;
    bool read(std::string_view path, std::vector<uint8_t>* out, std::string* error) const override;
    const std::filesystem::path& root() const override { return source; }
    bool isDirectory() const override { return false; }

private:
    // mz_zip_archive is an incomplete type here on purpose: miniz.h pulls in a lot and
    // every consumer of this header would otherwise pay for it. The destructor is declared
    // above and defined in the .cpp, which is what lets unique_ptr hold an incomplete type.
    struct Impl;

    std::unique_ptr<Impl> impl;
    bool opened = false;
    std::filesystem::path source;
    std::vector<std::string> entry_names;
    std::unordered_map<std::string, uint32_t> index; // normalized name -> entry index
    std::unordered_map<std::string, std::string> original_names;
};

} // namespace pack::zip
