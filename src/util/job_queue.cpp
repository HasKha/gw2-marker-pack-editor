#include "util/job_queue.h"

namespace util {

void JobQueue::post(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(mutex_);
    queued_.push_back(std::move(job));
    if (!worker_.joinable()) {
        stopping_ = false;
        worker_ = std::thread([this] { work(); });
    }
    wake_.notify_one();
}

void JobQueue::wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queued_.empty() && !running_job_; });
}

void JobQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void JobQueue::work() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stopping_ || !queued_.empty(); });
        if (queued_.empty()) return; // stopping, with everything run

        std::function<void()> job = std::move(queued_.front());
        queued_.pop_front();
        running_job_ = true;
        lock.unlock();
        // A job that throws loses its own work, not the queue's: an exception escaping the
        // thread would end the game. ask()'s jobs hand theirs to the future, for take().
        try {
            job();
        } catch (...) {
        }
        lock.lock();
        running_job_ = false;
        if (queued_.empty()) idle_.notify_all();
    }
}

} // namespace util
