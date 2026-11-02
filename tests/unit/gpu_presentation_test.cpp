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

void test_the_log_numbers_pictures_from_its_start() {
    auto log = std::make_unique<PsxShownLog>();
    psx_shown_log_begin(log.get());
    expect(psx_shown_log_add(log.get(), 0u, 0, 1.0f, 3840, 2160) == 0u, "the first picture is number 0");
    expect(psx_shown_log_add(log.get(), 1u, 1, 0.25f, 3840, 2160) == 1u, "the next one 1");
    PsxShownPicture out[4]{};
    expect(psx_shown_log_take(log.get(), out, 4u) == 2u, "both are taken");
    expect(out[0].frame == 0u && out[0].kind == 0u && out[0].alpha == 1.0f, "a game frame's own swap in the log's first VBlank");
    expect(out[1].sequence == 1u && out[1].frame == 1u && out[1].kind == 1u && out[1].alpha == 0.25f && out[1].width == 3840u &&
               out[1].height == 2160u,
           "an in-between picture keeps its game frame, its place and the window's size");
    expect(psx_shown_log_take(log.get(), out, 4u) == 0u, "a picture is taken once");
}

void test_a_full_log_counts_the_lost_and_keeps_numbering() {
    auto log = std::make_unique<PsxShownLog>();
    psx_shown_log_begin(log.get());
    for (uint32_t i = 0; i < PSX_SHOWN_LOG_SIZE + 3u; ++i) psx_shown_log_add(log.get(), i, 1, 0.5f, 640, 360);
    expect(log->lost == 3u, "pictures past a full ring are counted lost");
    std::vector<PsxShownPicture> out(PSX_SHOWN_LOG_SIZE);
    expect(psx_shown_log_take(log.get(), out.data(), PSX_SHOWN_LOG_SIZE) == PSX_SHOWN_LOG_SIZE, "the ring gives what it kept");
    expect(psx_shown_log_add(log.get(), 0u, 1, 0.5f, 640, 360) == PSX_SHOWN_LOG_SIZE + 3u, "the next picture's number counts the lost ones");
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
    expect(psx_shown_script_step(&script, 0u, 1, &alpha) == PSX_SHOWN_WAIT, "it waits for the game frame its first picture follows");
    expect(!psx_shown_script_drained(&script, 1u) && psx_shown_script_drained(&script, 0u),
           "the game frame after the waiting one may come, the one after a picture still owed may not");
    expect(psx_shown_script_step(&script, 1u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.25f, "then the first picture at its place");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 1u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.75f, "then the second one of that frame");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_drained(&script, 1u), "with both shown the next game frame may come");
    expect(psx_shown_script_step(&script, 1u, 1, &alpha) == PSX_SHOWN_WAIT, "and nothing more is drawn before it");
    expect(psx_shown_script_step(&script, 2u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.5f, "the next frame's picture");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 3u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 1.0f, "a game frame's own swap shows that frame");
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 4u, 1, &alpha) == PSX_SHOWN_DONE && script.shown == 4u && script.missed == 0u,
           "the script ends with every picture shown");
}

void test_a_game_frames_own_swap_shows_that_frame() {
    std::vector<PsxShownPicture> pictures = {{0u, 2u, 0.3f, 640, 360, 0u}};
    PsxShownScript script{pictures.data(), 1u, 0, 0, 0, 0, 0, 0, 0};
    float alpha = -1.0f;
    expect(psx_shown_script_step(&script, 2u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 1.0f,
           "a swap the game made itself shows the later frame whatever place it was logged with");
}

void test_pictures_it_can_no_longer_draw_are_missed() {
    std::vector<PsxShownPicture> pictures = script_of({{0u, 0.5f}, {1u, 0.5f}, {2u, 0.3f}, {2u, 0.6f}, {4u, 0.5f}});
    PsxShownScript script{pictures.data(), static_cast<uint32_t>(pictures.size()), 0, 0, 0, 0, 0, 0, 0};
    float alpha = -1.0f;
    expect(psx_shown_script_step(&script, 0u, 0, &alpha) == PSX_SHOWN_WAIT && script.missed == 1u,
           "a picture of a game frame the presenter cannot place yet is missed");
    expect(psx_shown_script_step(&script, 2u, 1, &alpha) == PSX_SHOWN_PRESENT && alpha == 0.3f && script.missed == 2u,
           "a picture of a game frame already replaced is missed, the current frame's is drawn");
    psx_shown_script_presented(&script);
    psx_shown_script_presented(&script);
    expect(psx_shown_script_step(&script, 3u, 1, &alpha) == PSX_SHOWN_WAIT && psx_shown_script_drained(&script, 3u),
           "a game frame with no picture of its own lets the next come at once");
    expect(psx_shown_script_step(&script, 5u, 1, &alpha) == PSX_SHOWN_DONE && script.missed == 3u && script.shown == 2u,
           "a frame passed by is missed too");
    expect(script.late == 1u, "only the picture missed after one was shown counts as the replay falling behind");
}

}  // namespace

int main() {
    test_the_log_numbers_pictures_from_its_start();
    test_a_full_log_counts_the_lost_and_keeps_numbering();
    test_a_frame_is_named_by_its_vblank();
    test_the_script_presents_each_picture_at_its_game_frame();
    test_pictures_it_can_no_longer_draw_are_missed();
    test_a_game_frames_own_swap_shows_that_frame();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "GPU presentation log and script: PASS\n";
    return 0;
}
