#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Where the real marker packs live, shared by mpe_probe and mpe_test.
//
// The corpus is deliberately *not* checked into the repo: these are other people's packs,
// some of them hundreds of megabytes, and the whole point of testing against them is that
// they are what pack authors actually ship. The cost is that the suite needs them present
// — see corpusDir() for how it is pointed at them.
namespace corpus {

// The folder holding the packs. In order: the MPE_TEST_PACK_DIR environment variable, then
// the path baked in at configure time (MPE_TEST_PACK_DIR_DEFAULT, which CMake resolves to
// the Blish HUD markers folder). Empty when neither names a real directory.
//
// The environment wins so a pack folder elsewhere can be used without reconfiguring.
std::filesystem::path dir();

// Every .zip/.taco in dir(), sorted by name so a run is reproducible. Empty when there is
// no corpus.
std::vector<std::filesystem::path> packs();

// One pack by file name, e.g. "haskha_markers.zip". Empty when the corpus does not have
// it — not every machine has every pack, and a test should say so rather than fail.
std::filesystem::path pack(std::string_view file_name);

// An *unzipped* pack: a source checkout rather than a shipped archive. Needed on its own
// because the corpus folder holds archives, and the folder DataSource has to be shown to
// resolve to the same model as the zip built from it.
//
// From MPE_TEST_FOLDER_PACK, else the path baked in at configure time. Empty when neither
// is a real directory.
std::filesystem::path folderPack();

// What to tell the user when dir() is empty. Names the variable to set.
std::string missingMessage();

// A directory under the system temp folder that removes itself, for the tests that write
// real files. A failing assertion must not leave megabytes of rebuilt archives behind, and
// nothing a test writes belongs anywhere near the corpus itself.
class TempDir
{
public:
    explicit TempDir(const std::string& name);
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(std::string_view leaf) const { return path_ / leaf; }

private:
    std::filesystem::path path_;
};

// Writes `text` to `path` byte for byte, creating its folder first: how a test lays out a
// small pack, or a settings file, of its own.
void writeFile(const std::filesystem::path& path, std::string_view text);

} // namespace corpus
