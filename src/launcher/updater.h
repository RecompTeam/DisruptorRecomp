#pragma once

#include "activity.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Velopack {
class UpdateManager;
struct UpdateInfo;
struct VelopackAsset;
} // namespace Velopack

namespace disruptor::launcher {

enum class UpdateState { unavailable, idle, checking, current, available, downloading, ready, failed };

struct UpdateView {
    UpdateState state = UpdateState::unavailable;
    bool portable = false;
    std::string installed; // empty when the updater did not install this copy
    std::string latest;
    std::string notes; // Markdown of the release a check found or a download left waiting
    std::uint64_t download_bytes = 0; // what the deltas weigh, or the full package when there are none
    std::uint64_t full_bytes = 0;     // what is fetched instead when the deltas cannot be applied
    bool delta = false;
    int progress = 0;
    std::string error;
};

// Velopack's update manager on a worker thread. found_ and pending_ are touched only by a task or while busy_ is clear.
class Updater {
public:
    // A GitHub repository URL reads its releases, pre-releases included, anything else is a feed folder or URL.
    Updater(std::string source, Activity& activity);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    const std::string& source() const { return source_; }
    UpdateView view() const;
    bool busy() const { return busy_; }

    void check();
    void download();
    // Hands the downloaded update to Velopack's updater. True when the caller must exit now.
    bool apply(bool silent, bool restart);

private:
    void start(std::function<void()> task);
    void fail(const std::string& what);
    template <typename Change> void change(Change&& alter);

    std::string source_;
    Activity& activity_;
    std::unique_ptr<Velopack::UpdateManager> manager_;
    std::unique_ptr<Velopack::UpdateInfo> found_;
    std::unique_ptr<Velopack::VelopackAsset> pending_;
    mutable std::mutex mutex_;
    UpdateView view_;
    std::atomic_bool busy_ = false;
    std::thread worker_;
};

std::string size_text(std::uint64_t bytes);

} // namespace disruptor::launcher
