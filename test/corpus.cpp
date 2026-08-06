#include "corpus.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifndef MPE_TEST_PACK_DIR_DEFAULT
    #define MPE_TEST_PACK_DIR_DEFAULT ""
#endif

#ifndef MPE_TEST_FOLDER_PACK_DEFAULT
    #define MPE_TEST_FOLDER_PACK_DEFAULT ""
#endif

namespace corpus {

namespace {

constexpr const char* kEnvironmentVariable = "MPE_TEST_PACK_DIR";
constexpr const char* kFolderEnvironmentVariable = "MPE_TEST_FOLDER_PACK";

// Environment first so a folder elsewhere can be used without reconfiguring; a variable
// that is set but wrong resolves to nothing rather than silently falling back.
std::filesystem::path resolveDirectory(const char* variable, const char* baked) {
    std::error_code ec;

    if (const char* override_path = std::getenv(variable)) {
        const std::filesystem::path candidate(override_path);
        return std::filesystem::is_directory(candidate, ec) ? candidate : std::filesystem::path{};
    }

    const std::filesystem::path fallback(baked);
    if (!fallback.empty() && std::filesystem::is_directory(fallback, ec)) return fallback;

    return {};
}

bool isPackFile(const std::filesystem::path& path) {
    // .taco is a zip under a different name; both are read by the same archive reader.
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension == ".zip" || extension == ".taco";
}

} // namespace

std::filesystem::path dir() {
    // Resolved once: it cannot change under a running process, and every test asks.
    static const std::filesystem::path resolved = resolveDirectory(kEnvironmentVariable, MPE_TEST_PACK_DIR_DEFAULT);
    return resolved;
}

std::filesystem::path folderPack() {
    static const std::filesystem::path resolved = resolveDirectory(kFolderEnvironmentVariable, MPE_TEST_FOLDER_PACK_DEFAULT);
    return resolved;
}

std::vector<std::filesystem::path> packs() {
    std::vector<std::filesystem::path> found;

    const std::filesystem::path root = dir();
    if (root.empty()) return found;

    std::error_code ec;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_regular_file(ec) && isPackFile(entry.path())) found.push_back(entry.path());
    }

    // Directory order is not guaranteed stable, and a test run should be reproducible.
    std::sort(found.begin(), found.end());
    return found;
}

std::filesystem::path pack(std::string_view file_name) {
    const std::filesystem::path root = dir();
    if (root.empty()) return {};

    const std::filesystem::path candidate = root / file_name;
    std::error_code ec;
    return std::filesystem::is_regular_file(candidate, ec) ? candidate : std::filesystem::path{};
}

std::string missingMessage() {
    std::string message = "No marker pack corpus found. Set ";
    message += kEnvironmentVariable;
    message += " to a folder holding .zip/.taco packs";

    const std::string baked = MPE_TEST_PACK_DIR_DEFAULT;
    if (!baked.empty()) message += " (tried " + baked + ")";

    return message + ".";
}

TempDir::TempDir(const std::string& name) {
    std::error_code ec;
    path_ = std::filesystem::temp_directory_path(ec) / ("mpe_test_" + name);
    // Removed first: a previous run that was killed mid-test leaves its directory behind,
    // and starting from whatever it managed to write is not a test of anything.
    std::filesystem::remove_all(path_, ec);
    std::filesystem::create_directories(path_, ec);
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

} // namespace corpus
