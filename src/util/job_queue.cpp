#include "util/job_queue.h"

namespace util {

void JobQueue::post(std::function<void()> job) {
    std::lock_guard<std::mutex> lock(mutex);
    queued.push_back(std::move(job));
    if (!worker.joinable()) {
        stopping = false;
        worker = std::thread([this] { work(); });
    }
    wake.notify_one();
}

void JobQueue::wait() {
    std::unique_lock<std::mutex> lock(mutex);
    idle.wait(lock, [this] { return queued.empty() && !running_job; });
}

void JobQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_one();
    if (worker.joinable()) worker.join();
}

size_t JobQueue::unfinished() const {
    std::lock_guard<std::mutex> lock(mutex);
    return queued.size() + (running_job ? 1 : 0);
}

void JobQueue::work() {
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
        wake.wait(lock, [this] { return stopping || !queued.empty(); });
        if (queued.empty()) return; // stopping, with everything run

        std::function<void()> job = std::move(queued.front());
        queued.pop_front();
        running_job = true;
        lock.unlock();
        // A job that throws loses its own work, not the queue's: an exception escaping the
        // thread would end the game. ask()'s jobs hand theirs to the future, for take().
        try {
            job();
        } catch (...) {
        }
        lock.lock();
        running_job = false;
        if (queued.empty()) idle.notify_all();
    }
}

} // namespace util
