#pragma once

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "http/background.h"
#include "http/http.h"

// The player's GW2 API key, for what Pathing asks the API about the account: the achievements
// and the raids done this week. Nexus has no key to share, so the player registers one in our
// options, and /v2/tokeninfo says whether it is a key and what it may read.
namespace viewer::api_key {

// Where a key is made.
constexpr const char* APPLICATIONS_URL = "https://account.arena.net/applications";

// Pathing's manifest's api_permissions, both optional there: AchievementStates and RaidStates
// ask the API only when the key has both. The applications page ticks "account" on every key.
constexpr const char* PERMISSIONS[] = {"account", "progression"};

enum class Outcome {
    Valid,
    MissingPermissions, // a key, without all of PERMISSIONS
    Rejected, // not a key the API knows
    NoAnswer, // the API could not be reached, or answered nothing we can read
};

struct Check {
    Outcome outcome = Outcome::NoAnswer;
    std::string missing; // MissingPermissions: those it lacks, as "a, b"
};

// The key as pasted, without the spaces and line breaks a copy picks up around it.
std::string trimmed(std::string_view key);

// What /v2/tokeninfo's answer says of the key it was asked about. The API answers 400 for
// text that is not shaped like a key and 401 for a key it does not know, as measured in
// November 2026; 403 is counted with them, as a key it will not answer for.
Check fromTokenInfo(const http::Reply& reply);

// The request header that hands the API `key`: a header rather than ?access_token=, so the key
// stays out of any log of addresses.
std::string authorization(std::string_view key);

// Asks /v2/tokeninfo about `key`. Blocks, so never from a frame.
Check ask(std::string_view key, const http::Cancel& cancel);

// A key the player registers in the options: what they have typed, the check of it while it
// runs, and what the last check said, until the key is removed. A check carries the key it was
// asked about, which is what is kept.
class Registration
{
public:
    char* field() { return field_; }
    size_t fieldSize() const { return sizeof(field_); }
    bool checking() const { return check_ != nullptr; }
    const std::string& status() const { return status_; }
    void setStatus(std::string status) { status_ = std::move(status); }

    // Checks what the field holds on a thread of its own, or says to paste a key first.
    void submit();

    // The key the last check found fit, once, with the field cleared: the caller keeps it and
    // hands over its save with saving(). Otherwise the status says why the key will not do.
    // Polled every frame, rather than from the options page, so a key is kept even if the page
    // closed while the check ran; a save that failed is reported then too.
    std::optional<std::string> takeAccepted();
    // The accepted key being written, which the status waits for.
    void saving(std::future<bool> saved) { saved_ = std::move(saved); }

    // Waits for the check, and forgets the save.
    void stop();

private:
    struct Checked {
        std::string key;
        Check check;
    };

    char field_[128] = {};
    std::unique_ptr<http::Background<Checked>> check_;
    std::string status_;
    std::future<bool> saved_;
};

} // namespace viewer::api_key
