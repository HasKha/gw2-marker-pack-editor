// What /v2/tokeninfo's answers say of a key the player registers.

#include <catch2/catch_test_macros.hpp>

#include "viewer/gw2_api/api_key.h"

using viewer::api_key::Outcome;

TEST_CASE("a key with account and progression is valid, whatever else it may read", "[api_key]") {
    const auto check = viewer::api_key::fromTokenInfo(
        {200, R"({"id":"ABC","name":"pathing","permissions":["account","builds","progression","wallet"],"type":"APIKey"})"});
    CHECK(check.outcome == Outcome::Valid);
    CHECK(check.missing.empty());
}

TEST_CASE("a key without progression names what it lacks", "[api_key]") {
    const auto check = viewer::api_key::fromTokenInfo({200, R"({"id":"ABC","name":"x","permissions":["account","characters"]})"});
    CHECK(check.outcome == Outcome::MissingPermissions);
    CHECK(check.missing == "progression");

    const auto none = viewer::api_key::fromTokenInfo({200, R"({"permissions":[]})"});
    CHECK(none.missing == "account, progression");
}

TEST_CASE("the API's refusals are a key it does not know; anything else is no answer", "[api_key]") {
    // As the API answers a key-shaped string it does not know, and text that is not a key.
    CHECK(viewer::api_key::fromTokenInfo({401, R"({"text":"Invalid access token"})"}).outcome == Outcome::Rejected);
    CHECK(viewer::api_key::fromTokenInfo({400, R"({"text":"invalid key"})"}).outcome == Outcome::Rejected);

    CHECK(viewer::api_key::fromTokenInfo({503, ""}).outcome == Outcome::NoAnswer);
    CHECK(viewer::api_key::fromTokenInfo({429, R"({"text":"too many requests"})"}).outcome == Outcome::NoAnswer);
    CHECK(viewer::api_key::fromTokenInfo({200, "<html>maintenance</html>"}).outcome == Outcome::NoAnswer);
    CHECK(viewer::api_key::fromTokenInfo({200, R"({"permissions":"account"})"}).outcome == Outcome::NoAnswer);
}

TEST_CASE("a pasted key loses the spaces and line breaks around it", "[api_key]") {
    CHECK(viewer::api_key::trimmed("  ABC-DEF\r\n") == "ABC-DEF");
    CHECK(viewer::api_key::trimmed("\t") == "");
    CHECK(viewer::api_key::trimmed("ABC") == "ABC");
}
