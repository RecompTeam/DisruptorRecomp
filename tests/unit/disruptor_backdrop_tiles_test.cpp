#include "cpu_state.h"
#include "gpu_ws_screen_tile.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr std::int32_t kFrameWidth = 320;
constexpr std::int32_t kFrameCentre = kFrameWidth / 2;
constexpr std::int32_t kPeriod = 0x500;
constexpr std::int32_t kColumnWidth = 64;
constexpr std::int32_t kMargin = 4;
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
std::int32_t g_told_scroll = 0, g_told_period = 0, g_told_column = 0, g_told_left = 0, g_told_right = 0;
std::int32_t g_own_told_left = 0, g_own_told_right = 0;
int g_scrolls = 0, g_spans = 0;
std::vector<std::int32_t> g_fog_steps;
std::vector<std::uint32_t> g_fog_tiles;
std::uint32_t g_taken_pins = 0;
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
    psx_ws_screen_tile_mark(g_marks.get(), primitive_addr, words, g_now,
                            g_spans ? psx_ws_screen_tile_pins(static_cast<std::int16_t>(words[1] & 0xFFFFu),
                                                              static_cast<std::int16_t>(words[3] & 0xFFFFu), g_told_left, g_told_right)
                                    : 3u);
}

void gpu_ws_backdrop_scroll(std::int32_t scroll, std::int32_t period, std::int32_t column) {
    g_told_scroll = scroll;
    g_told_period = period;
    g_told_column = column;
    ++g_scrolls;
}

void gpu_sky_fog_opening(std::int32_t step) { g_fog_steps.push_back(step); }
struct PanoramaTold {
    int rows, columns, tall, period, focal;
    std::int32_t first_x, first_y, last_x;
};
std::vector<PanoramaTold> g_panoramas;
void gpu_sky_panorama(int rows, int columns, int tall, int period, int focal, const std::int32_t *cells, int irregular) {
    (void)irregular;
    g_panoramas.push_back({rows, columns, tall, period, focal, rows ? cells[0] : -1, rows ? cells[1] : -1,
                           rows ? cells[2 * (rows * columns - 1)] : -1});
}

void gpu_sky_fog_tile(CPUState *, std::uint32_t primitive_addr) { g_fog_tiles.push_back(primitive_addr); }

void gpu_sky_fog_span(std::int32_t left, std::int32_t right) {
    g_own_told_left = left;
    g_own_told_right = right;
}

void gpu_ws_backdrop_span(std::int32_t left, std::int32_t right) {
    g_told_left = left;
    g_told_right = right;
    ++g_spans;
}

}  // extern "C"

#include "game/render/sky_grower.h"

namespace disruptor::sky_grower {
void look(const sky_panorama::Cells &, uint32_t, uint32_t, bool, uint32_t) {}
}  // namespace disruptor::sky_grower

#include "../../src/game/render/backdrop_tiles.cpp"

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

/* Where the routine's sky starts and stops: an edge inside the frame moves out by the margin, up to the frame's edge. */
std::int32_t reach(std::int32_t edge, bool right_edge) {
    if (edge == 0 || edge == kFrameWidth) return edge;
    return right_edge ? std::min(edge + kMargin, kFrameWidth) : std::max(edge - kMargin, 0);
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
                expect(first <= reach(left, false) && first >= reach(left, false) - 1 && last >= reach(right, true) &&
                           last <= reach(right, true) + 1,
                       "the squashed span must cover the opening and its margin within a pixel");
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
            expect(row.edges.panorama_left == reach(left, false) + scroll,
                   "4:3 must keep the retail panorama origin of the opened edge");
            expect(!row.tiles.empty() && row.tiles.front().x == reach(left, false),
                   "4:3 must start at the opened left edge");
            if (row.tiles.empty()) continue;
            const Tile &last = row.tiles.back();
            expect(last.x + last.width == kFrameWidth,
                   "4:3 must keep the retail right edge");
        }
    }
}

void test_openings_reach_past_their_edges() {
    g_widescreen = false;
    const std::array<std::array<std::int32_t, 3>, 10> cases{{{0, 0, 0},
                                                           {1, 0, 0},
                                                           {4, 0, 0},
                                                           {5, 1, 0},
                                                           {69, 65, 0},
                                                           {137, 141, 1},
                                                           {315, 319, 1},
                                                           {316, 320, 1},
                                                           {319, 320, 1},
                                                           {320, 320, 1}}};
    for (const auto &[edge, expected, right_edge] : cases) {
        CPUState cpu = make_cpu();
        cpu.gpr[24] = static_cast<std::uint32_t>(edge);
        run_seam(cpu, right_edge ? kRightSeam : kLeftSeam, right_edge ? kRightWord : kLeftWord);
        expect(static_cast<std::int32_t>(cpu.gpr[24]) == expected,
               "an edge inside the frame moves out by four pixels up to the frame's edge, the frame's edges stay");
        if (!right_edge) {
            cpu.gpr[24] = static_cast<std::uint32_t>(edge);
            run_seam(cpu, kReloadSeam, kLeftWord);
            expect(static_cast<std::int32_t>(cpu.gpr[24]) == expected, "the reloaded left edge moves the same");
        }
    }
}

void test_only_clear_openings_reach_past_their_edges() {
    g_widescreen = false;
    seed_word(80u, 3u);
    CPUState cpu = make_cpu();
    cpu.gpr[24] = 69u;
    run_seam(cpu, kLeftSeam, kLeftWord);
    const std::int32_t left = static_cast<std::int32_t>(cpu.gpr[24]);
    cpu.gpr[24] = 69u;
    run_seam(cpu, kReloadSeam, kLeftWord);
    const std::int32_t reloaded = static_cast<std::int32_t>(cpu.gpr[24]);
    cpu.gpr[24] = 137u;
    run_seam(cpu, kRightSeam, kRightWord);
    expect(left == 69 && reloaded == 69 && static_cast<std::int32_t>(cpu.gpr[24]) == 137,
           "a fogged opening keeps the edges the game measured");
    seed_word(80u, 0u);
    cpu.gpr[24] = 69u;
    run_seam(cpu, kLeftSeam, kLeftWord);
    expect(static_cast<std::int32_t>(cpu.gpr[24]) == 65, "and the next clear one reaches past them again");
}

void test_the_fog_hears_the_measured_span() {
    for (const bool wide : {false, true}) {
        g_widescreen = wide;
        g_aspect = {3, 4};
        run_edge_seams(69, 137, 0);
        expect(g_own_told_left == 69 && g_own_told_right == 137 && g_told_left == gpu_ws_widen_x(65, 0) &&
                   g_told_right == gpu_ws_widen_x(141, 1),
               "the fog is told the span the game measured in its own columns, the tiles reach the margin past it");
        run_edge_seams(69, 137, 0, kReloadSeam);
        expect(g_own_told_left == 69, "a later row's reload leaves the measured left edge alone");
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
    return psx_ws_screen_tile_take(g_marks.get(), address, words.data(), g_now, &g_taken_pins);
}

void queue_tile(std::uint32_t address, const std::array<std::uint32_t, 4> &words) {
    CPUState cpu = make_cpu();
    seed_packet(address, words);
    cpu.gpr[17] = address;
    run_seam(cpu, kQueuedSeam, kQueuedWord);
}

void test_a_tile_tells_its_panorama_column() {
    expect(psx_ws_screen_tile_sky_column(0, 0, kPeriod, kColumnWidth) == 1.0f &&
               psx_ws_screen_tile_sky_column(64, 0, kPeriod, kColumnWidth) == 2.0f,
           "a tile at the frame's left edge with no scroll is column 0, the next one column 1, each plus one");
    expect(psx_ws_screen_tile_sky_column(100, 30, kPeriod, kColumnWidth) == 3.0f,
           "a tile laid mid-column is the column its frame x and the scroll fall in");
    expect(psx_ws_screen_tile_sky_column(10, kPeriod - 5, kPeriod, kColumnWidth) == 1.0f &&
               psx_ws_screen_tile_sky_column(-5, 0, kPeriod, kColumnWidth) == 20.0f,
           "the panorama wraps round its turn either way, a widened opening's tile left of the frame included");
    expect(psx_ws_screen_tile_sky_column(0, 0, 0, kColumnWidth) == 0.0f && psx_ws_screen_tile_sky_column(0, 0, kPeriod, 0) == 0.0f,
           "with no panorama the column is not known");
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

void test_a_pinned_end_squashed_to_nothing_keeps_a_pixel() {
    std::int32_t left = psx_ws_squash_about(190, 160, 3, 4), right = psx_ws_squash_about(191, 160, 3, 4);
    expect(left == 183 && right == 183, "the clear opening's first tile, 190..191 beside a fogged one, squashes to no width at 16:9");
    expect(psx_ws_screen_tile_keep(&left, &right, psx_ws_screen_tile_pins(190, 1, 190, 374)) && left == 183 && right == 184,
           "holding its span's left end it keeps a pixel there, under the moved edge of the tile beside it");
    expect(psx_ws_screen_tile_edge(191, 160, 3, 4) - right >= -PSX_WS_SCREEN_TILE_OFFSET_MOST,
           "its right end, placed off that pixel, stays within what the renderer accepts");
    left = right = 183;
    expect(psx_ws_screen_tile_keep(&left, &right, 2u) && left == 182 && right == 183, "a pinned right end keeps the pixel before it");
    left = right = 183;
    expect(!psx_ws_screen_tile_keep(&left, &right, 0u), "a tile of no width with no pinned end is not drawn");
    left = right = 183;
    expect(!psx_ws_screen_tile_keep(&left, &right, 3u) && left == 183 && right == 183,
           "nor one with both ends pinned: a span of no width bares nothing, and a kept pixel would reach past it");
    int kept = 0, within = 1;
    for (const Aspect &aspect : kAspects) {
        for (std::int32_t x = -300; x <= 620; x++) {
            for (std::uint32_t pins = 1u; pins <= 2u; pins++) {
                std::int32_t l = psx_ws_squash_about(x, kFrameCentre, aspect.numerator, aspect.denominator);
                std::int32_t r = psx_ws_squash_about(x + 1, kFrameCentre, aspect.numerator, aspect.denominator);
                if (r > l) continue;
                within &= psx_ws_screen_tile_keep(&l, &r, pins);
                ++kept;
                const double off = pins == 1u
                                       ? psx_ws_screen_tile_edge(x + 1, kFrameCentre, aspect.numerator, aspect.denominator) - r
                                       : psx_ws_screen_tile_edge(x, kFrameCentre, aspect.numerator, aspect.denominator) - l;
                within &= r - l == 1 && off >= -PSX_WS_SCREEN_TILE_OFFSET_MOST && off <= PSX_WS_SCREEN_TILE_OFFSET_MOST;
            }
        }
    }
    expect(kept > 0 && within,
           "at every aspect a kept one-pixel tile's moved end stays within what the renderer places, 32:9 included");
    left = right = psx_ws_squash_about(155, 160, 3, 8);
    expect(psx_ws_screen_tile_keep(&left, &right, 2u) && left == 157 && right == 158 &&
               psx_ws_screen_tile_edge(155, 160, 3, 8) - left == 1.125,
           "at 32:9 a pinned right end's tie reaches 1.125 off the kept pixel, inside the bound");
    left = 100;
    right = 148;
    expect(psx_ws_screen_tile_keep(&left, &right, 3u) && left == 100 && right == 148, "a tile with width is drawn as it is");
}

void test_the_panorama_is_placed_between_pixels() {
    expect(psx_ws_screen_tile_pins(-54, 48, -54, 374) == 1u && psx_ws_screen_tile_pins(326, 48, -54, 374) == 2u &&
               psx_ws_screen_tile_pins(-6, 48, -54, 374) == 0u && psx_ws_screen_tile_pins(-54, 428, -54, 374) == 3u &&
               psx_ws_screen_tile_pins(-53, 48, -54, 374) == 0u && psx_ws_screen_tile_pins(325, 48, -54, 374) == 0u,
           "a tile's edge is pinned where it is an end of the span the game fills, and nowhere else");
    expect(psx_ws_screen_tile_edge(162, 160, 3, 4) == 161.5 && psx_ws_screen_tile_edge(-54, 160, 3, 4) == -0.5 &&
               psx_ws_screen_tile_edge(100, 160, 1, 1) == 100.0 && psx_ws_screen_tile_edge(374, 160, 3, 4) == 320.5,
           "an edge stands where the squash puts it, not rounded");
    expect(psx_ws_screen_tile_room(27.0f, 64.0f, 0) == 27.0f && psx_ws_screen_tile_room(64.0f, 64.0f, 0) == 0.0f &&
               psx_ws_screen_tile_room(130.5f, 64.0f, 0) == 2.5f && psx_ws_screen_tile_room(33.0f, 64.0f, 1) == 31.0f &&
               psx_ws_screen_tile_room(64.0f, 64.0f, 1) == 0.0f && psx_ws_screen_tile_room(128.25f, 64.0f, 1) == 63.75f &&
               psx_ws_screen_tile_room(27.0f, 0.0f, 0) == 0.0f,
           "an end has the texels of its own column beyond it, to the left of a left end and to the right of a right one");
    expect(psx_ws_screen_tile_slide(0.0f, 1.5f, 0.75f, 0, 27.0f, 40.0f) == -2.0f &&
               psx_ws_screen_tile_slide(0.0f, -1.5f, 0.75f, 0, 27.0f, 40.0f) == 2.0f &&
               psx_ws_screen_tile_slide(0.0f, 1.5f, 0.75f, 1, 31.0f, 40.0f) == -2.0f &&
               psx_ws_screen_tile_slide(0.375f, 0.375f, 0.75f, 1, 31.0f, 40.0f) == -1.0f,
           "an end that stays shows the texel that lies there: the panorama's move and the end's own rounding, in texels, backwards");
    expect(psx_ws_screen_tile_slide(0.0f, 30.0f, 0.75f, 0, 27.0f, 40.0f) == -27.0f &&
               psx_ws_screen_tile_slide(0.0f, -60.0f, 0.75f, 0, 27.0f, 40.0f) == 40.0f &&
               psx_ws_screen_tile_slide(0.0f, -30.0f, 0.75f, 1, 31.0f, 40.0f) == 31.0f &&
               psx_ws_screen_tile_slide(0.0f, 60.0f, 0.75f, 1, 31.0f, 40.0f) == -40.0f &&
               psx_ws_screen_tile_slide(0.0f, 30.0f, 0.75f, 0, 0.0f, 40.0f) == 0.0f,
           "no farther out than its column has texels, and no farther in than the tile is wide");
    expect(psx_ws_screen_tile_slide(0.0f, 1.5f, 0.0f, 0, 27.0f, 40.0f) == 0.0f &&
               psx_ws_screen_tile_slide(0.0f, NAN, 0.75f, 0, 27.0f, 40.0f) == 0.0f,
           "a tile with no scale, or a move that is no number, slides nothing");
    expect(psx_ws_screen_tile_want(0.375f, 0.375f, 0.75f) == -1.0f && psx_ws_screen_tile_want(0.0f, -1.5f, 0.75f) == 2.0f &&
               psx_ws_screen_tile_want(0.0f, 1.5f, 0.0f) == 0.0f && psx_ws_screen_tile_want(0.0f, NAN, 0.75f) == 0.0f,
           "what an end wants is that slide before any bound");
    const float wanted = psx_ws_screen_tile_want(0.0f, 2.5f, 1.0f);
    expect(psx_ws_screen_tile_slide(0.0f, 2.5f, 1.0f, 0, 1.0f, 40.0f) == -1.0f &&
               psx_ws_screen_tile_slide(0.0f, 0.0f - wanted, 1.0f, 0, 1.0f, 40.0f) == -1.0f &&
               psx_ws_screen_tile_slide(0.0f, -2.5f - wanted, 1.0f, 0, 1.0f, 40.0f) == 0.0f &&
               psx_ws_screen_tile_slide(0.0f, -2.0f - wanted, 1.0f, 0, 1.0f, 40.0f) == -0.5f &&
               psx_ws_screen_tile_slide(0.0f, -1.0f - wanted, 1.0f, 0, 1.0f, 40.0f) == -1.0f,
           "a left end a texel into its column that wants 2.5 out gets one, and the presenter's way back is bounded together with that want: none of it gives the mirror's slide, all of it the game's own corner");
    expect(psx_ws_backdrop_back(10.0f, 7.0f, 960.0f, 12.0f) == 3.0f && psx_ws_backdrop_back(7.0f, 10.0f, 960.0f, 12.0f) == -3.0f,
           "the panorama stood as far back as its origin was off a frame earlier");
    expect(psx_ws_backdrop_back(2.0f, 958.0f, 960.0f, 12.0f) == 4.0f && psx_ws_backdrop_back(958.0f, 2.0f, 960.0f, 12.0f) == -4.0f,
           "by the short way round its period");
    expect(psx_ws_backdrop_back(40.0f, 0.0f, 960.0f, 12.0f) == 12.0f && psx_ws_backdrop_back(-40.0f, 0.0f, 960.0f, 12.0f) == -12.0f &&
               psx_ws_backdrop_back(12.0f, 0.0f, 960.0f, 12.0f) == 12.0f,
           "and no farther than the bound it is given");
    expect(psx_ws_backdrop_back(10.0f, 7.0f, 0.0f, 12.0f) == 0.0f && psx_ws_backdrop_back(NAN, 7.0f, 960.0f, 12.0f) == 0.0f &&
               psx_ws_backdrop_back(10.0f, INFINITY, 960.0f, 12.0f) == 0.0f,
           "a panorama with no period, or an origin that is no number, stood nowhere else");

    g_widescreen = true;
    g_aspect = {3, 4};
    g_scrolls = g_spans = 0;
    CPUState cpu = make_cpu();
    cpu.gpr[24] = 0;
    run_seam(cpu, kLeftSeam, kLeftWord);
    cpu.gpr[3] = 10u;
    run_seam(cpu, kScrollSeam, kScrollWord);
    expect(g_scrolls == 1 && g_told_scroll == 10 + 0x500 && g_told_period == 0x500 && g_told_column == kColumnWidth && g_spans == 0,
           "the scroll seam tells the panorama's scroll as the routine goes on with it, its period and its column width");
    cpu.gpr[24] = 320;
    run_seam(cpu, kRightSeam, kRightWord);
    expect(g_spans == 1 && g_told_left == -54 && g_told_right == 374, "the right edge's seam tells the widened span");
    run_seam(cpu, kScrollSeam, kLeftWord);
    run_seam(cpu, kRightSeam, kLeftWord);
    expect(g_scrolls == 1 && g_spans == 1, "a foreign word tells nothing");

    const auto left_end = std::array<std::uint32_t, 4>{0x64808080u, 0x0010FFCAu, 0x7FC02000u, 0x00400030u};
    queue_tile(kPacket, left_end);
    expect(take(kPacket, left_end) == 1 && g_taken_pins == 1u, "a tile at the span's left end carries that pin to its draw");
    const auto inner = std::array<std::uint32_t, 4>{0x64808080u, 0x00100010u, 0x7FC02000u, 0x00400030u};
    queue_tile(kPacket, inner);
    expect(take(kPacket, inner) == 1 && g_taken_pins == 0u, "an inner tile carries none");
    expect(take(kPacket, inner) == 0 && g_taken_pins == 0u, "and a draw that matches no mark takes none");
    queue_tile(kPacket, left_end);
    g_taken_pins = 7u;
    expect(take(kPacket, inner) == 0 && g_taken_pins == 0u, "nor does a draw of other words at a marked address");
    g_spans = 0;
    queue_tile(kPacket, inner);
    expect(take(kPacket, inner) == 1 && g_taken_pins == 3u, "with no span told, both edges stay where they are");
    g_widescreen = false;
}

}  // namespace

void test_sky_behind_the_world() {
    constexpr std::uint32_t kVBlanks = 0x8005B920u;
    CPUState cpu = make_cpu();
    seed_word(kVBlanks, 1000u);
    expect(disruptor_backdrop_tiles_sky_behind(read_word(kVBlanks)) == 0, "no sky is behind the world before a tile");
    run_seam(cpu, kQueuedSeam, kQueuedWord + 1u);
    expect(disruptor_backdrop_tiles_sky_behind(read_word(kVBlanks)) == 0, "a seam with another word says nothing of the sky");
    seed_packet(kPacket, tile_words(1));
    cpu.gpr[17] = kPacket;
    run_seam(cpu, kQueuedSeam, kQueuedWord);
    expect(disruptor_backdrop_tiles_sky_behind(read_word(kVBlanks)) == 1, "a sky tile puts the sky behind the world");
    seed_word(kVBlanks, 1300u);
    expect(disruptor_backdrop_tiles_sky_behind(read_word(kVBlanks)) == 1, "for five seconds after it");
    seed_word(kVBlanks, 1301u);
    expect(disruptor_backdrop_tiles_sky_behind(read_word(kVBlanks)) == 0, "and no longer");
    *g_marks = PsxWsScreenTiles{};
}

/* The routine keeps an opening's fog step at 80($sp): its first row tells it, a row after does not, and so does a tile. */
void test_each_opening_tells_its_fog_step() {
    constexpr std::uint32_t kStack = 0x801FFE00u;
    g_fog_steps.clear();
    g_fog_tiles.clear();
    CPUState cpu = make_cpu();
    cpu.gpr[29] = kStack;
    seed_word(kStack + 80u, 7u);
    run_seam(cpu, kLeftSeam, kLeftWord);
    run_seam(cpu, kReloadSeam, kLeftWord);
    seed_word(kStack + 80u, 0u);
    run_seam(cpu, kLeftSeam, kLeftWord);
    run_seam(cpu, kLeftSeam, kLeftWord ^ 1u);
    expect(g_fog_steps == std::vector<std::int32_t>{7, 0}, "each opening's first row tells its fog step, read from the stack");
    cpu.gpr[17] = kPacket;
    run_seam(cpu, kQueuedSeam, kQueuedWord);
    run_seam(cpu, kQueuedSeam, kQueuedWord ^ 1u);
    expect(g_fog_tiles == std::vector<std::uint32_t>{kPacket}, "a queued tile is told with its packet");
    CPUState blind{};
    blind.gpr[29] = kStack;
    run_seam(blind, kLeftSeam, kLeftWord);
    expect(g_fog_steps.size() == 2u, "without a way to read memory no step is told");
}

void test_the_level_panorama_is_told_when_it_changes() {
    constexpr std::uint32_t kStack = 0x801FFE00u, kTable = 0x800AF8C8u;
    g_panoramas.clear();
    CPUState cpu = make_cpu();
    cpu.gpr[29] = kStack;
    seed_word(0x80071704u, kTable);
    seed_word(kTable, 4u);
    seed_word(kTable + 4u, 20u);
    for (std::uint32_t row = 0; row <= 4u; ++row) seed_word(kTable + 8u + 4u * row, 60u * row);
    for (std::uint32_t column = 0; column <= 20u; ++column) seed_word(kTable + 32u + 4u * column, 64u * column);
    for (std::uint32_t row = 0; row < 4u; ++row)
        for (std::uint32_t column = 0; column < 20u; ++column) {
            seed_word(kTable + 128u + 96u * row + 4u * column, 320u + 32u * (column % 5u));
            seed_word(kTable + 704u + 96u * row + 4u * column, 256u + 60u * row);
        }
    seed_word(kVBlanks, 100u);
    run_seam(cpu, kLeftSeam, kLeftWord);
    run_seam(cpu, kLeftSeam, kLeftWord);
    expect(g_panoramas.size() == 1u && g_panoramas[0].rows == 4 && g_panoramas[0].columns == 20 && g_panoramas[0].tall == 60 &&
               g_panoramas[0].period == 1280 && g_panoramas[0].focal == 160 && g_panoramas[0].first_x == 320 &&
               g_panoramas[0].first_y == 256 && g_panoramas[0].last_x == 448,
           "the level's table is told with its cells' rows, its turn and the world's focal, once a frame");
    seed_word(kVBlanks, 101u);
    run_seam(cpu, kLeftSeam, kLeftWord);
    expect(g_panoramas.size() == 1u, "the same table is not told again");
    for (std::uint32_t column = 0; column < 20u; column += 5u) seed_word(kTable + 128u + 4u * column, 480u);
    seed_word(kVBlanks, 102u);
    run_seam(cpu, kLeftSeam, kLeftWord);
    expect(g_panoramas.size() == 2u && g_panoramas[1].first_x == 480, "a changed cell is, with its repeats");
    seed_word(kVBlanks, 103u);
    run_seam(cpu, kReloadSeam, kLeftWord);
    seed_word(kTable + 4u, 16u);
    run_seam(cpu, kLeftSeam, kLeftWord ^ 1u);
    expect(g_panoramas.size() == 2u, "a row's reload or another instruction reads nothing");
    run_seam(cpu, kLeftSeam, kLeftWord);
    expect(g_panoramas.size() == 3u && g_panoramas[2].rows == 0, "a table the sky pass cannot draw is told as none");
}

int main() {
    test_the_level_panorama_is_told_when_it_changes();
    test_each_opening_tells_its_fog_step();
    test_sky_behind_the_world();
    test_widening_rounds_outward();
    test_measured_row_at_16_9();
    test_seams_for_every_edge_and_heading();
    test_row_model_for_every_heading();
    test_native_aspect_is_untouched();
    test_openings_reach_past_their_edges();
    test_the_fog_hears_the_measured_span();
    test_only_clear_openings_reach_past_their_edges();
    test_seams_are_guarded_by_their_instruction_words();
    test_a_tile_tells_its_panorama_column();
    test_mark_buys_one_draw_of_the_same_packet();
    test_marks_expire_and_survive_collisions();
    test_a_pinned_end_squashed_to_nothing_keeps_a_pixel();
    test_the_panorama_is_placed_between_pixels();
    if (g_failures != 0) {
        std::cerr << g_failures << " backdrop tile check(s) failed\n";
        return 1;
    }
    std::cout << "Disruptor backdrop tiles: PASS\n";
    return 0;
}
