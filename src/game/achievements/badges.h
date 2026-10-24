#pragma once

#include "game/achievements/requests.h"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace disruptor::achievements {

// The name a picture from RetroAchievements' media server is kept under, or empty for any other address.
[[nodiscard]] std::string badge_file_name(std::string_view url);

// Achievement and game pictures, each fetched once and then read from the folder. The requests carry no account.
class Badges {
public:
    static constexpr std::size_t kLargest = 256 * 1024;

    Badges(std::filesystem::path folder, std::unique_ptr<Transport> transport);
    Badges(const Badges&) = delete;
    Badges& operator=(const Badges&) = delete;

    // The PNG once it is here, otherwise null. The first call for an address starts its fetch. Any thread.
    [[nodiscard]] std::shared_ptr<const std::string> png(const std::string& url);

private:
    enum class State { fetching, ready, failed };
    struct Entry {
        State state = State::fetching;
        std::shared_ptr<const std::string> png;
    };

    void arrived(const std::string& url, const std::string& name, const Response& response);

    std::filesystem::path folder_;
    std::mutex mutex_;
    std::map<std::string, Entry> entries_;
    // Last, so it is the first to go: its thread writes the entries while it runs.
    Requests requests_;
};

} // namespace disruptor::achievements
