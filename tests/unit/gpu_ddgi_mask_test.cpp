#include "gpu_ddgi_mask.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr uint16_t kOpaque = 0x7FFF;
constexpr uint16_t kBlending = 0x8000 | 0x1234;
constexpr uint16_t kFifteenBit = 2 << 7;
constexpr uint16_t kPage = 3 | kFifteenBit; /* page x 192, y 0 */

struct Vram {
    std::vector<uint16_t> words = std::vector<uint16_t>(1024 * 512, 0);
    void put(int x, int y, uint16_t value) { words[(y & 511) * 1024 + (x & 1023)] = value; }
};

// A 64 by 64 picture at the page's corner: opaque over texels [x0, x1) by [y0, y1).
Vram picture(int x0, int x1, int y0, int y1, uint16_t texel = kOpaque) {
    Vram vram;
    for (int v = y0; v < y1; ++v)
        for (int u = x0; u < x1; ++u) vram.put(192 + u, v, texel);
    return vram;
}

GpuDdgiCard card(int left_u, int top_v, int right_u, int bottom_v, uint16_t texpage = kPage, uint16_t clut = 0) {
    GpuDdgiCard c{};
    c.uv[0] = static_cast<int16_t>(left_u);
    c.uv[1] = static_cast<int16_t>(top_v);
    c.uv[2] = static_cast<int16_t>(right_u);
    c.uv[3] = static_cast<int16_t>(bottom_v);
    c.texpage = texpage;
    c.clut = clut;
    return c;
}

int bits(const GpuDdgiCard &c) {
    int count = 0;
    for (const uint32_t row : c.mask)
        for (uint32_t b = row; b; b &= b - 1) ++count;
    return count;
}

// The picture's edges fall between the 16 by 16 grid's samples: the padding must reach them.
void box_round_the_opaque_part() {
    const Vram vram = picture(15, 49, 7, 57);
    GpuDdgiCard c = card(0, 0, 64, 64);
    psx_ddgi_card_look(vram.words.data(), &c, 0, 1);
    const float cell = 1.0f / 16.0f;
    expect(c.box[0] <= 15.0f / 64.0f && c.box[0] >= 15.0f / 64.0f - 2.0f * cell, "the box starts at most two cells left of the picture");
    expect(c.box[2] >= 49.0f / 64.0f && c.box[2] <= 49.0f / 64.0f + 2.0f * cell, "and ends at most two cells right of it");
    expect(c.box[1] <= 7.0f / 64.0f && c.box[3] >= 57.0f / 64.0f, "it holds the picture top to bottom");
    expect(c.box[1] >= 0.0f && c.box[3] <= 1.0f, "and stays inside the card");

    bool matches = true;
    for (int row = 0; row < GPU_DDGI_MASK_ROWS; ++row)
        for (int column = 0; column < 32; ++column) {
            const float fx = c.box[0] + (column + 0.5f) / 32.0f * (c.box[2] - c.box[0]);
            const float fy = c.box[1] + (row + 0.5f) / 32.0f * (c.box[3] - c.box[1]);
            const int u = static_cast<int>(std::floor(fx * 64.0f)), v = static_cast<int>(std::floor(fy * 64.0f));
            const bool inside = u >= 15 && u < 49 && v >= 7 && v < 57;
            matches = matches && inside == (((c.mask[row] >> column) & 1u) != 0);
        }
    expect(matches, "a bit is set where its place over the box is on the picture, and only there");
    expect(c.mask[0] == 0 && c.mask[31] == 0, "the padding's rows are clear");
}

void nothing_to_block() {
    const Vram vram;
    GpuDdgiCard c = card(0, 0, 64, 64);
    c.box[2] = 1.0f;
    c.mask[5] = 7u;
    psx_ddgi_card_look(vram.words.data(), &c, 0, 1);
    expect(c.box[0] == 0.0f && c.box[1] == 0.0f && c.box[2] == 0.0f && c.box[3] == 0.0f, "a clear picture has no box");
    expect(bits(c) == 0, "and no bits, whatever the card held before");
}

void blending_texels() {
    const Vram vram = picture(0, 64, 0, 64, kBlending);
    GpuDdgiCard semi = card(0, 0, 64, 64), solid = card(0, 0, 64, 64);
    psx_ddgi_card_look(vram.words.data(), &semi, 1, 1);
    psx_ddgi_card_look(vram.words.data(), &solid, 0, 1);
    expect(bits(semi) == 0, "a semi-transparent sprite's texels with the mask bit blend: they let light through");
    expect(semi.box[0] == 0.0f && semi.box[2] == 0.0f && semi.bounds[2] == 0.0f, "and such a sprite has no box and no bounds");
    expect(bits(solid) == 32 * 32, "the same texels of a sprite drawn solid block it all");
    expect(psx_ddgi_blocks(0x0001, 1) && !psx_ddgi_blocks(0, 0), "a texel without the mask bit blocks, a clear one never does");
}

void mirrored_card() {
    const Vram vram = picture(0, 16, 0, 64);
    GpuDdgiCard straight = card(0, 0, 64, 64), mirrored = card(63, 0, -1, 64);
    psx_ddgi_card_look(vram.words.data(), &straight, 0, 1);
    psx_ddgi_card_look(vram.words.data(), &mirrored, 0, 1);
    expect(straight.box[0] == 0.0f && straight.box[2] < 0.5f, "the picture's left quarter is the card's left");
    expect(mirrored.box[2] == 1.0f && mirrored.box[0] > 0.5f, "a card drawn mirrored has it on its right");
    expect(std::fabs((1.0f - mirrored.box[0]) - straight.box[2]) < 1.0f / 16.0f, "the two boxes mirror each other");
}

void palettes() {
    Vram vram;
    const uint16_t clut = static_cast<uint16_t>((480 << 6) | (512 / 16)); /* the CLUT at x 512, y 480 */
    vram.put(512 + 0, 480, 0);
    vram.put(512 + 1, 480, 0x03E0);
    vram.put(512 + 200, 480, 0x7C00);
    const uint16_t four = 3, eight = 3 | (1 << 7);
    vram.put(192 + 1, 10, static_cast<uint16_t>(1 << 4)); /* 4-bit texels 4..7 of the word: u 5 is index 1 */
    vram.put(192 + 2, 11, static_cast<uint16_t>(200 << 8)); /* 8-bit texels 4 and 5: u 5 is index 200 */
    expect(psx_ddgi_texel(vram.words.data(), 5, 10, four, clut) == 0x03E0, "a 4-bit texel reads its colour through the CLUT");
    expect(psx_ddgi_texel(vram.words.data(), 4, 10, four, clut) == 0, "index 0 there is clear");
    expect(psx_ddgi_texel(vram.words.data(), 5, 11, eight, clut) == 0x7C00, "an 8-bit texel reads its colour through the CLUT");
    vram.put(192 + 7, 12, kOpaque);
    expect(psx_ddgi_texel(vram.words.data(), 7 + 256, 12 + 256, kPage, 0) == kOpaque, "u and v wrap inside the page");
    const uint16_t lower = static_cast<uint16_t>(kPage | (1 << 4));
    vram.put(192 + 7, 256 + 12, 0x1111);
    expect(psx_ddgi_texel(vram.words.data(), 7, 12, lower, 0) == 0x1111, "a page in the lower half starts at row 256");
}

// A quarter of a 64 by 64 picture red, the left half of it with the mask bit.
void look_as_drawn() {
    Vram vram = picture(0, 32, 0, 32, 0x001F);
    for (int v = 0; v < 32; ++v)
        for (int u = 0; u < 16; ++u) vram.put(192 + u, v, 0x801F);
    GpuDdgiCard plain = card(0, 0, 64, 64);
    psx_ddgi_card_look(vram.words.data(), &plain, 0, 0);
    expect(plain.cover == 0.25f, "cover: a quarter of the samples are not clear");
    expect(plain.blended == 0.5f, "blended: half of those carry the mask bit");
    expect(plain.texels[0] == 1.0f && plain.texels[1] == 0.0f && plain.texels[2] == 0.0f, "their mean colour, red");
    expect(plain.bounds[0] == 0.0f && plain.bounds[1] == 0.0f && plain.bounds[2] == 0.5f && plain.bounds[3] == 0.5f,
           "without a mask the bounds are the texels not clear");
    expect(bits(plain) == 0 && plain.box[2] == 0.0f, "and no mask is taken");

    GpuDdgiCard semi = card(0, 0, 64, 64);
    semi.bounds[0] = 0.9f;
    psx_ddgi_card_look(vram.words.data(), &semi, 1, 1);
    const float cell_x = (semi.box[2] - semi.box[0]) / 32.0f, cell_y = (semi.box[3] - semi.box[1]) / 32.0f;
    uint32_t columns = 0;
    int first_row = -1, last_row = -1;
    for (int row = 0; row < GPU_DDGI_MASK_ROWS; ++row) {
        columns |= semi.mask[row];
        if (semi.mask[row]) last_row = row, first_row = first_row < 0 ? row : first_row;
    }
    int first_column = 0, last_column = 31;
    while (first_column < 32 && !((columns >> first_column) & 1u)) ++first_column;
    while (last_column >= 0 && !((columns >> last_column) & 1u)) --last_column;
    expect(semi.bounds[0] == semi.box[0] + (float)first_column / 32.0f * (semi.box[2] - semi.box[0]) &&
               semi.bounds[2] == semi.box[0] + (float)(last_column + 1) / 32.0f * (semi.box[2] - semi.box[0]) &&
               semi.bounds[1] == semi.box[1] + (float)first_row / 32.0f * (semi.box[3] - semi.box[1]) &&
               semi.bounds[3] == semi.box[1] + (float)(last_row + 1) / 32.0f * (semi.box[3] - semi.box[1]),
           "the bounds are the edges of the mask's outermost cells that are set");
    expect(std::fabs(semi.bounds[0] - 0.25f) <= cell_x && std::fabs(semi.bounds[2] - 0.5f) <= cell_x,
           "with a mask the bounds are the texels that block light: a semi-transparent sprite's red without the mask bit");
    expect(semi.bounds[1] == 0.0f && std::fabs(semi.bounds[3] - 0.5f) <= cell_y, "top to bottom within a cell of the mask");
    expect(semi.cover == 0.25f && semi.blended == 0.5f, "cover and blended are of every texel not clear, blending or not");
    semi.flags = 1;
    plain.flags = 0;
    expect(psx_ddgi_card_blocking(&semi) == 0.125f, "a semi-transparent sprite blocks with its texels without the mask bit");
    expect(psx_ddgi_card_blocking(&plain) == 0.25f, "a sprite drawn solid blocks with all it shows");

    const Vram clear;
    GpuDdgiCard nothing = card(0, 0, 64, 64);
    nothing.cover = nothing.blended = 1.0f;
    nothing.texels[0] = nothing.bounds[2] = 1.0f;
    psx_ddgi_card_look(clear.words.data(), &nothing, 0, 1);
    expect(nothing.cover == 0.0f && nothing.blended == 0.0f && nothing.texels[0] == 0.0f && nothing.bounds[2] == 0.0f,
           "a clear picture has no look, whatever the card held before");
}

void picture_of_the_box() {
    GpuDdgiCard c{};
    c.across[0] = 10.0f;
    c.down[1] = 20.0f;
    c.centre[0] = 100.0f;
    c.centre[1] = -50.0f;
    c.centre[2] = 7.0f;
    c.box[0] = 0.25f;
    c.box[1] = 0.0f;
    c.box[2] = 0.75f;
    c.box[3] = 0.5f;
    float place[3], half_width = 0.0f, half_height = 0.0f;
    psx_ddgi_box_picture(&c, 10.0f, 20.0f, place, &half_width, &half_height);
    expect(place[0] == 100.0f && place[1] == -60.0f && place[2] == 7.0f, "the box's centre: the middle across, the top half down");
    expect(half_width == 5.0f && half_height == 10.0f, "its halves: half the card across, half the card down");
}

}  // namespace

int main() {
    box_round_the_opaque_part();
    nothing_to_block();
    blending_texels();
    mirrored_card();
    palettes();
    look_as_drawn();
    picture_of_the_box();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_ddgi_mask: ok\n";
    return 0;
}
