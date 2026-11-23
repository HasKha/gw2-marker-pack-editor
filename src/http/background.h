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
        if (started_) return;
        started_ = true;
        worker_ = std::thread([this, job = std::move(job)] {
            std::optional<T> answer = job(cancel_);
            if (answer) {
                value_ = std::move(*answer);
                ready_.store(true, std::memory_order_release);
            }
            finished_.store(true, std::memory_order_release);
        });
    }

    // Cancels the job's request, if it is waiting on one, and waits for the job. Its answer, if
    // it still gives one, is kept.
    void stop() {
        cancel_.cancel();
        if (worker_.joinable()) worker_.join();
    }

    // The answer, once the job has given one; null until then, and for good when it gave
    // none. Never written again once set, so it is read without a lock.
    const T* get() const { return ready_.load(std::memory_order_acquire) ? &value_ : nullptr; }

    // Whether the job has returned, with an answer or without one.
    bool finished() const { return finished_.load(std::memory_order_acquire); }

private:
    std::thread worker_;
    http::Cancel cancel_;
    bool started_ = false;
    std::atomic<bool> ready_{false};
    std::atomic<bool> finished_{false};
    T value_{};
};

} // namespace http
