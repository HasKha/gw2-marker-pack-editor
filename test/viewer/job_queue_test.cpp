// The queue the frame hands its file work to. In the viewer's tests, which link util, since util
// has no test binary of its own.

#include <catch2/catch_test_macros.hpp>

#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "util/job_queue.h"

TEST_CASE("jobs run in the order posted, off the poster's thread", "[job_queue]") {
    util::JobQueue jobs;
    std::vector<int> ran;
    std::thread::id ran_on;
    for (int i = 0; i < 5; ++i) {
        jobs.post([&ran, &ran_on, i] {
            ran.push_back(i);
            ran_on = std::this_thread::get_id();
        });
    }
    jobs.wait();
    CHECK(ran == std::vector<int>{0, 1, 2, 3, 4});
    CHECK(ran_on != std::this_thread::get_id());
}

TEST_CASE("an answer is not ready until the job ran, and a job that threw answers nothing", "[job_queue]") {
    util::JobQueue jobs;
    std::future<int> empty;
    CHECK_FALSE(util::ready(empty));

    std::future<int> answer = jobs.ask([] { return 42; });
    std::future<int> thrown = jobs.ask([]() -> int { throw std::runtime_error("disk gone"); });
    jobs.wait();
    REQUIRE(util::ready(answer));
    CHECK(answer.get() == 42);
    REQUIRE(util::ready(thrown));
    // Taken from the frame, the throw is an answer of nothing rather than a throw out of a
    // render callback.
    CHECK(util::take(thrown) == std::nullopt);

    // The queue outlives a job that threw.
    jobs.post([] { throw std::runtime_error("again"); });
    std::future<bool> after = jobs.ask([] { return true; });
    jobs.wait();
    CHECK(after.get());
}

TEST_CASE("stopping runs what was queued, and a later post starts it again", "[job_queue]") {
    util::JobQueue jobs;
    int written = 0;
    for (int i = 0; i < 3; ++i) jobs.post([&written] { ++written; });
    jobs.stop();
    CHECK(written == 3);

    jobs.post([&written] { ++written; });
    jobs.wait();
    CHECK(written == 4);
}

TEST_CASE("what is unfinished counts the running job and those queued behind it", "[job_queue]") {
    util::JobQueue jobs;
    CHECK(jobs.unfinished() == 0);

    std::promise<void> started;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    jobs.post([&started, released] {
        started.set_value();
        released.wait();
    });
    jobs.post([] {});
    started.get_future().wait();
    CHECK(jobs.unfinished() == 2);

    release.set_value();
    jobs.wait();
    CHECK(jobs.unfinished() == 0);
}
