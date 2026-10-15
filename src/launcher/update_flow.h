#pragma once

#include "activity.h"
#include "updater.h"

#include <filesystem>

namespace disruptor::launcher {

// Copies the saves aside in `data` and hands the update to Velopack, which replaces the program folder. True when the
// launcher must exit now.
bool apply_update(Updater& updater, const std::filesystem::path& program, const std::filesystem::path& data,
                  Activity& activity, bool silent, bool restart);

} // namespace disruptor::launcher
