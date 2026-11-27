// Reading archive entries: deflated ones are inflated by libdeflate, stored ones read by miniz, and
// a damaged one is refused. The archives are written here by miniz, so each entry's method is known.

#include <catch2/catch_test_macros.hpp>

#include <miniz.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "corpus.h"
#include "pack/source.h"
#include "util/path_text.h"

namespace {

struct Entry {
    std::string name;
    std::vector<uint8_t> bytes;
    mz_uint level; // 0 stores it
};

void writeArchive(const std::filesystem::path& path, const std::vector<Entry>& entries) {
    mz_zip_archive zip{};
    REQUIRE(mz_zip_writer_init_file(&zip, util::pathText(path).c_str(), 0));
    for (const Entry& entry : entries) {
        REQUIRE(mz_zip_writer_add_mem(&zip, entry.name.c_str(), entry.bytes.data(), entry.bytes.size(), entry.level));
    }
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(mz_zip_writer_end(&zip));
}

// Text that compresses, as a pack's XML does, and noise that does not, as a texture's bytes do.
std::vector<uint8_t> repetitive(size_t size) {
    const std::string line = "<POI type=\"a.b\" xpos=\"1.5\" ypos=\"2.5\" zpos=\"3.5\"/>\n";
    std::vector<uint8_t> out;
    while (out.size() < size) out.insert(out.end(), line.begin(), line.end());
    out.resize(size);
    return out;
}

std::vector<uint8_t> noise(size_t size) {
    std::mt19937 engine(7);
    std::vector<uint8_t> out(size);
    for (uint8_t& byte : out) byte = static_cast<uint8_t>(engine());
    return out;
}

} // namespace

TEST_CASE("an archive's entries read back byte for byte, deflated or stored", "[zip]") {
    const corpus::TempDir temp("zip_read");
    const std::filesystem::path path = temp / "pack.zip";
    const std::vector<Entry> entries = {
        {"markers.xml", repetitive(300'000), MZ_DEFAULT_LEVEL},
        {"Data/Icon.png", noise(70'000), MZ_DEFAULT_LEVEL},
        {"stored.trl", repetitive(5'000), 0},
        {"empty.lua", {}, MZ_DEFAULT_LEVEL},
    };
    writeArchive(path, entries);

    std::string error;
    const std::unique_ptr<pack::DataSource> source = pack::openZip(path, &error);
    REQUIRE(source != nullptr);
    for (const Entry& entry : entries) {
        INFO(entry.name);
        std::vector<uint8_t> read;
        REQUIRE(source->read(entry.name, &read, &error));
        CHECK(read == entry.bytes);
    }
}

TEST_CASE("a damaged entry does not read, and says why", "[zip]") {
    const corpus::TempDir temp("zip_damaged");
    const std::filesystem::path path = temp / "pack.zip";
    const std::vector<uint8_t> bytes = repetitive(200'000);
    writeArchive(path, {{"markers.xml", bytes, MZ_DEFAULT_LEVEL}});

    // A byte in the middle of the compressed data, well past the 30-byte local header and the name.
    std::vector<char> file;
    {
        std::ifstream in(path, std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    REQUIRE(file.size() > 200);
    file[100] = static_cast<char>(file[100] ^ 0x5A);
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(file.data(), static_cast<std::streamsize>(file.size()));

    std::string error;
    const std::unique_ptr<pack::DataSource> source = pack::openZip(path, &error);
    REQUIRE(source != nullptr);
    std::vector<uint8_t> read;
    CHECK_FALSE(source->read("markers.xml", &read, &error));
    CHECK(read.empty());
    CHECK_FALSE(error.empty());
}
