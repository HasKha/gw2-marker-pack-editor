#pragma once

#include <cstdint>
#include <filesystem>
#include <future>

#include "util/job_queue.h"
#include "util/json_file.h"

namespace util {

// One file kept between sessions, written on a worker whenever its store's revision moves.
// A write that fails is handed over again at the next save.
class KeptFile
{
public:
    // What the store holds at `revision` is what the file holds: it was just read from it.
    void loaded(uint64_t revision) { handed_ = revision; }

    // Hands `text()` to `jobs` to be written to `file` when the store moved past what was handed
    // over last. Writes of one file land in order, so the last handed over is what stays.
    template <class Text>
    void save(JobQueue* jobs, const std::filesystem::path& file, uint64_t revision, const Text& text) {
        if (ready(written_) && !take(written_).value_or(false)) handed_ = kNothing;
        if (file.empty() || revision == handed_) return;
        handed_ = revision;
        written_ = jobs->ask([file, text = text()] { return json_file::writeText(file, text); });
    }

private:
    static constexpr uint64_t kNothing = ~0ull;
    uint64_t handed_ = kNothing;
    std::future<bool> written_; // the last handed over
};

} // namespace util
