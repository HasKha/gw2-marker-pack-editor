#pragma once

#include <string>
#include <string_view>

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

} // namespace viewer::api_key
