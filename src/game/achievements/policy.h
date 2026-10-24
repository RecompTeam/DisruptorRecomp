#pragma once

#include <atomic>
#include <string_view>

namespace disruptor::achievements {

// What a hardcore session forbids. A casual one allows all of it.
enum class Act { load_state, use_cheat, start_mission, debug_tools, input_replay };

enum class Verdict {
    allowed,
    refused,        // the act does not happen
    ends_hardcore,  // the act happens and the session goes on in casual
};

[[nodiscard]] Verdict judge(bool hardcore, Act act);
// The line the player sees when a hardcore session refuses the act or ends over it.
[[nodiscard]] std::string_view explain(Act act);

// What a run did that keeps it from hardcore, which only a run watched from its start may play. Any thread.
class RunRecord {
public:
    void allowed(Act act);
    // RetroAchievements switched off: what follows goes unwatched.
    void switched_off();
    // A session started. Only the run's first may be hardcore.
    void session_started();
    [[nodiscard]] bool may_start_hardcore() const;
    // Whether it was switched off since the last call: a hardcore session then goes on in casual.
    [[nodiscard]] bool take_lapse();

private:
    std::atomic<bool> marked_{false};
    std::atomic<bool> lapsed_{false};
};

} // namespace disruptor::achievements
