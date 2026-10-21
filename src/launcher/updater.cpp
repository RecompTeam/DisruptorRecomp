#include "updater.h"

#include "Velopack.hpp"

#include <exception>
#include <format>
#include <numeric>
#include <system_error>
#include <utility>

namespace disruptor::launcher {

namespace {

std::unique_ptr<Velopack::UpdateManager> open_manager(const std::string& source) {
    if (source.starts_with("https://github.com/"))
        return std::make_unique<Velopack::UpdateManager>(std::make_unique<Velopack::GithubSource>(source, "", true));
    return std::make_unique<Velopack::UpdateManager>(source);
}

} // namespace

Updater::Updater(std::string source, Activity& activity) : source_(std::move(source)), activity_(activity) {
    try {
        manager_ = open_manager(source_);
    } catch (const std::exception& error) {
        activity_.add(std::string("Updates are off: the updater cannot open this copy (") + error.what() + ")");
        return;
    }
    view_.state = UpdateState::idle;
    view_.portable = manager_->IsPortable();
    view_.installed = manager_->GetCurrentVersion();
    if (const auto pending = manager_->UpdatePendingRestart()) {
        pending_ = std::make_unique<Velopack::VelopackAsset>(*pending);
        view_.state = UpdateState::ready;
        view_.latest = pending->Version;
        view_.notes = pending->NotesMarkdown;
    }
    activity_.add(std::format("Installed version {}{}", view_.installed, view_.portable ? " (portable)" : ""));
}

Updater::~Updater() {
    if (worker_.joinable()) worker_.join();
}

UpdateView Updater::view() const {
    std::scoped_lock lock(mutex_);
    return view_;
}

template <typename Change> void Updater::change(Change&& alter) {
    std::scoped_lock lock(mutex_);
    alter(view_);
}

void Updater::fail(const std::string& what) {
    const bool waiting = pending_ != nullptr;
    change([&](UpdateView& view) {
        view.state = waiting ? UpdateState::ready : UpdateState::failed;
        view.error = what;
    });
    activity_.add("Update failed: " + what);
}

void Updater::start(std::function<void()> task) {
    if (!manager_ || busy_.exchange(true)) return;
    if (worker_.joinable()) worker_.join();
    try {
        worker_ = std::thread([this, task = std::move(task)] {
            try {
                task();
            } catch (const std::exception& error) {
                fail(error.what());
            }
            busy_ = false;
        });
    } catch (const std::system_error& error) {
        busy_ = false;
        fail(error.what());
    }
}

void Updater::check() {
    start([this] {
        change([](UpdateView& view) {
            view.state = UpdateState::checking;
            view.error.clear();
        });
        activity_.add("Checking " + source_ + " for updates...");
        auto found = manager_->CheckForUpdates();
        if (!found || (pending_ && pending_->Version == found->TargetFullRelease.Version)) {
            found_.reset();
            change([&](UpdateView& view) {
                view.state = pending_ ? UpdateState::ready : UpdateState::current;
                view.latest = pending_ ? pending_->Version : view.installed;
            });
            activity_.add(pending_ ? "The downloaded update is still the newest" : "This is the newest version");
            return;
        }
        found_ = std::make_unique<Velopack::UpdateInfo>(std::move(*found));
        const auto& target = found_->TargetFullRelease;
        const auto& deltas = found_->DeltasToTarget;
        const std::uint64_t delta_bytes =
            std::accumulate(deltas.begin(), deltas.end(), std::uint64_t{0},
                            [](std::uint64_t sum, const Velopack::VelopackAsset& asset) { return sum + asset.Size; });
        change([&](UpdateView& view) {
            view.state = UpdateState::available;
            view.latest = target.Version;
            view.notes = target.NotesMarkdown;
            view.delta = !deltas.empty();
            view.download_bytes = deltas.empty() ? target.Size : delta_bytes;
            view.full_bytes = target.Size;
        });
        activity_.add(std::format("Version {} is available: about {} to download{}", target.Version,
                                  size_text(deltas.empty() ? target.Size : delta_bytes),
                                  deltas.empty() ? "" : std::format(" in {} delta package(s)", deltas.size())));
    });
}

void Updater::download() {
    if (busy_ || !found_) return;
    start([this] {
        change([](UpdateView& view) {
            view.state = UpdateState::downloading;
            view.progress = 0;
            view.error.clear();
        });
        activity_.add("Downloading version " + found_->TargetFullRelease.Version + "...");
        const auto progress = [](void* user, size_t percent) {
            static_cast<Updater*>(user)->change([&](UpdateView& view) { view.progress = static_cast<int>(percent); });
        };
        manager_->DownloadUpdates(*found_, progress, this);
        pending_ = std::make_unique<Velopack::VelopackAsset>(found_->TargetFullRelease);
        found_.reset();
        change([](UpdateView& view) {
            view.state = UpdateState::ready;
            view.progress = 100;
        });
        activity_.add("Downloaded, and each package matched its SHA256\nPress Restart and update to install it");
    });
}

bool Updater::apply(bool silent, bool restart) {
    if (!manager_ || busy_ || !pending_) return false;
    try {
        manager_->WaitExitThenApplyUpdates(*pending_, silent, restart);
    } catch (const std::exception& error) {
        fail(error.what());
        return false;
    }
    activity_.add("Restarting to apply the update...");
    return true;
}

std::string size_text(std::uint64_t bytes) {
    if (bytes >= 1024 * 1024) return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return std::format("{} KB", (bytes + 1023) / 1024);
}

} // namespace disruptor::launcher
