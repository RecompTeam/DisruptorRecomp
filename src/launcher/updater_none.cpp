#include "updater.h"

#include <format>
#include <utility>

namespace Velopack {
class UpdateManager {};
struct UpdateInfo {};
struct VelopackAsset {};
} // namespace Velopack

namespace disruptor::launcher {

Updater::Updater(std::string source, Activity& activity) : source_(std::move(source)), activity_(activity) {
    view_.error = "This build was made without the updater.";
}

Updater::~Updater() = default;

UpdateView Updater::view() const {
    std::scoped_lock lock(mutex_);
    return view_;
}

void Updater::check() {}

void Updater::download() {}

bool Updater::apply(bool, bool) { return false; }

std::string size_text(std::uint64_t bytes) {
    if (bytes >= 1024 * 1024) return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return std::format("{} KB", (bytes + 1023) / 1024);
}

} // namespace disruptor::launcher
