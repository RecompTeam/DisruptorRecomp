#include "game/render/near_split.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace disruptor::near_split;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

uint32_t packed(uint32_t x, uint32_t y, uint32_t z) { return x << 20 | y << 10 | z; }

void test_the_middle_point_sits_on_the_shared_diagonal() {
    const std::array<uint32_t, 2> centre =
        centre_on_diagonal(packed(10u, 20u, 30u), packed(30u, 40u, 50u), {100u, 1000u, 2000u});
    expect(centre[0] == 220u && centre[1] == (520u | 1360u << 16),
           "fields in 16ths, x less the camera's, y and z from it");
    const std::array<uint32_t, 2> far =
        centre_on_diagonal(packed(1000u, 1001u, 1002u), packed(1000u, 1001u, 1002u), {0u, 20000u, 20000u});
    expect(far[0] == 16000u && far[1] == ((20000u - 16016u) | (20000u - 16032u) << 16), "every field's top bit counts");
    const std::array<uint32_t, 2> behind = centre_on_diagonal(packed(1u, 1u, 1u), packed(1u, 1u, 1u), {100u, 0u, 0u});
    expect(behind[0] == static_cast<uint32_t>(16 - 100) && (behind[1] & 0xFFFFu) == 0xFFF0u,
           "a point behind the camera's coordinate wraps as the game's subtraction does");
    const std::array<uint32_t, 4> parallelogram = {packed(0u, 0u, 0u), packed(40u, 8u, 4u), packed(60u, 48u, 24u),
                                                   packed(20u, 40u, 20u)};
    uint32_t x = 0, y = 0, z = 0;
    for (const uint32_t corner : parallelogram) x += field_x(corner), y += field_y(corner), z += field_z(corner);
    expect(centre_on_diagonal(parallelogram[1], parallelogram[3], {7u, 900u, 900u}) ==
               split_point(x, y, z, 2u, {7u, 900u, 900u}),
           "on a parallelogram it is the game's four-corner average");
    const std::array<uint32_t, 4> trapezoid = {packed(0u, 0u, 0u), packed(64u, 0u, 0u), packed(48u, 32u, 0u),
                                               packed(16u, 32u, 0u)};
    x = y = z = 0;
    for (const uint32_t corner : trapezoid) x += field_x(corner), y += field_y(corner), z += field_z(corner);
    expect(centre_on_diagonal(trapezoid[1], trapezoid[3], {0u, 900u, 900u}) !=
               split_point(x, y, z, 2u, {0u, 900u, 900u}),
           "on a trapezoid it is not, which is what bent the texture");
}

void test_the_tile_entry_is_read_as_the_renderer_reads_it() {
    expect(uv_entry(0x80100000u, 0x7FCu | 5u << 22) == 0x80100FF8u, "a tile's entry is eight bytes, by bits 2..11");
    expect(uv_entry(0x80100000u, 0xFFFFF003u) == 0x80100000u, "the corner indices and flags around them do not count");
}

void test_only_a_whole_square_tile_meets_at_texel_16() {
    expect(square_tile(0x39142040u, 0x008E205Fu), "a 32 by 32 tile at 64, 32");
    expect(!square_tile(0x39142040u, 0x008E204Fu), "a narrower tile keeps the game's quarters");
    expect(!square_tile(0x39142040u, 0x008E215Fu), "so does one whose second corner moved down");
    expect(square_tile(0x391420E0u, 0x008E20FFu), "a tile at the page's right edge fits");
    expect(!square_tile(0x391420E1u, 0x008E2100u), "one a texel past it does not");
    expect(square_tile(0x3914E040u, 0x008EE05Fu), "a tile at the page's bottom fits");
    expect(!square_tile(0x3914E140u, 0x008EE15Fu), "one a texel past it does not");
}

void test_the_quarters_meet_at_texel_16() {
    const uint32_t base = 0x2040u;
    const auto corner0 = quarter_uvs(Quarter::corner0, {0x39142040u, 0x008E204Fu, 0x00002F40u, 0x00002F4Fu}, base);
    const auto corner1 = quarter_uvs(Quarter::corner1, {0x39142050u, 0x008E205Fu, 0x00002F50u, 0x00002F5Fu}, base);
    const std::array<uint32_t, 4> corner2 = {0x39143050u, 0x008E305Fu, 0x00003F50u, 0x00003F5Fu};
    const auto corner3 = quarter_uvs(Quarter::corner3, {0x39143040u, 0x008E304Fu, 0x00003F40u, 0x00003F4Fu}, base);
    expect(corner0 == std::array<uint32_t, 4>{0x39142040u, 0x008E2050u, 0x00003040u, 0x00003050u},
           "the corner-0 quarter reaches texel 16 both ways, its corner and the page words kept");
    expect(corner1 == std::array<uint32_t, 4>{0x39142050u, 0x008E205Fu, 0x00003050u, 0x0000305Fu},
           "the corner-1 quarter reaches texel 16 down");
    expect(corner3 == std::array<uint32_t, 4>{0x39143040u, 0x008E3050u, 0x00003F40u, 0x00003F50u},
           "the corner-3 quarter reaches texel 16 across");
    const auto uv = [](uint32_t word) { return word & 0xFFFFu; };
    expect(uv(corner0[3]) == uv(corner1[2]) && uv(corner1[2]) == uv(corner2[0]) && uv(corner2[0]) == uv(corner3[1]),
           "all four take one texel at the middle");
    expect(uv(corner0[1]) == uv(corner1[0]) && uv(corner1[3]) == uv(corner2[1]) && uv(corner2[2]) == uv(corner3[3]) &&
               uv(corner3[0]) == uv(corner0[2]),
           "and two neighbours one texel at each edge point");
}

}  // namespace

int main() {
    test_the_middle_point_sits_on_the_shared_diagonal();
    test_the_tile_entry_is_read_as_the_renderer_reads_it();
    test_only_a_whole_square_tile_meets_at_texel_16();
    test_the_quarters_meet_at_texel_16();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "near_split: PASS\n";
    return 0;
}
