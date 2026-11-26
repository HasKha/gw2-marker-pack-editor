#include "viewer/gw2_api/achievements.h"

#include <charconv>
#include <regex>

#include "http/http.h"
#include "util/json_file.h"

namespace viewer::achievements {

namespace {

std::string readText(const nlohmann::json& object, const char* key) {
    return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
}

} // namespace

const std::string& Details::requirementFor(int bit) const {
    if (bit >= 0 && static_cast<size_t>(bit) < bits.size() && !bits[static_cast<size_t>(bit)].empty()) {
        return bits[static_cast<size_t>(bit)];
    }
    return requirement;
}

std::string withoutMarkup(std::string_view text) {
    // CleanMessage's two regular expressions, Singleline's "." spelled [\s\S].
    static const std::regex kColour(R"(<c[=@][@=]?[^>]+>([\s\S]*?)(</?c/?>|$))", std::regex::icase);
    static const std::regex kBreaks(R"((<br ?/?>)+)", std::regex::icase);
    const std::string uncoloured = std::regex_replace(std::string(text), kColour, "$1");
    return std::regex_replace(uncoloured, kBreaks, "\n");
}

std::optional<std::unordered_map<int, Details>> detailsFromApi(std::string_view json) {
    const nlohmann::json body = nlohmann::json::parse(json, nullptr, false);
    if (!body.is_array()) return std::nullopt;
    std::unordered_map<int, Details> out;
    for (const nlohmann::json& entry : body) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_integer()) continue;
        Details details;
        details.name = readText(entry, "name");
        details.description = withoutMarkup(readText(entry, "description"));
        details.requirement = withoutMarkup(readText(entry, "requirement"));
        if (entry.contains("bits") && entry["bits"].is_array()) {
            for (const nlohmann::json& bit : entry["bits"]) {
                const bool text = bit.is_object() && readText(bit, "type") == "Text";
                details.bits.push_back(text ? withoutMarkup(readText(bit, "text")) : std::string());
            }
        }
        out[entry["id"].get<int>()] = std::move(details);
    }
    return out;
}

nlohmann::json toCache(const Known& known) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [id, details] : known) {
        if (details) {
            out[std::to_string(id)] = {
                {"name", details->name},
                {"description", details->description},
                {"requirement", details->requirement},
                {"bits", details->bits},
            };
        } else {
            out[std::to_string(id)] = nullptr;
        }
    }
    return out;
}

Known fromCache(const nlohmann::json& cache) {
    Known known;
    if (!cache.is_object()) return known;
    for (const auto& [id_text, entry] : cache.items()) {
        int id = 0;
        const auto [end, error] = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
        if (error != std::errc() || end != id_text.data() + id_text.size()) continue;
        if (entry.is_null()) {
            known[id] = std::nullopt;
        } else if (entry.is_object()) {
            Details details;
            details.name = readText(entry, "name");
            details.description = readText(entry, "description");
            details.requirement = readText(entry, "requirement");
            if (entry.contains("bits") && entry["bits"].is_array()) {
                for (const nlohmann::json& bit : entry["bits"]) details.bits.push_back(bit.is_string() ? bit.get<std::string>() : "");
            }
            known[id] = std::move(details);
        }
    }
    return known;
}

void Catalog::open(const std::filesystem::path& cache_file, util::JobQueue* jobs) {
    file_ = cache_file;
    jobs_ = jobs;
    if (file_.empty()) return;
    if (const std::optional<nlohmann::json> cache = util::json_file::readObject(file_)) known_ = fromCache(*cache);
}

const Details* Catalog::find(int id) {
    if (const auto found = known_.find(id); found != known_.end()) return found->second ? &*found->second : nullptr;
    if (!asking_.count(id)) wanted_.insert(id);
    return nullptr;
}

void Catalog::update(Clock::time_point now) {
    if (request_ && request_->finished()) {
        if (std::optional<Known> answer = request_->take()) {
            known_ = std::move(*answer);
            failed_at_.reset();
        } else {
            wanted_.insert(asking_.begin(), asking_.end());
            failed_at_ = now;
        }
        request_.reset();
        asking_.clear();
    }

    // After an ask the API did not answer, the next waits this long: the tooltips stay empty
    // meanwhile, and asking every frame would only hammer an API that is down.
    constexpr auto kRetry = std::chrono::seconds(60);
    // /v2/achievements takes at most 200 ids at once.
    constexpr size_t kBatch = 200;
    if (request_ || wanted_.empty() || (failed_at_ && now - *failed_at_ < kRetry)) return;

    std::string ids;
    while (!wanted_.empty() && asking_.size() < kBatch) {
        const int id = *wanted_.begin();
        wanted_.erase(wanted_.begin());
        asking_.insert(id);
        ids += (ids.empty() ? "" : ",") + std::to_string(id);
    }
    request_ = std::make_unique<http::Background<Known>>();
    request_->start(
        [url = "https://api.guildwars2.com/v2/achievements?ids=" + ids, asked = asking_, known = known_, file = file_](
            const http::Cancel& cancel) mutable -> std::optional<Known> {
            const std::optional<http::Reply> reply = http::request(url, {}, cancel, std::chrono::seconds(15));
            // 206 when some of the ids are not achievements, 404 when none is.
            std::optional<std::unordered_map<int, Details>> found;
            if (reply && (reply->status == 200 || reply->status == 206)) found = detailsFromApi(reply->body);
            if (reply && reply->status == 404) found.emplace();
            if (!found) return std::nullopt;

            for (const int id : asked) {
                const auto at = found->find(id);
                known[id] = at == found->end() ? std::nullopt : std::optional<Details>(std::move(at->second));
            }
            if (!file.empty()) util::json_file::write(file, toCache(known));
            return known;
        });
}

void Catalog::clear() {
    stop();
    request_.reset();
    known_.clear();
    wanted_.clear();
    asking_.clear();
    failed_at_.reset();
    if (!file_.empty() && jobs_) {
        jobs_->post([file = file_] {
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
        });
    }
}

void Catalog::stop() {
    if (request_) request_->stop();
}

} // namespace viewer::achievements
