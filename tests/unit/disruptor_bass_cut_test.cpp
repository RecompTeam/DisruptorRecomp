#include "game/audio/bass_cut.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <vector>

namespace {

using disruptor::audio::BassCut;
using disruptor::audio::BassFilter;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

std::vector<int16_t> tone(double hz, int rate, int frames, double amplitude, double right_scale = 1.0) {
    std::vector<int16_t> stereo(2 * static_cast<size_t>(frames));
    for (int frame = 0; frame < frames; ++frame) {
        const double value = amplitude * std::sin(2.0 * std::numbers::pi * hz * frame / rate);
        stereo[2 * frame] = static_cast<int16_t>(std::lround(value));
        stereo[2 * frame + 1] = static_cast<int16_t>(std::lround(value * right_scale));
    }
    return stereo;
}

double level_db(const std::vector<int16_t> &stereo, int channel, int skip, double amplitude) {
    double sum = 0.0;
    int count = 0;
    for (size_t frame = static_cast<size_t>(skip); 2 * frame + 1 < stereo.size(); ++frame) {
        const double sample = stereo[2 * frame + channel];
        sum += sample * sample;
        ++count;
    }
    return 20.0 * std::log10(std::sqrt(sum / count) / (amplitude / std::numbers::sqrt2));
}

double through(BassCut cut, double hz, int rate) {
    const int frames = rate;
    std::vector<int16_t> stereo = tone(hz, rate, frames, 10000.0);
    BassFilter filter;
    filter.run(stereo.data(), frames, cut, rate);
    return level_db(stereo, 0, rate / 2, 10000.0);
}

void original_leaves_the_sound_alone() {
    std::vector<int16_t> stereo = tone(50.0, 48000, 4800, 30000.0, -0.5);
    const std::vector<int16_t> before = stereo;
    BassFilter filter;
    filter.run(stereo.data(), 4800, BassCut::original, 48000);
    expect(stereo == before, "Original gives back every sample as it was");
}

void each_cut_takes_the_low_end_and_keeps_the_rest() {
    for (const int rate : {44100, 48000}) {
        expect(std::fabs(through(BassCut::reduced, 80.0, rate) + 3.01) < 0.2, "Reduced is 3 dB down at 80 Hz");
        expect(through(BassCut::reduced, 40.0, rate) < -11.5, "Reduced takes about 12 dB at 40 Hz");
        expect(std::fabs(through(BassCut::tv_speaker, 160.0, rate) + 3.01) < 0.2, "TV speaker is 3 dB down at 160 Hz");
        expect(through(BassCut::tv_speaker, 80.0, rate) < -11.5, "TV speaker takes about 12 dB at 80 Hz");
        for (const BassCut cut : {BassCut::reduced, BassCut::tv_speaker}) {
            expect(std::fabs(through(cut, 1000.0, rate)) < 0.15, "a cut leaves 1 kHz as it was");
            expect(std::fabs(through(cut, 5000.0, rate)) < 0.15, "a cut leaves 5 kHz as it was");
        }
    }
}

void the_channels_are_filtered_apart() {
    std::vector<int16_t> stereo = tone(1000.0, 48000, 48000, 10000.0, 0.0);
    BassFilter filter;
    filter.run(stereo.data(), 48000, BassCut::tv_speaker, 48000);
    expect(std::fabs(level_db(stereo, 0, 24000, 10000.0)) < 0.15 && level_db(stereo, 1, 0, 10000.0) < -200.0,
           "a silent right channel stays silent beside a loud left one");
}

void a_full_scale_edge_does_not_wrap() {
    std::vector<int16_t> stereo(2 * 4800);
    for (int frame = 0; frame < 4800; ++frame)
        stereo[2 * frame] = stereo[2 * frame + 1] = static_cast<int16_t>((frame / 300) % 2 ? 32767 : -32768);
    BassFilter filter;
    filter.run(stereo.data(), 4800, BassCut::tv_speaker, 48000);
    bool wrapped = false;
    for (int frame = 1; frame < 4800; ++frame)
        if (frame % 300 == 0 && (stereo[2 * frame] < 0) != (frame / 300 % 2 == 0)) wrapped = true;
    expect(!wrapped, "an overshoot past full scale is held at the edge, never wrapped to the other sign");
}

void a_change_of_cut_or_rate_takes_effect_in_20_ms() {
    BassFilter filter;
    std::vector<int16_t> first = tone(40.0, 48000, 48000, 10000.0);
    filter.run(first.data(), 48000, BassCut::tv_speaker, 48000);
    std::vector<int16_t> second = tone(40.0, 44100, 44100, 10000.0);
    const std::vector<int16_t> source = second;
    filter.run(second.data(), 44100, BassCut::original, 44100);
    expect(std::equal(second.begin() + 2 * 882, second.end(), source.begin() + 2 * 882),
           "back to Original the sound is untouched once the 20 ms crossfade is over");
    std::vector<int16_t> third = tone(80.0, 44100, 44100, 10000.0);
    filter.run(third.data(), 44100, BassCut::reduced, 44100);
    expect(std::fabs(level_db(third, 0, 22050, 10000.0) + 3.01) < 0.2, "a new cut is set for the new rate");
    std::vector<int16_t> fourth = tone(80.0, 48000, 48000, 10000.0);
    filter.run(fourth.data(), 48000, BassCut::reduced, 48000);
    expect(std::fabs(level_db(fourth, 0, 24000, 10000.0) + 3.01) < 0.2, "a new rate alone sets the cut anew");
}

void a_change_of_cut_does_not_click() {
    const int rate = 48000, block = 1024, blocks = 3 * rate / block;
    std::vector<int16_t> stereo = tone(40.0, rate, blocks * block, 20000.0);
    const BassCut order[] = {BassCut::tv_speaker, BassCut::original, BassCut::reduced, BassCut::tv_speaker, BassCut::original};
    BassFilter filter;
    for (int at = 0; at < blocks; ++at)
        filter.run(stereo.data() + 2 * at * block, block, order[at * 5 / blocks], rate);
    int steepest = 0;
    for (int frame = 1; frame < blocks * block; ++frame)
        steepest = std::max(steepest, std::abs(stereo[2 * frame] - stereo[2 * (frame - 1)]));
    const double tone_steepest = 2.0 * std::numbers::pi * 40.0 * 20000.0 / rate;
    expect(steepest < 2.0 * tone_steepest,
           "a loud 40 Hz tone switched between every cut moves no faster than twice the tone itself: no click");
}

void the_cut_is_shared_and_kept_in_range() {
    disruptor_bass_cut_set(2);
    expect(disruptor_bass_cut() == 2, "the chosen cut is kept");
    disruptor_bass_cut_set(9);
    expect(disruptor_bass_cut() == 2, "a cut past the list is held at its last choice");
    disruptor_bass_cut_set(-1);
    expect(disruptor_bass_cut() == 0, "a cut before the list is Original");
    std::vector<int16_t> stereo = tone(40.0, 44100, 4410, 10000.0);
    const std::vector<int16_t> before = stereo;
    disruptor::audio::shape_output(stereo.data(), 4410, 44100);
    expect(stereo == before, "with Original chosen the output is untouched");
    disruptor_bass_cut_set(1);
    disruptor::audio::shape_output(stereo.data(), 4410, 44100);
    expect(stereo != before, "with a cut chosen the output is shaped");
    disruptor_bass_cut_set(0);
}
}  // namespace

int main() {
    original_leaves_the_sound_alone();
    each_cut_takes_the_low_end_and_keeps_the_rest();
    the_channels_are_filtered_apart();
    a_full_scale_edge_does_not_wrap();
    a_change_of_cut_or_rate_takes_effect_in_20_ms();
    a_change_of_cut_does_not_click();
    the_cut_is_shared_and_kept_in_range();
    if (g_failures) return 1;
    std::cout << "bass cut: PASS\n";
    return 0;
}
