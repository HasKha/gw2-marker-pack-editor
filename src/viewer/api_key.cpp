#include "viewer/api_key.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>

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

} // namespace viewer::api_key
