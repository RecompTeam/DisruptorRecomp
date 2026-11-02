#include "gpu_texture_row.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr int kX = 0x1F0, kY = 480, kWords = 16;

/* Rows 480 and 481 at x 0x1F0 in a VRAM dump of the retail game at the corridor's tall figure. */
constexpr std::uint16_t kRetail[2][kWords] = {
    {0x032D, 0x8988, 0x1746, 0xBC17, 0x2D2D, 0xBC2D, 0x17BC, 0x8365, 0x8317, 0x0365, 0x8303, 0x6517, 0x6565, 0x89AC, 0x0765, 0x1783},
    {0x864F, 0x5B4F, 0x5718, 0x5757, 0x2EAE, 0x572E, 0x572E, 0x1818, 0x2E4F, 0x1811, 0x2E4F, 0x4F57, 0x0918, 0x094F, 0x4909, 0x0B4C},
};

std::uint16_t &at(std::vector<std::uint16_t> &vram, int x, int y) { return vram[static_cast<std::size_t>(y) * 1024u + x]; }

/* Video memory with the texture page as shipped around the two rows. */
std::vector<std::uint16_t> shipped() {
    std::vector<std::uint16_t> vram(1024u * 512u, 0x1234u);
    for (int k = 0; k < kWords; k++) {
        at(vram, kX + k, kY) = kRetail[0][k];
        at(vram, kX + k, kY + 1) = kRetail[1][k];
    }
    return vram;
}

void test_the_rows_are_where_the_header_says() {
    expect(PSX_TEXTURE_ROW_X == kX && PSX_TEXTURE_ROW_Y == kY && PSX_TEXTURE_ROW_WORDS == kWords,
           "page 0x97's tile at u 0x60, v 0xE0 starts at word (0x1F0, 480), sixteen words wide");
}

void test_the_light_row_takes_the_next() {
    std::vector<std::uint16_t> vram = shipped();
    const std::vector<std::uint16_t> before = vram;
    expect(psx_texture_row_fix(vram.data(), 448, 256, 64, 256) == 1, "a page upload over the tile fixes it");
    bool row = true;
    for (int k = 0; k < kWords; k++) row &= at(vram, kX + k, kY) == kRetail[1][k];
    expect(row, "the light row holds the next row's texels");
    int changed = 0;
    for (std::size_t i = 0; i < vram.size(); i++) changed += vram[i] != before[i];
    expect(changed == kWords, "nothing but the light row's sixteen words changes");
    expect(psx_texture_row_fix(vram.data(), 448, 256, 64, 256) == 0, "a fixed row is not fixed again");
}

void test_uploads_beside_the_rows_are_left_alone() {
    struct Rect {
        int x, y, w, h;
        int fixed;
        const char *message;
    };
    const Rect rects[] = {
        {kX, kY, 1, 1, 1, "the light row's first word"},
        {kX + kWords - 1, kY + 1, 1, 1, 1, "the next row's last word"},
        {kX + kWords, kY, 8, 2, 0, "right of the tile"},
        {kX - 8, kY, 8, 2, 0, "left of the tile"},
        {kX, kY + 2, kWords, 4, 0, "below the next row"},
        {kX, kY - 4, kWords, 4, 0, "above the light row"},
    };
    for (const Rect &rect : rects) {
        std::vector<std::uint16_t> vram = shipped();
        expect(psx_texture_row_fix(vram.data(), rect.x, rect.y, rect.w, rect.h) == rect.fixed, rect.message);
    }
}

void test_other_texels_are_left_alone() {
    std::vector<std::uint16_t> light = shipped();
    at(light, kX + 7, kY) ^= 1u;
    const std::vector<std::uint16_t> before_light = light;
    expect(psx_texture_row_fix(light.data(), 448, 256, 64, 256) == 0 && light == before_light,
           "a light row that is not the shipped one is not written");
    std::vector<std::uint16_t> next = shipped();
    at(next, kX + kWords - 1, kY + 1) ^= 1u;
    const std::vector<std::uint16_t> before_next = next;
    expect(psx_texture_row_fix(next.data(), 448, 256, 64, 256) == 0 && next == before_next,
           "nor one whose next row is not the shipped one");
}

}  // namespace

int main() {
    test_the_rows_are_where_the_header_says();
    test_the_light_row_takes_the_next();
    test_uploads_beside_the_rows_are_left_alone();
    test_other_texels_are_left_alone();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_texture_row: PASS\n";
    return 0;
}
