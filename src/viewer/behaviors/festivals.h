#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Which festivals are on, for the `festival` filter: Blish HUD's FestivalContext, which asks
// the GW2 API and, when the API gets it wrong, Blish HUD's own service.
namespace viewer::behaviors {

struct Festival {
    const char* name; // what packs write, lowercase
    const char* display_name; // what a hidden marker's reason says
    int daily_category; // its category in the daily achievement group
};

// FestivalContext's known festivals.
extern const Festival kFestivals[6];

// The display name of festival `name`, or "Unknown" for one Blish HUD does not know.
const char* festivalDisplayName(std::string_view name);

// The festivals on, from the body of /v2/achievements/groups/<daily group>: those whose
// category the group lists. Nothing when the body is not that.
std::optional<std::vector<std::string>> festivalsFromDailyGroup(std::string_view json);

// The festivals on, from Blish HUD's getactivefestivals: a JSON array of names.
std::optional<std::vector<std::string>> festivalsFromList(std::string_view json);

// Asks once, on a thread of its own, and holds the answer. Until it is in, no festival is on,
// as in Blish HUD until its context is ready.
class FestivalFetcher
{
public:
    FestivalFetcher() = default;
    ~FestivalFetcher();
    FestivalFetcher(const FestivalFetcher&) = delete;
    FestivalFetcher& operator=(const FestivalFetcher&) = delete;

    void start();
    // Waits for a request still in flight, which its timeouts bound: unloading offline costs
    // at most those seconds.
    void stop();

    // The festivals on, lowercase; empty until the answer is in.
    std::vector<std::string> active() const;

private:
    std::thread worker_;
    mutable std::mutex mutex_;
    std::vector<std::string> active_;
};

} // namespace viewer::behaviors
