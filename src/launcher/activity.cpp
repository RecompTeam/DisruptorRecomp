#include "activity.h"

#include <chrono>
#include <exception>
#include <format>
#include <utility>

namespace disruptor::launcher {

void Activity::add(std::string text) noexcept {
    try {
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        std::string stamp;
        try {
            stamp = std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::current_zone()->to_local(now));
        } catch (const std::exception&) {
            stamp = std::format("{:%Y-%m-%d %H:%M:%S} UTC", now);
        }
        std::scoped_lock lock(mutex_);
        lines_.push_back(stamp + "  " + std::move(text));
    } catch (...) {
        // A line that cannot be kept is dropped: no update is worth stopping for the Activity box.
    }
}

std::vector<std::string> Activity::lines() const {
    std::scoped_lock lock(mutex_);
    return lines_;
}

} // namespace disruptor::launcher
