#include "game/achievements/policy.h"

namespace disruptor::achievements {

Verdict judge(bool hardcore, Act act) {
    if (!hardcore) return Verdict::allowed;
    switch (act) {
    case Act::load_state:
    case Act::use_cheat:
    case Act::start_mission: return Verdict::refused;
    case Act::debug_tools:
    case Act::input_replay: return Verdict::ends_hardcore;
    }
    return Verdict::refused;
}

std::string_view explain(Act act) {
    switch (act) {
    case Act::load_state: return "Hardcore: save states cannot be loaded";
    case Act::use_cheat: return "Hardcore: cheats are off";
    case Act::start_mission: return "Hardcore: missions start from the game's own menu";
    case Act::debug_tools: return "A debug tool connected: achievements go on in casual";
    case Act::input_replay: return "An input replay started: achievements go on in casual";
    }
    return "Hardcore: not allowed";
}

void RunRecord::allowed(Act) { marked_ = true; }

void RunRecord::switched_off() {
    marked_ = true;
    lapsed_ = true;
}

void RunRecord::session_started() { marked_ = true; }

bool RunRecord::may_start_hardcore() const { return !marked_.load(); }

bool RunRecord::take_lapse() { return lapsed_.exchange(false); }

} // namespace disruptor::achievements
