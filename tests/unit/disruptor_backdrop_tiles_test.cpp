#include "cpu_state.h"
#include "gpu_ws_screen_tile.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr std::int32_t kFrameWidth = 320;
constexpr std::int32_t kFrameCentre = kFrameWidth / 2;
constexpr std::int32_t kPeriod = 0x500;
constexpr std::int32_t kColumnWidth = 64;
constexpr std::uint32_t kLeftSeam = 0x8003B328u;
constexpr std::uint32_t kScrollSeam = 0x8003B338u;
constexpr std::uint32_t kRightSeam = 0x8003B348u;
constexpr std::uint32_t kQueuedSeam = 0x8003B5A8u;
constexpr std::uint32_t kReloadSeam = 0x8003B638u;
constexpr std::uint32_t kLeftWord = 0x8FB80040u;
constexpr std::uint32_t kScrollWord = 0x00621821u;
constexpr std::uint32_t kRightWord = 0x8FB80028u;
constexpr std::uint32_t kQueuedWord = 0x8F8205B8u;
constexpr std::uint32_t kPacket = 0x800C4000u;
constexpr std::uint32_t kCollidingPackets = 8;

struct Aspect {
    std::int32_t numerator;
    std::int32_t denominator;
};

/* 16:10, 16:9, 21:9 and 32:9 as gpu_ws_configure reduces them. */
constexpr std::array<Aspect, 4> kAspects{{{5, 6}, {3, 4}, {4, 7}, {3, 8}}};

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
auto g_marks = std::make_unique<PsxWsScreenTiles>();
Aspect g_aspect{1, 1};
bool g_widescreen = false;
std::uint32_t g_now = 0;
int g_failures = 0;

std::size_t physical(std::uint32_t address) {
    return static_cast<std::size_t>(address & 0x001FFFFFu);
}

void seed_word(std::uint32_t address, std::uint32_t value) {
    const std::size_t p = physical(address);
    for (int i = 0; i < 4; i++)
        g_ram[p + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint32_t read_word(std::uint32_t address) {
    const std::size_t p = physical(address);
    std::uint32_t value = 0;
    for (int i = 0; i < 4; i++)
        value |= static_cast<std::uint32_t>(g_ram[p + i]) << (8 * i);
    return value;
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    if (g_failures <= 20) std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" {

/* Same bodies as the gpu.c wrappers, minus the renderer's own state. */
std::int32_t gpu_ws_widen_x(std::int32_t x, int round_up) {
    if (!g_widescreen) return x;
    return psx_ws_widen_about(x, kFrameCentre, g_aspect.numerator,
                              g_aspect.denominator, round_up);
}

void gpu_ws_tag_screen_tile(CPUState *cpu, std::uint32_t primitive_addr) {
    if (!g_widescreen || !cpu || !cpu->read_word) return;
    std::uint32_t words[PSX_WS_SCREEN_TILE_WORDS];
    for (std::uint32_t i = 0; i < PSX_WS_SCREEN_TILE_WORDS; i++)
        words[i] = cpu->read_word(primitive_addr + 4u + i * 4u);
    psx_ws_screen_tile_mark(g_marks.get(), primitive_addr, words, g_now);
}

}  // extern "C"

#include "../../src/disruptor_backdrop_tiles.cpp"

namespace {

struct Tile {
    std::int32_t x;
    std::int32_t width;
    std::int32_t panorama_x;

    bool operator==(const Tile &other) const {
        return x == other.x && width == other.width &&
               panorama_x == other.panorama_x;
    }
};

struct Edges {
    std::int32_t cursor;
    std::int32_t panorama_left;  /* before the routine's own wrap */
    std::int32_t panorama_right;
};

struct Row {
    Edges edges;
    std::vector<Tile> tiles;
};

CPUState make_cpu() {
    CPUState cpu{};
    cpu.read_word = read_word;
    return cpu;
}

void run_seam(CPUState &cpu, std::uint32_t address, std::uint32_t word) {
    disruptor_backdrop_tiles_instruction_hook(&cpu, address, word, 1);
}

/* The retail instructions from 0x8003B328 to 0x8003B354 with the seams run
 * where generated code calls them. */
Edges run_edge_seams(std::int32_t left, std::int32_t right, std::uint8_t yaw,
                     std::uint32_t left_seam = kLeftSeam) {
    CPUState cpu = make_cpu();
    cpu.gpr[24] = static_cast<std::uint32_t>(left);
    run_seam(cpu, left_seam, kLeftWord);
    const std::uint32_t inverted = static_cast<std::uint8_t>(~yaw);
    cpu.gpr[3] = (inverted << 2) + inverted;
    run_seam(cpu, kScrollSeam, kScrollWord);
    Edges edges{};
    edges.cursor = static_cast<std::int32_t>(cpu.gpr[24]);
    cpu.gpr[24] += cpu.gpr[3];
    edges.panorama_left = static_cast<std::int32_t>(cpu.gpr[24]);
    cpu.gpr[24] = static_cast<std::uint32_t>(right);
    run_seam(cpu, kRightSeam, kRightWord);
    edges.panorama_right = static_cast<std::int32_t>(cpu.gpr[24] + cpu.gpr[3]);
    return edges;
}

/* Model of the routine's tile row, with the 64-texel columns the first
 * mission's panorama uses. */
Row lay_row(std::int32_t left, std::int32_t right, std::uint8_t yaw,
            std::uint32_t left_seam = kLeftSeam) {
    Row row{run_edge_seams(left, right, yaw, left_seam), {}};
    std::int32_t cursor = row.edges.cursor;
    std::int32_t panorama_right = row.edges.panorama_right;
    std::int32_t panorama_x = row.edges.panorama_left;
    if (panorama_x < 0) return row;
    if (panorama_x >= kPeriod) {
        panorama_x -= kPeriod;
        panorama_right -= kPeriod;
    }
    std::int32_t column = panorama_x / kColumnWidth;
    while (row.tiles.size() < 64) {
        const std::int32_t column_end = (column + 1) * kColumnWidth;
        const bool last = !(column_end < panorama_right);
        const std::int32_t width =
            (last ? panorama_right : column_end) - panorama_x;
        row.tiles.push_back({cursor, width, panorama_x});
        if (last) break;
        cursor += width;
        panorama_x = column_end;
        if (++column == kPeriod / kColumnWidth) {
            column = 0;
            panorama_x -= kPeriod;
            panorama_right -= kPeriod;
        }
    }
    return row;
}

std::int32_t squashed(std::int32_t x) {
    return psx_ws_squash_about(x, kFrameCentre, g_aspect.numerator,
                               g_aspect.denominator);
}

/* What the seams hand the routine, for every integer edge. The panorama X
 * depends on the left edge and the heading, the span on the two edges. */
void test_seams_for_every_edge_and_heading() {
    g_widescreen = true;
    for (const Aspect &aspect : kAspects) {
        g_aspect = aspect;
        for (int yaw = 0; yaw < 256; yaw++) {
            for (std::int32_t left = 0; left < kFrameWidth; left++) {
                const Edges edges = run_edge_seams(
                    left, kFrameWidth, static_cast<std::uint8_t>(yaw));
                expect(edges.panorama_left >= 0 &&
                           edges.panorama_left < 2 * kPeriod,
                       "the column search must get a panorama X inside two periods");
            }
        }
        for (std::int32_t left = 0; left < kFrameWidth; left++) {
            for (std::int32_t right = left + 1; right <= kFrameWidth; right++) {
                const Edges edges = run_edge_seams(left, right, 0);
                const std::int32_t span =
                    edges.panorama_right - edges.panorama_left;
                expect(span > 0 && span <= kPeriod,
                       "a widened opening must fit one panorama period");
                const std::int32_t first = squashed(edges.cursor);
                const std::int32_t last = squashed(edges.cursor + span);
                expect(first <= left && first >= left - 1 && last >= right &&
                           last <= right + 1,
                       "the squashed span must cover the opening within a pixel");
            }
        }
    }
}

void check_row(std::int32_t left, std::int32_t right, std::uint8_t yaw) {
    const Row row = lay_row(left, right, yaw);
    expect(!row.tiles.empty() && row.tiles.size() < 64,
           "the tile row must terminate");
    if (row.tiles.empty()) return;
    for (std::size_t i = 0; i < row.tiles.size(); i++) {
        expect(row.tiles[i].width > 0, "every tile must have a width");
        if (i == 0) continue;
        const Tile &previous = row.tiles[i - 1];
        expect(squashed(previous.x + previous.width) == squashed(row.tiles[i].x),
               "squashed neighbours must share an edge");
    }
    const Tile &last = row.tiles.back();
    expect(row.tiles.front().x == row.edges.cursor &&
               last.x + last.width - row.edges.cursor ==
                   row.edges.panorama_right - row.edges.panorama_left,
           "the row must span exactly the widened opening");
    const Row reloaded = lay_row(left, right, yaw, kReloadSeam);
    expect(reloaded.tiles == row.tiles,
           "later rows must start from the same widened left edge");
}

void test_row_model_for_every_heading() {
    g_widescreen = true;
    for (const Aspect &aspect : kAspects) {
        g_aspect = aspect;
        for (int yaw = 0; yaw < 256; yaw++) {
            check_row(0, kFrameWidth, static_cast<std::uint8_t>(yaw));
            for (std::int32_t left = 0; left < kFrameWidth; left += 7) {
                for (std::int32_t right = left + 1; right <= kFrameWidth;
                     right += 11) {
                    check_row(left, right, static_cast<std::uint8_t>(yaw));
                }
            }
        }
    }
}

void test_measured_row_at_16_9() {
    g_widescreen = true;
    g_aspect = {3, 4};
    const std::vector<Tile> measured{{-54, 59, 1221}, {5, 64, 0},  {69, 64, 64},
                                     {133, 64, 128},  {197, 64, 192},
                                     {261, 64, 256},  {325, 49, 320}};
    expect(lay_row(0, kFrameWidth, 0).tiles == measured,
           "the model must reproduce the row captured from the running game");
}

void test_native_aspect_is_untouched() {
    g_widescreen = false;
    for (int yaw = 0; yaw < 256; yaw++) {
        for (std::int32_t left = 0; left < kFrameWidth; left += 13) {
            const Row row =
                lay_row(left, kFrameWidth, static_cast<std::uint8_t>(yaw));
            const std::int32_t scroll = static_cast<std::uint8_t>(~yaw) * 5;
            expect(row.edges.panorama_left == left + scroll,
                   "4:3 must keep the retail panorama origin");
            expect(!row.tiles.empty() && row.tiles.front().x == left,
                   "4:3 must keep the retail left edge");
            if (row.tiles.empty()) continue;
            const Tile &last = row.tiles.back();
            expect(last.x + last.width == kFrameWidth,
                   "4:3 must keep the retail right edge");
        }
    }
}

void test_seams_are_guarded_by_their_instruction_words() {
    g_widescreen = true;
    g_aspect = {3, 4};
    CPUState cpu = make_cpu();
    cpu.gpr[3] = 0;
    cpu.gpr[24] = 0;
    run_seam(cpu, kLeftSeam, kRightWord);
    run_seam(cpu, kRightSeam, kLeftWord);
    run_seam(cpu, kReloadSeam, kScrollWord);
    run_seam(cpu, 0x8003B32Cu, kLeftWord);
    disruptor_backdrop_tiles_instruction_hook(&cpu, kLeftSeam,
                                              kLeftWord, 0);
    expect(cpu.gpr[24] == 0u, "a seam must ignore a foreign word or phase");
    cpu.gpr[24] = static_cast<std::uint32_t>(-54);
    run_seam(cpu, kScrollSeam, kLeftWord);
    expect(cpu.gpr[3] == 0u, "the scroll seam must ignore a foreign word");
    run_seam(cpu, kScrollSeam, kScrollWord);
    expect(cpu.gpr[3] == 0x500u, "a negative panorama X must gain one period");
    for (int reg = 1; reg < 32; reg++) {
        if (reg == 3 || reg == 24) continue;
        expect(cpu.gpr[reg] == 0u, "the edge seams must touch only $v1 and $t8");
    }
}

std::array<std::uint32_t, 4> tile_words(std::uint32_t x) {
    return {0x64808080u, 0x00100000u | x, 0x7FC02000u, 0x00400040u};
}

void seed_packet(std::uint32_t address, const std::array<std::uint32_t, 4> &words) {
    for (std::uint32_t i = 0; i < 4; i++)
        seed_word(address + 4u + i * 4u, words[i]);
}

int take(std::uint32_t address, const std::array<std::uint32_t, 4> &words) {
    return psx_ws_screen_tile_take(g_marks.get(), address, words.data(), g_now);
}

void queue_tile(std::uint32_t address, const std::array<std::uint32_t, 4> &words) {
    CPUState cpu = make_cpu();
    seed_packet(address, words);
    cpu.gpr[17] = address;
    run_seam(cpu, kQueuedSeam, kQueuedWord);
}

void test_mark_buys_one_draw_of_the_same_packet() {
    g_widescreen = true;
    g_now = 100;
    *g_marks = PsxWsScreenTiles{};
    const auto tile = tile_words(5);
    queue_tile(kPacket, tile);
    expect(take(kPacket + 0x20u, tile) == 0, "another address must not match");
    expect(take(kPacket, tile) == 1, "the marked packet must be recognised");
    expect(take(kPacket, tile) == 0, "a mark must buy exactly one draw");

    queue_tile(kPacket, tile);
    expect(take(kPacket, tile_words(6)) == 0,
           "a packet rebuilt at the same address must not match");
    expect(take(kPacket, tile) == 0, "a mismatch must retire the stale mark");

    queue_tile(kPacket, tile);
    auto textured_blend = tile;
    textured_blend[0] = 0x66808080u;
    expect(take(kPacket, textured_blend) == 0, "only opaque SPRT may match");
    expect(take(kPacket, tile) == 0, "a rectangle of another kind must retire the mark");

    for (std::size_t word = 0; word < tile.size(); word++) {
        queue_tile(kPacket, tile);
        auto altered = tile;
        altered[word] ^= 0x00010000u;
        expect(take(kPacket, altered) == 0 && take(kPacket, tile) == 0,
               "a change in any packet word must reject the draw and retire the mark");
    }

    CPUState cpu = make_cpu();
    seed_packet(kPacket, tile);
    cpu.gpr[17] = kPacket;
    run_seam(cpu, kQueuedSeam, kLeftWord);
    disruptor_backdrop_tiles_instruction_hook(&cpu, kQueuedSeam,
                                              kQueuedWord, 0);
    expect(take(kPacket, tile) == 0, "the mark seam must be word and phase guarded");

    queue_tile(kPacket, tile);
    queue_tile(kPacket, tile_words(6));
    expect(take(kPacket, tile_words(6)) == 1 && take(kPacket, tile) == 0,
           "a packet rebuilt before its draw must replace its own mark");

    seed_packet(kPacket, textured_blend);
    run_seam(cpu, kQueuedSeam, kQueuedWord);
    expect(psx_ws_screen_tile_slot(g_marks.get(), kPacket, 0u)->key == 0u,
           "only an SPRT packet may be marked");
}

void test_marks_expire_and_survive_collisions() {
    g_widescreen = true;
    *g_marks = PsxWsScreenTiles{};
    const auto tile = tile_words(5);
    g_now = 100;
    queue_tile(kPacket, tile);
    g_now = 100 + PSX_WS_SCREEN_TILE_MAX_AGE;
    expect(take(kPacket, tile) == 1, "a mark must live for its whole age bound");
    g_now = 100;
    queue_tile(kPacket, tile);
    g_now = 101 + PSX_WS_SCREEN_TILE_MAX_AGE;
    expect(take(kPacket, tile) == 0, "an old mark must not match");

    *g_marks = PsxWsScreenTiles{};
    g_now = 200;
    constexpr std::uint32_t kStride = 0x1000u;
    for (std::uint32_t i = 0; i < kCollidingPackets; i++)
        queue_tile(kPacket + i * kStride, tile_words(i));
    queue_tile(kPacket + 0x20u, tile_words(99));
    for (std::uint32_t i = 0; i < kCollidingPackets; i++) {
        expect(take(kPacket + i * kStride, tile_words(i)) == 1,
               "packets sharing a home slot must all keep their marks");
    }
    expect(take(kPacket + 0x20u, tile_words(99)) == 1,
           "a neighbouring packet must keep its mark beside a full probe run");

    *g_marks = PsxWsScreenTiles{};
    g_now = 300;
    queue_tile(kPacket + 4u, tile_words(99));
    g_now = 301 + PSX_WS_SCREEN_TILE_MAX_AGE;
    for (std::uint32_t i = 0; i < kCollidingPackets; i++)
        queue_tile(kPacket + i * kStride, tile_words(i));
    for (std::uint32_t i = 0; i < kCollidingPackets; i++) {
        expect(take(kPacket + i * kStride, tile_words(i)) == 1,
               "an expired mark must give its slot to a live one");
    }
}

void test_widening_rounds_outward() {
    for (const Aspect &aspect : kAspects) {
        for (std::int32_t x = -64; x <= kFrameWidth + 64; x++) {
            const std::int32_t low = psx_ws_widen_about(
                x, kFrameCentre, aspect.numerator, aspect.denominator, 0);
            const std::int32_t high = psx_ws_widen_about(
                x, kFrameCentre, aspect.numerator, aspect.denominator, 1);
            expect(high - low == 0 || high - low == 1,
                   "the two roundings must differ by at most one");
            expect(psx_ws_squash_about(low, kFrameCentre, aspect.numerator,
                                       aspect.denominator) <= x &&
                       psx_ws_squash_about(high, kFrameCentre, aspect.numerator,
                                           aspect.denominator) >= x,
                   "squashing a widened edge must not land inside it");
        }
    }
    expect(psx_ws_widen_about(0, 160, 3, 4, 0) == -54 &&
               psx_ws_widen_about(320, 160, 3, 4, 1) == 374 &&
               psx_ws_widen_about(0, 160, 3, 8, 0) == -267 &&
               psx_ws_widen_about(320, 160, 3, 8, 1) == 587,
           "the widened frame must match the spans measured in the game");
    expect(psx_ws_squash_about(-54, 160, 3, 4) == -1 &&
               psx_ws_squash_about(374, 160, 3, 4) == 321 &&
               psx_ws_squash_about(162, 160, 3, 4) == 162 &&
               psx_ws_squash_about(158, 160, 3, 4) == 158,
           "the squash must round to nearest like the renderer's other paths");
}

}  // namespace

int main() {
    test_widening_rounds_outward();
    test_measured_row_at_16_9();
    test_seams_for_every_edge_and_heading();
    test_row_model_for_every_heading();
    test_native_aspect_is_untouched();
    test_seams_are_guarded_by_their_instruction_words();
    test_mark_buys_one_draw_of_the_same_packet();
    test_marks_expire_and_survive_collisions();
    if (g_failures != 0) {
        std::cerr << g_failures << " backdrop tile check(s) failed\n";
        return 1;
    }
    std::cout << "Disruptor backdrop tiles: PASS\n";
    return 0;
}
