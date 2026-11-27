#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <thread>
#include <utility>

#include "http/http.h"

// One job run once on a thread of its own, whose answer is read from frames: a request to the
// web, mostly, which is why the job is handed a Cancel. The viewer's festivals and maps'
// rectangles, the catalog, and the Blish HUD import, which makes no request at all.
namespace http {

template <class T>
class Background
{
public:
    // The job's answer, or nothing when it found none. Runs off the frame, so it may block; a
    // request it makes is handed the Cancel, which stop() sets.
    using Job = std::function<std::optional<T>(const http::Cancel& cancel)>;

    Background() = default;
    ~Background() { stop(); }
    Background(const Background&) = delete;
    Background& operator=(const Background&) = delete;

    // Starts `job`, unless one was started already: the answer is written once.
    void start(Job job) {
        if (started) return;
        started = true;
        worker = std::thread([this, job = std::move(job)] {
            std::optional<T> answer = job(cancel);
            if (answer) {
                value = std::move(*answer);
                ready.store(true, std::memory_order_release);
            }
            job_returned.store(true, std::memory_order_release);
        });
    }

    // Cancels the job's request, if it is waiting on one, and waits for the job. Its answer, if
    // it still gives one, is kept.
    void stop() {
        cancel.cancel();
        if (worker.joinable()) worker.join();
    }

    // The answer, once the job has given one; null until then, and for good when it gave
    // none. Never written again once set, so it is read without a lock.
    const T* get() const { return ready.load(std::memory_order_acquire) ? &value : nullptr; }

    // The answer moved out, for whoever drops the job next: what get() reads after is what is
    // left of it. Nothing until the job gave one.
    std::optional<T> take() {
        if (!ready.load(std::memory_order_acquire)) return std::nullopt;
        return std::move(value);
    }

    // Whether the job has returned, with an answer or without one.
    bool finished() const { return job_returned.load(std::memory_order_acquire); }

private:
    std::thread worker;
    http::Cancel cancel;
    bool started = false;
    std::atomic<bool> ready{false};
    std::atomic<bool> job_returned{false};
    T value{};
};

} // namespace http
