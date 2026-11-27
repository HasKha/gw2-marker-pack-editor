#include "editor/file_dialog.h"

namespace file_dialog {

namespace {

std::vector<nfdu8filteritem_t> toNfdFilters(const std::vector<Filter>& filters) {
    std::vector<nfdu8filteritem_t> out;
    out.reserve(filters.size());
    for (const Filter& filter : filters) {
        out.push_back(nfdu8filteritem_t{filter.name.c_str(), filter.spec.c_str()});
    }
    return out;
}

} // namespace

// Single entry point for every dialog kind: spawns the worker, runs fn on it, stores
// the outcome. Keeping this in one place is what stops the three request* functions
// from being three copies of the same threading dance.
void FileDialog::spawn(DialogFn fn) {
    bool expected = false;
    if (!pending.compare_exchange_strong(expected, true)) return;

    {
        std::lock_guard lock(mutex);
        if (worker.joinable()) worker.join(); // previous dialog already finished
        result.reset();
        worker = std::thread([this, fn = std::move(fn)] {
            std::optional<std::filesystem::path> picked;
            if (NFD_Init() == NFD_OKAY) {
                nfdu8char_t* out_path = nullptr;
                if (fn(&out_path) == NFD_OKAY && out_path) {
                    picked = std::filesystem::path(reinterpret_cast<const char8_t*>(out_path));
                    NFD_FreePathU8(out_path);
                }
                NFD_Quit();
            }
            {
                std::lock_guard lock(mutex);
                result = std::move(picked);
            }
            pending.store(false);
        });
    }
}

void FileDialog::requestOpenFile(const std::filesystem::path& default_folder, std::vector<Filter> filters) {
    spawn([default_folder = default_folder.u8string(), filters = std::move(filters)](nfdu8char_t** out) {
        std::vector<nfdu8filteritem_t> nfd_filters = toNfdFilters(filters);
        nfdopendialogu8args_t args{};
        args.filterList = nfd_filters.empty() ? nullptr : nfd_filters.data();
        args.filterCount = static_cast<nfdfiltersize_t>(nfd_filters.size());
        args.defaultPath = default_folder.empty() ? nullptr : reinterpret_cast<const nfdu8char_t*>(default_folder.c_str());
        return NFD_OpenDialogU8_With(out, &args);
    });
}

void FileDialog::requestOpenFolder(const std::filesystem::path& default_folder) {
    spawn([default_folder = default_folder.u8string()](nfdu8char_t** out) {
        nfdpickfolderu8args_t args{};
        args.defaultPath = default_folder.empty() ? nullptr : reinterpret_cast<const nfdu8char_t*>(default_folder.c_str());
        return NFD_PickFolderU8_With(out, &args);
    });
}

void FileDialog::requestSaveFile(const std::filesystem::path& default_folder, std::string_view default_name, std::vector<Filter> filters) {
    spawn([default_folder = default_folder.u8string(), default_name = std::string(default_name), filters = std::move(filters)](
              nfdu8char_t** out) {
        std::vector<nfdu8filteritem_t> nfd_filters = toNfdFilters(filters);
        nfdsavedialogu8args_t args{};
        args.filterList = nfd_filters.empty() ? nullptr : nfd_filters.data();
        args.filterCount = static_cast<nfdfiltersize_t>(nfd_filters.size());
        args.defaultPath = default_folder.empty() ? nullptr : reinterpret_cast<const nfdu8char_t*>(default_folder.c_str());
        args.defaultName = default_name.empty() ? nullptr : default_name.c_str();
        return NFD_SaveDialogU8_With(out, &args);
    });
}

std::optional<std::filesystem::path> FileDialog::takeResult() {
    if (pending.load()) return std::nullopt;
    std::lock_guard lock(mutex);
    std::optional<std::filesystem::path> out = std::move(result);
    result.reset();
    return out;
}

FileDialog::~FileDialog() {
    // The worker takes `mutex` to store its result on the way out, so joining it while
    // holding that lock deadlocks — and unload with a dialog still open is exactly when
    // this runs. Take the thread out under the lock, then join with the lock released.
    std::thread finishing;
    {
        std::lock_guard lock(mutex);
        finishing = std::move(worker);
    }

    if (finishing.joinable()) finishing.join();
}

} // namespace file_dialog
