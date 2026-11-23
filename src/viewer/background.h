#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <thread>
#include <utility>

// One job run once on a thread of its own, whose answer is read from frames: what the viewer
// asks the web for at load. The festivals on, the maps' rectangles.
namespace viewer {

template <class T>
class Background
{
public:
    // The job's answer, or nothing when it found none. Runs off the frame, so it may block.
    using Job = std::function<std::optional<T>()>;

    Background() = default;
    ~Background() { stop(); }
    Background(const Background&) = delete;
    Background& operator=(const Background&) = delete;

    // Starts `job`, unless one was started already: the answer is written once.
    void start(Job job) {
        if (started_) return;
        started_ = true;
        worker_ = std::thread([this, job = std::move(job)] {
            std::optional<T> answer = job();
            if (answer) {
                value_ = std::move(*answer);
                ready_.store(true, std::memory_order_release);
            }
            finished_.store(true, std::memory_order_release);
        });
    }

    // Waits for the job, which its own timeouts bound.
    void stop() {
        if (worker_.joinable()) worker_.join();
    }

    // The answer, once the job has given one; null until then, and for good when it gave
    // none. Never written again once set, so it is read without a lock.
    const T* get() const { return ready_.load(std::memory_order_acquire) ? &value_ : nullptr; }

    // Whether the job has returned, with an answer or without one.
    bool finished() const { return finished_.load(std::memory_order_acquire); }

private:
    std::thread worker_;
    bool started_ = false;
    std::atomic<bool> ready_{false};
    std::atomic<bool> finished_{false};
    T value_{};
};

} // namespace viewer
