#include "gpu_presentation.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    if (g_failures <= 20) std::cerr << "FAIL: " << message << '\n';
}

void test_live_display_reserves_a_native_slot() {
    PsxLivePresentation plan{};
    expect(psx_live_presentation_sample(&plan, 1, 16667, 8333, 1) == 0.5f,
           "a 120 Hz display gets a single midpoint for a 60 Hz capture");
    expect(psx_live_presentation_sample(&plan, 1, 16667, 8333, 1) == 1.0f,
           "the second slot is reserved for the game image");
    psx_live_presentation_shown(&plan, 1, 1.0f);
    expect(psx_live_presentation_sample(&plan, 2, 16667, 8333, 0) == 1.0f,
           "a busy render pipeline gives the slot directly to the game image");
    psx_live_presentation_shown(&plan, 2, 1.0f);
    expect(psx_live_presentation_sample(&plan, 2, 16667, 8333, 1) == 1.0f,
           "a pair that already showed its endpoint cannot generate a frame behind it");
    expect(psx_live_presentation_sample(&plan, 3, 16667, 8333, 1) == 0.5f,
           "the next ready capture can interpolate again");
    expect(psx_live_presentation_sample(&plan, 4, 16667, 8333, 1) == 1.0f,
           "a missed endpoint gives the next available slot to the game");
}

void test_live_display_carries_fractional_slots() {
    PsxLivePresentation plan{};
    unsigned shown = 0;
    for (uint64_t capture = 1; capture <= 5; ++capture) {
        for (int slot = 0; slot < 4; ++slot) {
            const float alpha = psx_live_presentation_sample(&plan, capture, 16667, 6944, 1);
            ++shown;
            psx_live_presentation_shown(&plan, capture, alpha);
            if (alpha == 1.0f) break;
        }
        expect(plan.native_capture == capture, "every live pair offers an endpoint");
    }
    expect(shown == 12, "five 60 Hz captures use twelve slots on a 144 Hz display");
}

void test_live_display_smooths_capture_jitter() {
    constexpr uint64_t display_period = 8333;
    constexpr uint64_t source_period = 2 * display_period;
    constexpr uint64_t jitter = 4300;
    PsxLivePresentation plan{};
    uint64_t capture = 0;
    for (const uint64_t elapsed : {source_period, source_period - jitter, source_period + jitter, source_period}) {
        ++capture;
        expect(psx_live_presentation_sample(&plan, capture, elapsed, display_period, 1) == 0.5f,
               "one short interval and its correction preserve the midpoint");
        expect(psx_live_presentation_sample(&plan, capture, elapsed, display_period, 1) == 1.0f,
               "the smoothed plan still reserves an endpoint");
        psx_live_presentation_shown(&plan, capture, 1.0f);
    }
}

void test_live_display_preserves_endpoints_when_the_source_rate_changes() {
    constexpr uint64_t display_period = 16667;
    constexpr uint64_t slow_period = 2 * display_period;
    for (const bool previous_shown : {false, true}) {
        PsxLivePresentation plan{};
        expect(psx_live_presentation_sample(&plan, 1, slow_period, display_period, 1) == 0.5f,
               "a 30 Hz frame has room for a midpoint at 60 Hz");
        if (previous_shown) psx_live_presentation_shown(&plan, 1, 1.0f);
        expect(psx_live_presentation_previous_due(&plan, 2, display_period, display_period) == !previous_shown,
               "a pending endpoint survives the change from 30 to 60 Hz");
        psx_live_presentation_shown(&plan, 1, 1.0f);
        expect(psx_live_presentation_sample(&plan, 2, display_period, display_period, 1) == 1.0f,
               "the faster source leaves no extra slot even while its estimate catches up");
        psx_live_presentation_shown(&plan, 2, 1.0f);

        bool has_midpoint = false;
        for (uint64_t capture = 3; capture < 8; ++capture) {
            expect(!psx_live_presentation_previous_due(&plan, capture, slow_period, display_period),
                   "a shown endpoint is not repeated when the source slows again");
            if (psx_live_presentation_sample(&plan, capture, slow_period, display_period, 1) < 1.0f) {
                has_midpoint = true;
                expect(psx_live_presentation_sample(&plan, capture, slow_period, display_period, 1) == 1.0f,
                       "the slower source still reserves a native slot");
            }
            psx_live_presentation_shown(&plan, capture, 1.0f);
        }
        expect(has_midpoint, "interpolation resumes at the slower source rate");
    }
}

void test_live_display_retimes_only_an_early_quiet_slot() {
    PsxLivePresentation plan{};
    psx_live_presentation_sample(&plan, 10, 16000, 8000, 1);
    expect(psx_live_presentation_idle_slot(&plan, 122000, 100000, 11000, 2000, 1000) == 128000,
           "an early empty slot can follow the recent time a normal capture needs");
    expect(psx_live_presentation_idle_slot(&plan, 122000, 100000, 22000, 2000, 1000) == 129000,
           "a late capture does not consume the time reserved for a native frame");
    expect(psx_live_presentation_idle_slot(&plan, 121100, 100000, 22000, 2000, 1000) == 129000,
           "an unused slot can be probed just before the next original slot");
    expect(psx_live_presentation_idle_slot(&plan, 125000, 100000, 9000, 2000, 1000) == 0,
           "the existing short retry serves a nearly aligned slot");
    expect(psx_live_presentation_idle_slot(&plan, 116500, 100000, 11000, 2000, 1000) == 0,
           "retiming cannot consume an entire display interval");
    psx_live_presentation_sample(&plan, 30, 16000, 10000, 1);
    expect(psx_live_presentation_idle_slot(&plan, 122000, 100000, 11000, 2000, 1000) == 0,
           "cadences outside the two-to-one window keep their existing slot clock");
    for (const uint64_t source : {14999u, 15000u, 17000u, 17001u}) {
        plan = {};
        psx_live_presentation_sample(&plan, 1, source, 8000, 1);
        const bool adjusted = psx_live_presentation_idle_slot(&plan, 122000, 100000, 11000, 2000, 1000) != 0;
        expect(adjusted == (source == 15000 || source == 17000), "the timing window includes both boundaries");
    }
}

void test_the_log_numbers_pictures_from_its_start() {
    auto log = std::make_unique<PsxShownLog>();
    psx_shown_log_begin(log.get());
    expect(psx_shown_log_add(log.get(), 0u, 0, 1.0f, 3840, 2160, 0u) == 0u, "the first picture is number 0");
    expect(psx_shown_log_add(log.get(), 1u, 1, 0.25f, 3840, 2160, 8333u) == 1u, "the next one 1");
    PsxShownPicture out[4]{};
    expect(psx_shown_log_take(log.get(), out, 4u) == 2u, "both are taken");
    expect(out[0].frame == 0u && out[0].kind == 0u && out[0].alpha == 1.0f, "a game frame's own swap in the log's first VBlank");
    expect(out[1].sequence == 1u && out[1].frame == 1u && out[1].kind == 1u && out[1].alpha == 0.25f && out[1].width == 3840u &&
               out[1].height == 2160u && out[0].micros == 0u && out[1].micros == 8333u,
           "an in-between picture keeps its game frame, its place, the window's size and its time");
    expect(psx_shown_micros(10000000u + 83330u, 10000000u, 10000000u) == 8333u && psx_shown_micros(48000u * 7200u, 0u, 48000u) == 7200000000u,
           "a counter's reading is microseconds since the log began, at any frequency and past an hour");
    expect(psx_shown_micros(5u, 9u, 10000000u) == 0u && psx_shown_micros(9u, 5u, 0u) == 0u, "a reading before the beginning or no frequency is 0, never a wrapped time");
    expect(psx_shown_log_take(log.get(), out, 4u) == 0u, "a picture is taken once");
}

void test_a_full_log_counts_the_lost_and_keeps_numbering() {
    auto log = std::make_unique<PsxShownLog>();
    psx_shown_log_begin(log.get());
    for (uint32_t i = 0; i < PSX_SHOWN_LOG_SIZE + 3u; ++i) psx_shown_log_add(log.get(), i, 1, 0.5f, 640, 360, i);
    expect(log->lost == 3u, "pictures past a full ring are counted lost");
    std::vector<PsxShownPicture> out(PSX_SHOWN_LOG_SIZE);
    expect(psx_shown_log_take(log.get(), out.data(), PSX_SHOWN_LOG_SIZE) == PSX_SHOWN_LOG_SIZE, "the ring gives what it kept");
    expect(psx_shown_log_add(log.get(), 0u, 1, 0.5f, 640, 360, 0u) == PSX_SHOWN_LOG_SIZE + 3u, "the next picture's number counts the lost ones");
    PsxShownPicture one{};
    expect(psx_shown_log_take(log.get(), &one, 1u) == 1u && one.sequence == PSX_SHOWN_LOG_SIZE + 3u, "and it is kept once there is room");
}

void test_a_frame_is_named_by_its_vblank() {
    expect(psx_shown_frame_of(1000u + 3u * 564480u + 7u, 1000u, 564480u) == 3u, "a present within the fourth VBlank's period is frame 3");
    expect(psx_shown_frame_of(1000u + 564480u, 1000u, 564480u) == 1u && psx_shown_frame_of(1000u + 564479u, 1000u, 564480u) == 0u,
           "a VBlank's first cycle begins its frame");
    expect(psx_shown_frame_of(900u, 1000u, 564480u) == 0u && psx_shown_frame_of(5000u, 1000u, 0u) == 0u,
           "a clock before the first VBlank or no period is frame 0, never a wrapped one");
}

std::vector<PsxShownPicture> script_of(std::initializer_list<std::pair<uint32_t, float>> pictures) {
    std::vector<PsxShownPicture> out;
    uint32_t sequence = 0;
    for (const auto &[frame, alpha] : pictures) out.push_back({sequence++, frame, alpha, 640, 360, static_cast<uint8_t>(alpha < 1.0f)});
    return out;
}

void test_the_script_presents_each_picture_at_its_game_frame() {
    std::vector<PsxShownPicture> pictures = script_of({{1u, 0.25f}, {1u, 0.75f}, {2u, 0.5f}, {3u, 1.0f}});
    PsxShownScript script{pictures.data(), static_cast<uint32_t>(pictures.size()), 0, 0, 0, 0, 0, 0, 0};
    float alpha = -1.0f;
    expect(psx_shown_script_step(&script, 0u, 2, &alpha) == PSX_SHOWN_WAIT, "it waits for the game frame its first picture follows");
    expect(!psx_shown_script_drained(&script, 1u) && psx_shown_script_drained(&script, 0u),
           "the game frame after the waiting one may come, the one after a picture still owed may not");
    expect(psx_shown_script_step(&script, 1u, 2, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.25f, "then the first picture at its place");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 1u, 2, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.75f, "then the second one of that frame");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_drained(&script, 1u), "with both shown the next game frame may come");
    expect(psx_shown_script_step(&script, 1u, 2, &alpha) == PSX_SHOWN_WAIT, "and nothing more is drawn before it");
    expect(psx_shown_script_step(&script, 2u, 2, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.5f, "the next frame's picture");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 3u, 2, &alpha) == PSX_SHOWN_PRESENT && alpha == 1.0f, "a game frame's own swap shows that frame");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 4u, 2, &alpha) == PSX_SHOWN_DONE && script.shown == 4u && script.missed == 0u,
           "the script ends with every picture shown");
}

void test_a_game_frames_own_swap_shows_that_frame() {
    std::vector<PsxShownPicture> pictures = {{0u, 2u, 0.3f, 640, 360, 0u}};
    PsxShownScript script{pictures.data(), 1u, 0, 0, 0, 0, 0, 0, 0};
    float alpha = -1.0f;
    expect(psx_shown_script_step(&script, 2u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 1.0f,
           "a swap the game made itself shows the later frame whatever place it was logged with");
}

void test_one_retained_frame_cannot_interpolate() {
    std::vector<PsxShownPicture> pictures = script_of({{2u, 0.5f}, {2u, 1.0f}});
    PsxShownScript script{pictures.data(), 2u, 0, 0, 0, 0, 0, 0, 0};
    float alpha = -1.0f;
    expect(psx_shown_script_step(&script, 2u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 1.0f && script.missed == 1u,
           "one retained frame can show an endpoint but not an intermediate picture");
}

void test_a_direct_swap_consumes_its_saved_picture_once() {
    std::vector<PsxShownPicture> pictures = {{0u, 0u, 1.0f}, {1u, 0u, 1.0f, 640, 360, 1u}};
    PsxShownScript script{pictures.data(), 2u, 0, 0, 0, 0, 0, 0, 0};
    expect(psx_shown_script_native_presented(&script, 0u) && script.shown == 1u && script.next == 1u,
           "the direct swap consumes the saved game frame");
    expect(!psx_shown_script_native_presented(&script, 0u) && script.shown == 1u,
           "the next recorded presenter picture remains distinct");
}

void test_pictures_it_can_no_longer_draw_are_missed() {
    std::vector<PsxShownPicture> pictures = script_of({{0u, 0.5f}, {1u, 0.5f}, {2u, 0.3f}, {2u, 0.6f}, {4u, 0.5f}});
    PsxShownScript script{pictures.data(), static_cast<uint32_t>(pictures.size()), 0, 0, 0, 0, 0, 0, 0};
    float alpha = -1.0f;
    expect(psx_shown_script_step(&script, 0u, 0, &alpha) == PSX_SHOWN_WAIT && script.next == 0u && script.missed == 0u,
           "the first saved frame waits for a snapshot without being missed");
    expect(psx_shown_script_step(&script, 2u, 2, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.3f && script.missed == 2u,
           "a picture of a game frame already replaced is missed, the current frame's is drawn");
    psx_shown_script_presented(&script);
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 3u, 2, &alpha) == PSX_SHOWN_WAIT && psx_shown_script_drained(&script, 3u),
           "a game frame with no picture of its own lets the next come at once");
    expect(psx_shown_script_step(&script, 5u, 2, &alpha) == PSX_SHOWN_DONE && script.missed == 3u && script.shown == 2u,
           "a frame passed by is missed too");
    expect(script.late == 1u, "only the picture missed after one was shown counts as the replay falling behind");
}

}  // namespace

int main() {
    test_live_display_reserves_a_native_slot();
    test_live_display_carries_fractional_slots();
    test_live_display_smooths_capture_jitter();
    test_live_display_preserves_endpoints_when_the_source_rate_changes();
    test_live_display_retimes_only_an_early_quiet_slot();
    test_the_log_numbers_pictures_from_its_start();
    test_a_full_log_counts_the_lost_and_keeps_numbering();
    test_a_frame_is_named_by_its_vblank();
    test_the_script_presents_each_picture_at_its_game_frame();
    test_pictures_it_can_no_longer_draw_are_missed();
    test_a_game_frames_own_swap_shows_that_frame();
    test_one_retained_frame_cannot_interpolate();
    test_a_direct_swap_consumes_its_saved_picture_once();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "GPU presentation log and script: PASS\n";
    return 0;
}
