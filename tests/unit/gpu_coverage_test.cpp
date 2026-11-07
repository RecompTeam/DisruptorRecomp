#include "gpu_coverage.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>

namespace {

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

constexpr int32_t kFar = 1 << 20;
constexpr PsxCoverageArea kFrame{0, 0, 319, 239};

int count(const PsxCoverage &cov, uint8_t cls) {
    int found = 0;
    for (int32_t y = kFrame.top; y <= kFrame.bottom; ++y)
        for (int32_t x = kFrame.left; x <= kFrame.right; ++x) found += cov.cls[y][x] == cls;
    return found;
}

void quad(PsxCoverage &cov, int32_t left, int32_t top, int32_t right, int32_t bottom, uint8_t cls, bool reversed = false) {
    const int32_t ax[3] = {left, right, left}, ay[3] = {top, top, bottom};
    const int32_t bx[3] = {right, left, right}, by[3] = {top, bottom, bottom};
    const int32_t rax[3] = {left, left, right}, ray[3] = {top, bottom, top};
    psx_coverage_triangle(&cov, kFrame, reversed ? rax : ax, reversed ? ray : ay, cls);
    psx_coverage_triangle(&cov, kFrame, bx, by, cls);
}

std::unique_ptr<PsxCoverage> fresh() {
    auto cov = std::make_unique<PsxCoverage>();
    psx_coverage_begin(cov.get(), kFrame);
    return cov;
}

void triangles_fill_as_the_gpu_does() {
    auto cov = fresh();
    quad(*cov, 10, 10, 14, 14, PSX_COVER_WORLD);
    expect(count(*cov, PSX_COVER_WORLD) == 16, "a 4 by 4 quad of two triangles covers 16 pixels, its shared diagonal once");
    expect(cov->cls[10][10] == PSX_COVER_WORLD && cov->cls[13][13] == PSX_COVER_WORLD, "from its top left pixel");
    expect(cov->cls[14][10] == PSX_COVER_NONE && cov->cls[10][14] == PSX_COVER_NONE, "to before its right and bottom edges");
    auto other = fresh();
    quad(*other, 10, 10, 14, 14, PSX_COVER_WORLD, true);
    expect(count(*other, PSX_COVER_WORLD) == 16, "either winding covers the same pixels");
    const int32_t flat_x[3] = {5, 9, 13}, flat_y[3] = {5, 5, 5};
    psx_coverage_triangle(other.get(), kFrame, flat_x, flat_y, PSX_COVER_SKY);
    expect(count(*other, PSX_COVER_SKY) == 0, "a triangle with no area covers nothing");
    const int32_t big_x[3] = {-500, 900, -500}, big_y[3] = {-500, -500, 900};
    psx_coverage_triangle(other.get(), PsxCoverageArea{0, 0, 99, 49}, big_x, big_y, PSX_COVER_FLAT);
    expect(count(*other, PSX_COVER_FLAT) == 100 * 50, "a polygon past the draw area covers only the area");
    auto edge = fresh();
    const int32_t upper_x[3] = {10, 14, 10}, upper_y[3] = {10, 10, 14}, lower_x[3] = {14, 10, 14}, lower_y[3] = {10, 14, 14};
    psx_coverage_triangle(edge.get(), kFrame, upper_x, upper_y, PSX_COVER_WORLD);
    expect(count(*edge, PSX_COVER_WORLD) == 6, "a pixel whose centre lies on a right or bottom edge is left to the face beyond it");
    psx_coverage_triangle(edge.get(), kFrame, lower_x, lower_y, PSX_COVER_SKY);
    expect(count(*edge, PSX_COVER_SKY) == 10 && count(*edge, PSX_COVER_WORLD) == 6, "which takes it on its top or left edge");
    expect(cov->dirty, "a draw leaves the sums to make again");
}

void the_widened_walk_fills_only_the_void() {
    auto cov = fresh();
    quad(*cov, 0, 0, 10, 10, PSX_COVER_WORLD);
    quad(*cov, 5, 0, 15, 10, PSX_COVER_REACH);
    expect(cov->cls[5][7] == PSX_COVER_WORLD && cov->cls[5][12] == PSX_COVER_REACH, "a face of the widened walk never covers the game's own");
    quad(*cov, 10, 0, 20, 10, PSX_COVER_WORLD);
    expect(cov->cls[5][12] == PSX_COVER_WORLD, "and the game's own covers it");
    psx_coverage_rect(cov.get(), kFrame, 30, 30, 4, 2, PSX_COVER_SKY);
    psx_coverage_rect(cov.get(), kFrame, 31, 29, 4, 4, PSX_COVER_REACH);
    expect(cov->cls[30][31] == PSX_COVER_SKY && cov->cls[29][31] == PSX_COVER_REACH && cov->cls[32][34] == PSX_COVER_REACH,
           "rectangles keep the same rule");
    psx_coverage_begin(cov.get(), kFrame);
    expect(count(*cov, PSX_COVER_NONE) == 320 * 240 && !cov->built, "a new frame buffer starts empty and unanswered");
}

void a_tick_is_cut_and_hidden_sky_is_not() {
    PsxCoveragePiece pieces[PSX_COVERAGE_PIECES];

    auto hidden = fresh();
    psx_coverage_rect(hidden.get(), kFrame, 148, 60, 48, 60, PSX_COVER_SKY);
    quad(*hidden, 140, 0, 320, 240, PSX_COVER_WORLD);
    psx_coverage_build(hidden.get());
    expect(psx_coverage_pieces(hidden.get(), 157, 346, 0, 240, 196, kFar, pieces) == 0,
           "a wall over a sky tile the game hides behind it is drawn whole, however far it reaches past its span");

    auto window = fresh();
    psx_coverage_rect(window.get(), kFrame, 100, 60, 97, 60, PSX_COVER_SKY);
    quad(*window, 194, 0, 300, 240, PSX_COVER_WORLD);
    psx_coverage_build(window.get());
    expect(psx_coverage_sky_near(window.get(), 196, 0, 239) && !psx_coverage_sky_near(window.get(), 197, 0, 239),
           "sky shows within three columns of the window's last visible one");
    expect(!psx_coverage_sky_near(window.get(), 195, 0, 59) && psx_coverage_sky_near(window.get(), 195, 119, 130),
           "and only in the rows the window spans");
    const int ticked = psx_coverage_pieces(window.get(), 194, 300, 0, 240, 196, kFar, pieces);
    expect(ticked == 3 && !pieces[0].outside && pieces[0].high == 194 && pieces[1].outside && pieces[1].low == 194 &&
               pieces[1].high == 196 && !pieces[2].outside && pieces[2].low == 196 && pieces[2].high == kFar,
           "the two columns a face overhangs a visible window by are a tick, the rest of it the game's");
    expect(psx_coverage_pieces(window.get(), 194, 300, 0, 50, 196, kFar, pieces) == 0, "a face beside the window's rows has none");

    auto in_front = fresh();
    psx_coverage_rect(in_front.get(), kFrame, 100, 60, 150, 60, PSX_COVER_SKY);
    quad(*in_front, 200, 0, 300, 240, PSX_COVER_WORLD);
    psx_coverage_build(in_front.get());
    expect(psx_coverage_pieces(in_front.get(), 200, 300, 0, 240, 260, kFar, pieces) == 0,
           "a wall standing before part of a window, its edge far from its span's, keeps its edge");

    auto right = fresh();
    psx_coverage_rect(right.get(), kFrame, 152, 60, 100, 60, PSX_COVER_SKY);
    quad(*right, 20, 0, 153, 240, PSX_COVER_WORLD);
    psx_coverage_build(right.get());
    const int on_right = psx_coverage_pieces(right.get(), 20, 153, 0, 240, -kFar, 150, pieces);
    expect(on_right == 3 && !pieces[0].outside && pieces[0].low == -kFar && pieces[0].high == 150 && pieces[1].outside &&
               pieces[1].low == 150 && pieces[1].high == 153 && !pieces[2].outside,
           "past a right edge the overhang is the tick, cut on the edge itself");
    expect(psx_coverage_pieces(right.get(), 20, 140, 0, 240, -kFar, 150, pieces) == 0, "a face inside its span is never cut");
}

void broken_sky_cuts_alternate_and_never_spill() {
    auto cov = fresh();
    quad(*cov, 0, 0, 320, 240, PSX_COVER_WORLD);
    for (const int32_t column : {194, 202, 257, 265}) psx_coverage_rect(cov.get(), kFrame, column, 60, 1, 60, PSX_COVER_SKY);
    psx_coverage_build(cov.get());
    PsxCoveragePiece pieces[PSX_COVERAGE_PIECES];
    const int count = psx_coverage_pieces(cov.get(), 150, 300, 0, 240, 200, 260, pieces);
    const int32_t lows[9] = {-kFar, 197, 198, 199, 200, 260, 261, 262, 263};
    bool shaped = count == 9;
    for (int i = 0; shaped && i < 9; ++i) shaped = pieces[i].low == lows[i] && pieces[i].outside == (i % 2 == 1);
    expect(shaped && pieces[8].high == kFar, "ticks every other column past both edges are nine pieces, the last to the far side the game's");
    PsxCoveragePiece room[PSX_COVERAGE_PIECES];
    int filled = 0;
    for (int i = 0; i < PSX_COVERAGE_PIECES + 3; ++i) filled = psx_coverage_piece(room, filled, i, i + 1, i % 2);
    expect(filled == PSX_COVERAGE_PIECES + 1, "more pieces than room are told as such, never merged into a cut");
}

void drawn_sky_tiles_are_kept_by_packet() {
    PsxCoverageSkyKeys drawn{};
    psx_coverage_sky_keys_reset(&drawn);
    psx_coverage_sky_keys_add(&drawn, 0x1234u);
    psx_coverage_sky_keys_add(&drawn, 0x5678u);
    expect(psx_coverage_sky_keys_has(&drawn, 0x1234u) && psx_coverage_sky_keys_has(&drawn, 0x5678u) && !psx_coverage_sky_keys_has(&drawn, 0x9ABCu),
           "a drawn tile is known by its packet after the sky fog forgot it");
    for (uint32_t key = 0; key < PSX_COVERAGE_SKY_KEYS + 10; ++key) psx_coverage_sky_keys_add(&drawn, 0x10000u + key * 4u);
    expect(drawn.count == PSX_COVERAGE_SKY_KEYS && !psx_coverage_sky_keys_has(&drawn, 0x10000u + (PSX_COVERAGE_SKY_KEYS + 5) * 4u),
           "past the table a tile is not known, so it cuts nothing");
    psx_coverage_sky_keys_reset(&drawn);
    expect(!psx_coverage_sky_keys_has(&drawn, 0x1234u), "a new frame buffer forgets the last one's tiles");
}
void the_bit_answers_match_a_plain_count() {
    uint32_t seed = 12345u;
    auto next = [&seed](uint32_t below) {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) % below;
    };
    for (int round = 0; round < 6; ++round) {
        auto cov = std::make_unique<PsxCoverage>();
        const PsxCoverageArea area{static_cast<int32_t>(3 + next(60)), static_cast<int32_t>(next(200)),
                                   static_cast<int32_t>(250 + next(500)), static_cast<int32_t>(250 + next(250))};
        psx_coverage_begin(cov.get(), area);
        for (int r = 0; r < 40; ++r) {
            const int32_t x = area.left - 10 + static_cast<int32_t>(next(static_cast<uint32_t>(area.right - area.left + 20)));
            const int32_t y = area.top - 10 + static_cast<int32_t>(next(static_cast<uint32_t>(area.bottom - area.top + 20)));
            psx_coverage_rect(cov.get(), area, x, y, static_cast<int32_t>(1 + next(40)), static_cast<int32_t>(1 + next(40)),
                              static_cast<uint8_t>(next(5)));
        }
        psx_coverage_build(cov.get());
        for (int q = 0; q < 3000; ++q) {
            const int32_t x = area.left - 5 + static_cast<int32_t>(next(static_cast<uint32_t>(area.right - area.left + 10)));
            const int32_t top = area.top - 5 + static_cast<int32_t>(next(static_cast<uint32_t>(area.bottom - area.top + 10)));
            const int32_t bottom = top + static_cast<int32_t>(next(120));
            bool sky = false;
            for (int32_t y = top < area.top ? area.top : top; y <= (bottom > area.bottom ? area.bottom : bottom) && !sky; ++y)
                for (int32_t d = -PSX_COVERAGE_REACH_PX; d <= PSX_COVERAGE_REACH_PX && !sky; ++d)
                    sky = x >= area.left && x <= area.right && x + d >= area.left && x + d <= area.right &&
                          cov->cls[y][x + d] == PSX_COVER_SKY;
            expect(psx_coverage_sky_near(cov.get(), x, top, bottom) == (sky ? 1 : 0), "sky near agrees with a plain search");
        }
    }
}

}  // namespace

int main() {
    triangles_fill_as_the_gpu_does();
    the_widened_walk_fills_only_the_void();
    a_tick_is_cut_and_hidden_sky_is_not();
    broken_sky_cuts_alternate_and_never_spill();
    drawn_sky_tiles_are_kept_by_packet();
    the_bit_answers_match_a_plain_count();
    std::puts("GPU coverage tests passed");
    return 0;
}
