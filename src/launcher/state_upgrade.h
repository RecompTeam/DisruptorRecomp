#pragma once

#include "disc_import.h"
#include "launcher_flow.h"
#include "launcher_settings.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace disruptor::launcher {

struct StateUpgrade {
    int carried = 0;                // states that ran on this build and now carry its stamp
    int refused = 0;                // states the game did not load or did not play, kept as they were
    int untried = 0;                // states that could not be tried this time, tried again next time
    std::vector<std::string> lines; // a line for each state, for the Activity box
};

// Tries each state of another build on this one: the game in `program`, with `data/cache/state-trial` as its folder
// of the player's files, loads it with the window hidden and the sound off, records a second of play and ends. A state
// that loads and plays is written back with this build's stamp; the original is kept in `saves/old-states/<stamp>`
// either way, and a state the game refuses is noted there so that the same file is not tried again on this build.
// Returns at once when there is none.
StateUpgrade upgrade_states(const std::filesystem::path& program, const std::filesystem::path& data,
                            const VerifiedDisc& disc, const SettingsFile& settings, const Regions& regions,
                            const std::atomic_bool& cancelled,
                            const std::function<void(const std::wstring&, unsigned)>& report);

} // namespace disruptor::launcher
