// Requests that end when told to: the addon unloading must not wait out a server's silence.
// And Background, the one job on a thread of its own that most of them run in.

#include <catch2/catch_test_macros.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "http/background.h"
#include "http/http.h"

namespace {

// A server on localhost that takes connections and never says a word: what a request waiting
// on a stalled server looks like, without the web.
class SilentServer
{
public:
    SilentServer() {
        WSADATA data{};
        started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0; // any free port
        bind(listen_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        listen(listen_socket, 4);
        int length = sizeof(address);
        getsockname(listen_socket, reinterpret_cast<sockaddr*>(&address), &length);
        port = ntohs(address.sin_port);
    }
    ~SilentServer() {
        close();
        if (started) WSACleanup();
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port) + "/"; }
    SOCKET listener() const { return listen_socket; }
    // Stops listening, which ends an accept() waiting on it.
    void close() {
        if (listen_socket != INVALID_SOCKET) closesocket(listen_socket);
        listen_socket = INVALID_SOCKET;
    }

private:
    bool started = false;
    SOCKET listen_socket = INVALID_SOCKET;
    unsigned short port = 0;
};

// A server on localhost that answers its first request with `status`, a line such as
// "200 OK", and `body`, and keeps what it was asked.
class OneReplyServer
{
public:
    OneReplyServer(const std::string& status, const std::string& body)
        : reply("HTTP/1.1 " + status + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body) {
        thread = std::thread([this] {
            const SOCKET client = accept(silent.listener(), nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            char buffer[4096];
            while (received.find("\r\n\r\n") == std::string::npos) {
                const int read = recv(client, buffer, sizeof(buffer), 0);
                if (read <= 0) break;
                received.append(buffer, static_cast<size_t>(read));
            }
            send(client, reply.data(), static_cast<int>(reply.size()), 0);
            shutdown(client, SD_SEND);
            closesocket(client);
        });
    }
    ~OneReplyServer() {
        silent.close();
        thread.join();
    }

    std::string url() const { return silent.url(); }
    // What the request sent, once it has its answer.
    const std::string& request() const { return received; }

private:
    SilentServer silent;
    std::string reply;
    std::string received;
    std::thread thread;
};

} // namespace

TEST_CASE("a request sends its headers and reads any status's body", "[http]") {
    const OneReplyServer server("401 Unauthorized", R"({"text":"Invalid access token"})");
    http::Cancel cancel;
    const std::optional<http::Reply> reply = http::request(server.url(), "Authorization: Bearer KEY\r\n", cancel, std::chrono::seconds(5));
    REQUIRE(reply);
    CHECK(reply->status == 401);
    CHECK(reply->body == R"({"text":"Invalid access token"})");
    CHECK(server.request().find("\r\nAuthorization: Bearer KEY\r\n") != std::string::npos);
}

TEST_CASE("get answers only a 200", "[http]") {
    const OneReplyServer server("404 Not Found", "no");
    http::Cancel cancel;
    CHECK_FALSE(http::get(server.url(), cancel));
}

TEST_CASE("a background job's answer is there once it is done, and only if it gave one", "[http]") {
    http::Background<int> answered;
    CHECK(answered.get() == nullptr);
    answered.start([](const http::Cancel&) { return std::optional<int>(42); });
    answered.start([](const http::Cancel&) { return std::optional<int>(7); }); // once only
    answered.stop();
    REQUIRE(answered.get());
    CHECK(*answered.get() == 42);

    http::Background<int> unanswered;
    unanswered.start([](const http::Cancel&) { return std::optional<int>(); });
    unanswered.stop();
    CHECK(unanswered.get() == nullptr);
}

TEST_CASE("a request waiting on a silent server ends when cancelled", "[http]") {
    const SilentServer server;
    http::Cancel cancel;

    // Thirty seconds a step if nothing ends it: well past what this test waits.
    auto answer = std::async(std::launch::async, [&] { return http::get(server.url(), cancel, std::chrono::seconds(30)); });
    CHECK(answer.wait_for(std::chrono::milliseconds(300)) == std::future_status::timeout);

    cancel.cancel();
    REQUIRE(answer.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK_FALSE(answer.get());

    // Once cancelled, a request ends before it starts.
    CHECK_FALSE(http::get(server.url(), cancel, std::chrono::seconds(30)));
}

TEST_CASE("stopping a background job cancels the request it waits on", "[http]") {
    const SilentServer server;
    http::Background<std::string> job;
    job.start([&](const http::Cancel& cancel) { return http::get(server.url(), cancel, std::chrono::seconds(30)); });

    // Long enough to be waiting on the server rather than not yet started.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK_FALSE(job.finished());

    const auto started = std::chrono::steady_clock::now();
    job.stop();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    CHECK(job.finished());
    CHECK(job.get() == nullptr);
}
