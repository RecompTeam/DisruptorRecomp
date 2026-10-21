#pragma once

#include <cstdint>

namespace disruptor::audio {

/* The game's samples put most of the psionics' energy under 150 Hz. A TV's own speakers never played it back. */
enum class BassCut { original, reduced, tv_speaker };

int cut_off_hz(BassCut cut);

/* A second-order Butterworth high-pass over interleaved stereo. A change of cut or rate crossfades over 20 ms. */
class BassFilter {
public:
    void run(int16_t *stereo, int frames, BassCut cut, int rate);

private:
    struct Section {
        bool through = true;
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double state[2][2] = {};
        double step(int channel, double in);
    };
    static Section section_for(BassCut cut, int rate);

    BassCut cut_ = BassCut::original;
    int rate_ = 0;
    Section current_, previous_;
    int fade_left_ = 0, fade_length_ = 0;
};

/* The SPU's output as the host renders it, before any fade, gain or resampling. */
void shape_output(int16_t *stereo, int frames, int rate);

}  // namespace disruptor::audio

extern "C" {
int disruptor_bass_cut(void);
void disruptor_bass_cut_set(int cut);
}
