#include "game/achievements/chime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace disruptor::achievements {

namespace {

struct Note {
    double at;     // seconds from the tune's start
    double pitch;  // hertz
};

constexpr std::array<Note, 2> kUnlock = {{{0.0, 1318.51}, {0.09, 1975.53}}};
constexpr std::array<Note, 4> kMastery = {{{0.0, 1046.50}, {0.11, 1318.51}, {0.22, 1567.98}, {0.33, 2093.00}}};
constexpr double kDecay = 0.28;
constexpr double kAttack = 0.004;
constexpr double kTail = 1.1;
constexpr double kLoudness = 5200.0;
constexpr double kTau = 6.283185307179586;

std::span<const Note> notes(int tune) {
    return tune == static_cast<int>(Chime::Tune::mastery) ? std::span<const Note>(kMastery)
                                                          : std::span<const Note>(kUnlock);
}

double sample(int tune, double time) {
    double sum = 0.0;
    for (const Note& note : notes(tune)) {
        const double since = time - note.at;
        if (since < 0.0) continue;
        const double envelope = std::min(1.0, since / kAttack) * std::exp(-since / kDecay);
        sum += envelope * (std::sin(kTau * note.pitch * since) + 0.3 * std::sin(2.0 * kTau * note.pitch * since));
    }
    return sum * kLoudness;
}

double length(int tune) { return notes(tune).back().at + kTail; }

} // namespace

void Chime::start(Tune tune) { asked_.store(static_cast<int>(tune), std::memory_order_release); }

void Chime::use(std::shared_ptr<const Clip> clip) {
    std::scoped_lock lock(clip_mutex_);
    clip_ = clip && clip->frames() >= 2 && clip->rate > 0 ? std::move(clip) : nullptr;
}

void Chime::mix(std::int16_t* stereo, int frames, int rate) {
    if (const int asked = asked_.exchange(-1, std::memory_order_acq_rel); asked >= 0) {
        playing_ = asked;
        time_ = 0.0;
        std::scoped_lock lock(clip_mutex_);
        sounding_ = clip_;
    }
    if (playing_ < 0 || !stereo || frames <= 0 || rate <= 0) return;
    if (sounding_) {
        // The player's sound at its own rate, read between its frames for the output's.
        const Clip& clip = *sounding_;
        const double step = static_cast<double>(clip.rate) / rate;
        for (int frame = 0; frame < frames; ++frame, time_ += step) {
            const std::size_t at = static_cast<std::size_t>(time_);
            if (at + 1 >= clip.frames()) {
                playing_ = -1;
                sounding_.reset();
                return;
            }
            const double between = time_ - static_cast<double>(at);
            for (int side = 0; side < 2; ++side) {
                const double added = clip.stereo[2 * at + side] * (1.0 - between) + clip.stereo[2 * at + 2 + side] * between;
                std::int16_t& out = stereo[2 * frame + side];
                out = static_cast<std::int16_t>(std::clamp(out + std::lround(added), -32768L, 32767L));
            }
        }
        return;
    }
    const double step = 1.0 / rate;
    for (int frame = 0; frame < frames && playing_ >= 0; ++frame, time_ += step) {
        const double added = sample(playing_, time_);
        for (int side = 0; side < 2; ++side) {
            std::int16_t& out = stereo[2 * frame + side];
            out = static_cast<std::int16_t>(std::clamp(out + std::lround(added), -32768L, 32767L));
        }
        if (time_ >= length(playing_)) playing_ = -1;
    }
}

} // namespace disruptor::achievements
