#pragma once

#include "game/achievements/sound_clip.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace disruptor::achievements {

// The sound of an unlock, made as it plays and mixed into the game's own output: two bell notes, four for a mastery,
// or the player's own sound for both.
class Chime {
public:
    enum class Tune { unlock, mastery };

    // Any thread. A tune asked for while another plays starts over in its place.
    void start(Tune tune);
    // Any thread: the player's sound in place of the tunes from the next start, or null for the chime's own.
    void use(std::shared_ptr<const Clip> clip);
    // The audio thread: adds the tune to `frames` interleaved stereo frames at `rate`.
    void mix(std::int16_t* stereo, int frames, int rate);
    [[nodiscard]] bool playing() const { return playing_ >= 0; }

private:
    std::atomic<int> asked_{-1};
    std::mutex clip_mutex_;
    std::shared_ptr<const Clip> clip_;
    std::shared_ptr<const Clip> sounding_;
    int playing_ = -1;
    double time_ = 0.0;
};

} // namespace disruptor::achievements
