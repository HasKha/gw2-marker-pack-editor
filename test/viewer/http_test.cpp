// Requests that end when told to: the addon unloading must not wait out a server's silence.

#include <catch2/catch_test_macros.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <thread>

#include "viewer/background.h"
#include "viewer/http.h"

namespace {

// A server on localhost that takes connections and never says a word: what a request waiting
// on a stalled server looks like, without the web.
class SilentServer
{
public:
    SilentServer() {
        WSADATA data{};
        started_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0; // any free port
        bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        listen(socket_, 4);
        int length = sizeof(address);
        getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
    }
    ~SilentServer() {
        closesocket(socket_);
        if (started_) WSACleanup();
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/"; }

private:
    bool started_ = false;
    SOCKET socket_ = INVALID_SOCKET;
    unsigned short port_ = 0;
};

} // namespace

TEST_CASE("a request waiting on a silent server ends when cancelled", "[http]") {
    const SilentServer server;
    viewer::http::Cancel cancel;

    // Thirty seconds a step if nothing ends it: well past what this test waits.
    auto answer = std::async(std::launch::async, [&] { return viewer::http::get(server.url(), cancel, std::chrono::seconds(30)); });
    CHECK(answer.wait_for(std::chrono::milliseconds(300)) == std::future_status::timeout);

    cancel.cancel();
    REQUIRE(answer.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK_FALSE(answer.get());

    // Once cancelled, a request ends before it starts.
    CHECK_FALSE(viewer::http::get(server.url(), cancel, std::chrono::seconds(30)));
}

TEST_CASE("stopping a background job cancels the request it waits on", "[http]") {
    const SilentServer server;
    viewer::Background<std::string> job;
    job.start([&](const viewer::http::Cancel& cancel) { return viewer::http::get(server.url(), cancel, std::chrono::seconds(30)); });

    // Long enough to be waiting on the server rather than not yet started.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK_FALSE(job.finished());

    const auto started = std::chrono::steady_clock::now();
    job.stop();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    CHECK(job.finished());
    CHECK(job.get() == nullptr);
}
