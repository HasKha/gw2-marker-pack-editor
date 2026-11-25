#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

// Work the render thread must not wait on, run in order on one thread of its own: reading and
// writing files, above all. A disk usually answers in microseconds and now and then takes a
// tenth of a second, and a frame that waited for it is a stutter.
namespace util {

class JobQueue
{
public:
    JobQueue() = default;
    ~JobQueue() { stop(); }
    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    // Runs `job` on the worker after everything posted before it, so two writes of one file
    // land in the order they were posted. Starts the worker if it is not running.
    void post(std::function<void()> job);

    // post() for a job with an answer, taken by the frame with take() once ready() says it is
    // there.
    template <class Job>
    auto ask(Job job) -> std::future<decltype(job())> {
        using T = decltype(job());
        auto task = std::make_shared<std::packaged_task<T()>>(std::move(job));
        std::future<T> answer = task->get_future();
        post([task] { (*task)(); });
        return answer;
    }

    // Blocks until everything posted so far has run. Never from a frame: for tests, and for
    // whoever needs a file written before it goes on.
    void wait();

    // Runs everything posted, then ends the thread: a write queued at unload still lands. A
    // later post() starts it again.
    void stop();

    // How many jobs have not finished: those queued and the one running.
    size_t unfinished() const;

private:
    void work();

    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::function<void()>> queued_;
    bool running_job_ = false;
    bool stopping_ = false;
};

// Whether `answer` holds an answer now, without waiting for one. False for an empty future.
template <class T>
bool ready(const std::future<T>& answer) {
    return answer.valid() && answer.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// The answer `answer` holds, which ready() said is there, or nothing when its job threw: get()
// would throw it again on the frame, out of a render callback, which ends the game.
template <class T>
std::optional<T> take(std::future<T>& answer) {
    try {
        return answer.get();
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace util
