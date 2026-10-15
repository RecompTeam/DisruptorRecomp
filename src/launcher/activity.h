#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace disruptor::launcher {

// The window's Activity box, written from any thread.
class Activity {
public:
    void add(std::string text) noexcept;
    std::vector<std::string> lines() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};

} // namespace disruptor::launcher
