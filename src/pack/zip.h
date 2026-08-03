#pragma once

#include <cstdint>
#include <filesystem>
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
    Archive() = default;
    ~Archive() override;

    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    bool open(const std::filesystem::path& file, std::string* error);
    void close();
    bool isOpen() const { return open_; }

    const std::vector<std::string>& entries() const override { return entries_; }
    bool contains(std::string_view path) const override;
    bool read(std::string_view path, std::vector<uint8_t>* out, std::string* error) const override;
    const std::filesystem::path& root() const override { return source_; }
    bool isDirectory() const override { return false; }

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
