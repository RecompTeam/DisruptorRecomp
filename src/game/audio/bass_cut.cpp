#include "game/audio/bass_cut.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>

namespace disruptor::audio {

namespace {

static_assert(std::atomic<int>::is_always_lock_free);

std::atomic<int> g_cut{0};
BassFilter g_output;

}  // namespace

int cut_off_hz(BassCut cut) {
    switch (cut) {
        case BassCut::reduced: return 80;
        case BassCut::tv_speaker: return 160;
        case BassCut::original: break;
    }
    return 0;
}

double BassFilter::Section::step(int channel, double in) {
    if (through) return in;
    double &first = state[channel][0];
    double &second = state[channel][1];
    const double out = b0 * in + first;
    first = b1 * in - a1 * out + second;
    second = b2 * in - a2 * out;
    if (std::fabs(first) < 1e-20) first = 0.0;
    if (std::fabs(second) < 1e-20) second = 0.0;
    return out;
}

BassFilter::Section BassFilter::section_for(BassCut cut, int rate) {
    Section section;
    const int hz = cut_off_hz(cut);
    if (hz == 0 || rate <= 2 * hz) return section;
    const double w0 = 2.0 * std::numbers::pi * hz / rate;
    const double alpha = std::sin(w0) / std::numbers::sqrt2;
    const double a0 = 1.0 + alpha;
    section.through = false;
    section.b0 = (1.0 + std::cos(w0)) / 2.0 / a0;
    section.b1 = -2.0 * section.b0;
    section.b2 = section.b0;
    section.a1 = -2.0 * std::cos(w0) / a0;
    section.a2 = (1.0 - alpha) / a0;
    return section;
}

void BassFilter::run(int16_t *stereo, int frames, BassCut cut, int rate) {
    if (cut != cut_ || rate != rate_) {
        previous_ = current_;
        current_ = section_for(cut, rate);
        cut_ = cut;
        rate_ = rate;
        fade_length_ = fade_left_ = previous_.through && current_.through ? 0 : std::max(rate / 50, 1);
    }
    if (!stereo || frames <= 0 || (current_.through && fade_left_ == 0)) return;
    for (int frame = 0; frame < frames; ++frame) {
        const double share = fade_left_ > 0 ? 1.0 - static_cast<double>(fade_left_) / fade_length_ : 1.0;
        for (int channel = 0; channel < 2; ++channel) {
            int16_t &sample = stereo[2 * frame + channel];
            const double in = sample;
            double out = current_.step(channel, in);
            if (fade_left_ > 0) out = previous_.step(channel, in) * (1.0 - share) + out * share;
            sample = static_cast<int16_t>(std::clamp(std::lround(out), -32768L, 32767L));
        }
        if (fade_left_ > 0) --fade_left_;
    }
}

void shape_output(int16_t *stereo, int frames, int rate) {
    g_output.run(stereo, frames, static_cast<BassCut>(g_cut.load(std::memory_order_relaxed)), rate);
}

}  // namespace disruptor::audio

extern "C" int disruptor_bass_cut(void) { return disruptor::audio::g_cut.load(std::memory_order_relaxed); }

extern "C" void disruptor_bass_cut_set(int cut) {
    disruptor::audio::g_cut.store(std::clamp(cut, 0, 2), std::memory_order_relaxed);
}
