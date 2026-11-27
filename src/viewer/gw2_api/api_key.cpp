#include "viewer/gw2_api/api_key.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <optional>

#include "util/job_queue.h"

namespace viewer::api_key {

std::string trimmed(std::string_view key) {
    constexpr std::string_view kSpace = " \t\r\n";
    const size_t first = key.find_first_not_of(kSpace);
    if (first == std::string_view::npos) return {};
    return std::string(key.substr(first, key.find_last_not_of(kSpace) - first + 1));
}

Check fromTokenInfo(const http::Reply& reply) {
    Check check;
    if (reply.status == 400 || reply.status == 401 || reply.status == 403) {
        check.outcome = Outcome::Rejected;
    } else if (reply.status == 200) {
        // {"id": ..., "name": ..., "permissions": ["account", ...]}, for a subtoken as for a key.
        const nlohmann::json body = nlohmann::json::parse(reply.body, nullptr, false);
        if (body.is_object() && body.contains("permissions") && body["permissions"].is_array()) {
            const nlohmann::json& granted = body["permissions"];
            for (const char* permission : PERMISSIONS) {
                if (std::find(granted.begin(), granted.end(), permission) == granted.end()) {
                    check.missing += (check.missing.empty() ? "" : ", ") + std::string(permission);
                }
            }
            check.outcome = check.missing.empty() ? Outcome::Valid : Outcome::MissingPermissions;
        }
    }
    return check;
}

std::string authorization(std::string_view key) {
    return "Authorization: Bearer " + std::string(key) + "\r\n";
}

Check ask(std::string_view key, const http::Cancel& cancel) {
    const std::optional<http::Reply> reply =
        http::request("https://api.guildwars2.com/v2/tokeninfo", authorization(key), cancel, std::chrono::seconds(15));
    return reply ? fromTokenInfo(*reply) : Check{};
}

void Registration::submit() {
    if (std::string key = trimmed(field); key.empty()) {
        status = "Paste a key in the field first.";
    } else {
        status.clear();
        check = std::make_unique<http::Background<Checked>>();
        check->start(
            [key = std::move(key)](const http::Cancel& cancel) -> std::optional<Checked> { return Checked{key, ask(key, cancel)}; });
    }
}

std::optional<std::string> Registration::takeAccepted() {
    if (util::ready(saved) && !util::take(saved).value_or(false)) {
        status = "Key added, but it could not be saved: you will need to add it again next time.";
    }
    if (!check || !check->finished()) return std::nullopt;
    // The job always answers; one that did not counts as the API not answering.
    const Checked done = check->take().value_or(Checked{});
    check.reset();
    switch (done.check.outcome) {
    case Outcome::Valid:
        field.clear();
        status = "Key added.";
        return done.key;
    case Outcome::MissingPermissions:
        status =
            "This key is missing permissions: " + done.check.missing + ". Make a new one with \"account\" and \"progression\" selected.";
        break;
    case Outcome::Rejected: status = "This key was not accepted. Check that you copied all of it."; break;
    case Outcome::NoAnswer: status = "Could not reach the GW2 API to check the key. Try again in a moment."; break;
    }
    return std::nullopt;
}

void Registration::stop() {
    check.reset();
    saved = {};
}

} // namespace viewer::api_key
