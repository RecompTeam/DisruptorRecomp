#include "cpu_state.h"
#include "gte_precision.h"
#include "gpu_temporal_sprite.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace {

struct Note {
    std::uint32_t packet;
    std::int32_t depth;
};

struct Placed {
    std::uint32_t packet;
    std::int32_t x;
    std::int32_t dy;
};

/* x_gpr and y_gpr are 0 where the game keeps the operand on its stack. */
struct TestFunnel {
    std::uint32_t projection;
    int depth_gpr;
    std::array<std::uint32_t, 2> packets;
    int x_gpr;
    int y_gpr;
};

constexpr std::array<TestFunnel, 4> kTestFunnels{{
    {0x8003B98Cu, 16, {0x8003BB88u, 0u}, 7, 5},
    {0x8003BDA0u, 16, {0x8003BFB0u, 0u}, 7, 6},
    {0x8003C4B0u, 21, {0x8003C848u, 0x8003CAD4u}, 0, 0},
    {0x8003D078u, 20, {0x8003D488u, 0u}, 8, 5},
}};
constexpr std::uint32_t kStack = 0x801FF000u;
constexpr std::uint32_t kStoreWord = 0xAFA20010u;
constexpr std::uint32_t kTestRecordSite = 0x8003D1B4u;
constexpr std::uint32_t kTestRecordWord = 0xA4379EA0u;
constexpr std::uint32_t kTestDeferredSite = 0x800433A0u;
constexpr std::uint32_t kPacket = 0x800C4000u;
constexpr std::int32_t kUnits = 8;

std::vector<Note> g_notes;
std::vector<Placed> g_places;
std::uint32_t g_stack_x = 0;
std::uint32_t g_stack_y = 0;
int g_failures = 0;
bool g_squash = true;

struct Sized {
    std::uint32_t packet;
    std::int32_t row;
    std::int32_t width;
    std::int32_t height;
    int shadow;
    int whole;
};
std::vector<Sized> g_sizes;
std::map<std::uint32_t, std::uint8_t> g_memory;

std::uint8_t read_byte(std::uint32_t address) {
    const auto found = g_memory.find(address);
    return found == g_memory.end() ? 0x5Au : found->second;
}

std::uint16_t read_half(std::uint32_t address) {
    return static_cast<std::uint16_t>(read_byte(address) | read_byte(address + 1u) << 8);
}

std::uint32_t read_stack(std::uint32_t address) {
    if (address == kStack + 0x18u) return g_stack_x;
    if (address == kStack + 0x20u) return g_stack_y;
    if (g_memory.count(address) == 0u) return 0x7FFFFFFFu;
    return read_half(address) | static_cast<std::uint32_t>(read_half(address + 2u)) << 16;
}

void put(std::uint32_t address, std::uint32_t value, int bytes) {
    for (int byte = 0; byte < bytes; ++byte) g_memory[address + static_cast<std::uint32_t>(byte)] = static_cast<std::uint8_t>(value >> (8 * byte));
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" void gpu_temporal_note_sprite(CPUState *, std::uint32_t packet, std::int32_t depth) {
    g_notes.push_back({packet, depth});
}

extern "C" void gpu_temporal_place_sprite(std::uint32_t packet, std::int32_t x, std::int32_t dy) {
    g_places.push_back({packet, x, dy});
}

extern "C" void gpu_temporal_size_sprite(std::uint32_t packet, std::int32_t row, std::int32_t width, std::int32_t height,
                                         int shadow, int whole) {
    g_sizes.push_back({packet, row, width, height, shadow, whole});
}

/* Classic wide as the runtime does it: three quarters about column 160, rounded and unrounded. 4:3 without g_squash. */
extern "C" int psx_ws_project_x(int x) {
    if (!g_squash) return x;
    const int scaled = (x - 160) * 3;
    return 160 + (scaled + (scaled >= 0 ? 2 : -2)) / 4;
}

extern "C" std::int32_t psx_ws_project_x16(int x, std::int32_t fraction16) {
    if (!g_squash) return x * 65536 + fraction16;
    return 160 * 65536 + static_cast<std::int32_t>((static_cast<std::int64_t>(x - 160) * 65536 + fraction16) * 3 / 4);
}

std::int32_t g_range_num = 1, g_range_den = 1;
extern "C" void gte_ws_x_squash(std::int32_t *num, std::int32_t *den) {
    *num = g_range_num;
    *den = g_range_den;
}

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;

struct Projected {
    std::uint32_t word;
    std::int32_t x16;
    std::int32_t y16;
    std::uint16_t z;
};
std::map<std::uint32_t, Projected> g_projected;

/* The corners the GTE projected into words the game stored, by the word's physical address. */
GtePrecisionLookupResult gte_precision_load_word_ex(std::uint32_t addr, std::uint32_t packed, std::int32_t *x16, std::int32_t *y16,
                                                    std::uint16_t *z) {
    const auto found = g_projected.find(addr);
    if (found == g_projected.end()) return GTE_PRECISION_LOOKUP_STORE_MISS;
    if (found->second.word != packed) return GTE_PRECISION_LOOKUP_PACKED_MISMATCH;
    *x16 = found->second.x16;
    *y16 = found->second.y16;
    *z = found->second.z;
    return GTE_PRECISION_LOOKUP_ACCEPTED;
}

/* The retail DMAC folds a node address into 2 MiB of RAM; the render arena sits past it from 0xF00000. */
std::uint32_t psx_mod_gpu_dma_resolve_address(std::uint32_t address) {
    const std::uint32_t canonical = address & 0x00FFFFFCu;
    return canonical >= 0x00F00000u ? canonical : canonical & 0x001FFFFCu;
}
}

#include "../../src/game/render/sprite_depth.cpp"

namespace {

void project(CPUState &cpu, const TestFunnel &funnel, std::int32_t depth) {
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
}

bool noted_once(std::uint32_t packet, std::int32_t depth) {
    const bool right = g_notes.size() == 1 && g_notes[0].packet == packet && g_notes[0].depth == depth;
    g_notes.clear();
    return right;
}

void test_every_funnel_hands_its_depth_over() {
    CPUState cpu{};
    std::int32_t depth = 100;
    for (const TestFunnel &funnel : kTestFunnels)
        for (std::uint32_t site : funnel.packets) {
            if (site == 0u) continue;
            depth += 37;
            project(cpu, funnel, depth);
            disruptor_sprite_depth_packet(&cpu, site, kPacket + static_cast<std::uint32_t>(depth));
            expect(noted_once(kPacket + static_cast<std::uint32_t>(depth), depth * kUnits),
                   "a funnel's packet carries the depth of its own projection in GTE units");
            disruptor_sprite_depth_packet(&cpu, site, kPacket);
            expect(g_notes.empty(), "a depth is handed over once");
        }
}

void test_a_depth_stays_in_its_funnel() {
    CPUState cpu{};
    project(cpu, kTestFunnels[0], 300);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[1].packets[0], kPacket);
    expect(g_notes.empty(), "another funnel's packet does not take a depth left behind by a culled sprite");
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "and the depth it refused is gone");

    project(cpu, kTestFunnels[0], 300);
    project(cpu, kTestFunnels[2], 500);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[1], kPacket);
    expect(noted_once(kPacket, 500 * kUnits), "the latest projection is the pending one");
}

void test_only_the_reviewed_instructions_count() {
    CPUState cpu{};
    cpu.gpr[16] = 300;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection, kStoreWord ^ 1u, 1);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection, kStoreWord, 0);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[0].projection + 4u, kStoreWord, 1);
    disruptor_sprite_depth_instruction_hook(nullptr, kTestFunnels[0].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "a changed word, another phase or address and a missing CPU record nothing");

    for (std::int32_t depth : {0, -5, INT32_MAX / kUnits + 1}) {
        project(cpu, kTestFunnels[0], depth);
        disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
        expect(g_notes.empty(), "a depth that is not positive or does not fit is not passed on");
    }
    project(cpu, kTestFunnels[0], 300);
    disruptor_sprite_depth_packet(&cpu, 0x80010000u, kPacket);
    disruptor_sprite_depth_packet(nullptr, kTestFunnels[0].packets[0], kPacket);
    expect(g_notes.empty(), "an unknown packet site and a missing CPU hand nothing over");
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[0].packets[0], kPacket);
    expect(noted_once(kPacket, 300 * kUnits), "and neither of them spends the pending depth");
}

/* What the game holds at the seam for a point (x, y, depth): its operands, and its own two quotients. */
void at_the_seam(CPUState &cpu, const TestFunnel &funnel, std::int32_t x, std::int32_t y, std::int32_t depth) {
    cpu.gpr[29] = kStack;
    cpu.read_word = read_stack;
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    if (funnel.x_gpr) cpu.gpr[funnel.x_gpr] = static_cast<std::uint32_t>(x); else g_stack_x = static_cast<std::uint32_t>(x);
    if (funnel.y_gpr) cpu.gpr[funnel.y_gpr] = static_cast<std::uint32_t>(y); else g_stack_y = static_cast<std::uint32_t>(y);
    cpu.gpr[2] = static_cast<std::uint32_t>(psx_ws_project_x(160 + 160 * x / depth));
    cpu.gpr[3] = static_cast<std::uint32_t>(160 * y / depth);
}

bool placed_once(std::uint32_t packet, std::int32_t x, std::int32_t dy) {
    const bool right = g_places.size() == 1 && g_places[0].packet == packet && g_places[0].x == x && g_places[0].dy == dy;
    g_places.clear();
    g_notes.clear();
    g_sizes.clear();
    return right;
}

bool sized_once(std::int32_t row, std::int32_t width, std::int32_t height, int shadow, int whole) {
    const bool right = g_sizes.size() == 1 && g_sizes[0].packet == kPacket && g_sizes[0].row == row && g_sizes[0].width == width &&
                       g_sizes[0].height == height && g_sizes[0].shadow == shadow && g_sizes[0].whole == whole;
    g_sizes.clear();
    g_places.clear();
    g_notes.clear();
    return right;
}

bool unsized() {
    const bool right = g_sizes.empty() && g_places.size() == 1;
    g_sizes.clear();
    g_places.clear();
    g_notes.clear();
    return right;
}

constexpr std::uint32_t kActor = 0x80120000u;
constexpr std::uint32_t kBody = 0x80130000u;
constexpr std::uint32_t kPictures = 0x80140000u;
constexpr std::uint32_t kTable = 0x80057E98u;
/* The point of every size case: 160 * 100 / 640 and 160 * 37 / 640, row 120 - 9.25 = 110.75. */
constexpr std::int32_t kRow = 7258112;

CPUState ready(const TestFunnel &funnel) {
    CPUState cpu{};
    at_the_seam(cpu, funnel, 100, 37, 640);
    cpu.read_half = read_half;
    cpu.read_byte = read_byte;
    return cpu;
}

void run(CPUState &cpu, const TestFunnel &funnel, std::uint32_t site) {
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, site, kPacket);
}

/* An actor of the fourth funnel: picture 28 x 7 at scales 1024 and 2048 over 64, which is 448 x 224 before the depth. */
void fourth_actor(CPUState &cpu, std::uint32_t kind, std::uint32_t frame, std::uint32_t turned) {
    g_memory.clear();
    cpu.gpr[21] = kActor;
    cpu.gpr[22] = kBody;
    cpu.gpr[16] = 0xABCD00u | turned;
    put(kActor + 0x3Au, kind, 1);
    put(kActor + 0x3Bu, frame, 1);
    put(kBody, kPictures, 4);
    put(kPictures + 20u * (frame + (kind == 7u || kind == 8u ? 0u : turned)) + 8u, 28u | 7u << 8, 2);
    put(kBody + 0x1Cu, 1024u, 4);
    put(kBody + 0x18u, 2048u, 4);
    put(kBody + 0xCu, 64u, 4);
    put(kTable + 2u * 64u, 1024u, 2);
    put(kTable + 2u * 640u, 102u, 2);
    cpu.gpr[18] = 111u;  /* (448 * 160 * 102) >> 16 */
    cpu.gpr[19] = 55u;   /* (224 * 160 * 102) >> 16 */
}

constexpr std::uint32_t kKind = 0x80150000u;

/* An effect of the third funnel: 401 units, a picture 38 x 21 over a reference 40 x 40, with what the game's two cuts leave of it. */
void effect(CPUState &cpu, std::uint32_t which, bool mirrored, std::int32_t depth, std::int32_t x = 100, std::int32_t y = 37) {
    g_memory.clear();
    at_the_seam(cpu, kTestFunnels[2], x, y, depth);
    cpu.gpr[19] = kActor;
    cpu.gpr[20] = kPictures;
    cpu.gpr[30] = 0xABCD00u | (mirrored ? 1u : 0u);
    put(kStack + 0x28u, kKind, 4);
    put(kActor + 0x1Du, which, 1);
    put(kKind + 0x14u, which ? 9999u : 401u, 4);
    put(kKind + 0x18u, which ? 401u : 9999u, 4);
    put(kPictures + 8u, 38u | 21u << 8, 2);
    put(kKind + which + 0x30u, 40u, 1);
    put(kKind + which + 0x34u, 40u, 1);
    const std::int32_t base = 160 * 401 / depth, wide = base * 38 / 40;
    cpu.gpr[22] = static_cast<std::uint32_t>(mirrored ? -wide : wide);
    cpu.gpr[18] = static_cast<std::uint32_t>(base * 21 / 40);
}

void run_effect(CPUState &cpu) {
    run(cpu, kTestFunnels[2], kTestFunnels[2].packets[0]);
}

void test_an_effect_keeps_its_size_before_both_cuts() {
    /* At depth 640 the base is 100.25 px: 95.2375 x 52.63125 exactly, 95 x 52 after the cuts. */
    constexpr std::int32_t wide = 6241484, tall = 3449241;
    for (const std::uint32_t which : {0u, 3u}) {
        CPUState cpu = ready(kTestFunnels[2]);
        effect(cpu, which, false, 640);
        run_effect(cpu);
        expect(sized_once(kRow, wide, tall, 0, 0), "an effect's size is its units over the depth times its picture over its reference");
    }
    CPUState cpu = ready(kTestFunnels[2]);
    effect(cpu, 0u, true, 640);
    run_effect(cpu);
    expect(sized_once(kRow, wide + 2 * 65536, tall, 0, 0), "a mirrored effect's packet spans two columns more, and so does its size");
    effect(cpu, 0u, true, 640);
    cpu.gpr[22] = 95u;
    run_effect(cpu);
    expect(unsized(), "a width the game did not mirror is not a mirrored effect's");
    effect(cpu, 0u, false, 640);
    cpu.gpr[22] = 3u * 95u;
    cpu.gpr[18] = 3u * 52u;
    run_effect(cpu);
    expect(unsized(), "an effect the game went on to grow threefold keeps its packet's size");
    effect(cpu, 0u, false, 640);
    ++cpu.gpr[18];
    run_effect(cpu);
    expect(unsized(), "nor is a height the steps do not end on handed over");
    for (const std::uint32_t reference : {0x30u, 0x34u}) {
        effect(cpu, 0u, false, 640);
        put(kKind + reference, 0u, 1);
        run_effect(cpu);
        expect(unsized(), "an effect without a reference has no size");
    }
    effect(cpu, 0u, false, 640);
    put(kKind + 0x14u, 0u, 4);
    cpu.gpr[22] = cpu.gpr[18] = 0u;
    run_effect(cpu);
    expect(unsized(), "nor one without units");
    effect(cpu, 0u, false, 640);
    cpu.read_byte = nullptr;
    run_effect(cpu);
    expect(unsized(), "nor one that cannot be read");
    cpu.read_byte = read_byte;
    effect(cpu, 0u, false, 60);
    run_effect(cpu);
    expect(unsized(), "nor one with a side over 511 pixels");

    effect(cpu, 0u, false, 640);
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[2].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[1], kPacket);
    expect(unsized(), "the funnel's second packet is another rectangle on the same point: it takes the place and no size");
}

/* Every effect at 4:3: the packet as the game centres it, the place, row and size as the module hands them, through the
 * renderer's own arithmetic. The sides it returns must put the rectangle where the numbers before any cut say. */
void test_the_renderer_takes_every_effect_size() {
    constexpr std::array<std::array<std::int32_t, 2>, 2> points{{{100, 37}, {-77, -45}}};
    int seen = 0, refused = 0, held = 0;
    g_squash = false;
    for (const bool mirrored : {false, true})
        for (const auto &point : points)
            for (std::int32_t depth = 140; depth <= 900; ++depth) {
                CPUState cpu = ready(kTestFunnels[2]);
                effect(cpu, 0u, mirrored, depth, point[0], point[1]);
                const auto s6 = static_cast<std::int32_t>(cpu.gpr[22]), s2 = static_cast<std::int32_t>(cpu.gpr[18]);
                run_effect(cpu);
                ++seen;
                if (g_sizes.size() != 1 || g_places.size() != 1) {
                    ++refused;
                    g_sizes.clear(); g_places.clear(); g_notes.clear();
                    continue;
                }
                const Sized size = g_sizes[0];
                const Placed place = g_places[0];
                g_sizes.clear(); g_places.clear(); g_notes.clear();
                /* 0x8003C628..0x8003C7D8: x0 = x - ($s6 >> 1), x1 = x0 + $s6 - 1, y0 = y - ($s2 >> 1), y2 = y0 + $s2 - 1. */
                const int x0 = 160 + 160 * point[0] / depth - (s6 >> 1), x1 = x0 + s6 - 1;
                const int y0 = 120 - 160 * point[1] / depth - (s2 >> 1), y2 = y0 + s2 - 1;
                const int left = std::min(x0, x1), w = std::abs(x1 - x0), h = y2 - y0;
                const GpuTemporalSprite sprite = gpu_temporal_sprite_from_fixed(1, 1, 0, 0, place.x, place.dy, size.row, size.width, size.height);
                float sides[4];
                gpu_temporal_sprite_sides(&sprite, static_cast<float>(left), static_cast<float>(y0), w, h, w + 1, h + 1, 1.0f, 0, sides);
                const double cut_wide = std::abs(s6), exact_wide = 160.0 * 401.0 * 38.0 / (40.0 * depth);
                const double wide = std::min(exact_wide, cut_wide + 1.5) + (mirrored ? 2.0 : 0.0) - 1.5;
                const double tall = std::min(160.0 * 401.0 * 21.0 / (40.0 * depth), s2 + 1.5) - 1.5;
                const double column = 160.0 + 160.0 * point[0] / depth - 0.25, row = 120.0 - 160.0 * point[1] / depth - 0.25;
                const std::array<double, 4> wanted{column - wide / 2, row - tall / 2, column + wide / 2, row + tall / 2};
                const std::array<double, 4> shown{left + sides[0], y0 + sides[1], left + w + sides[2], y0 + h + sides[3]};
                bool right = true;
                for (std::size_t side = 0; side < 4; ++side)
                    right = right && std::isfinite(sides[side]) && std::fabs(shown[side] - wanted[side]) <= 0.003;
                refused += !right;
                held += exact_wide > cut_wide + 1.5;
            }
    g_squash = true;
    expect(seen == 2 * 2 * 761 && refused == 0,
           "the renderer puts every effect from depth 140 to 900 where its numbers before any cut say, mirrored or not");
    expect(held > 0 && held * 5 < seen, "a side the two cuts left more than a pixel and a half behind is held there, and few are");
}

void test_a_sprite_keeps_its_size_before_the_cut() {
    {
        CPUState cpu = ready(kTestFunnels[0]);
        g_memory.clear();
        cpu.gpr[21] = kActor;
        put(kActor + 4u, 36u, 4);
        put(kActor + 8u, 52u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(sized_once(kRow, 9 * 65536, 13 * 65536, 0, 0), "the first funnel's size is two words of its definition times 160 over the depth");
        put(kActor + 4u, 0u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(unsized(), "a size of nothing is not handed over");
        cpu.read_half = nullptr;
        put(kActor + 4u, 36u, 4);
        run(cpu, kTestFunnels[0], kTestFunnels[0].packets[0]);
        expect(unsized(), "nor a size that cannot be read");
    }
    {
        CPUState cpu = ready(kTestFunnels[1]);
        g_memory.clear();
        cpu.gpr[18] = kActor;
        put(kActor + 8u, 24u | 48u << 8, 2);
        run(cpu, kTestFunnels[1], kTestFunnels[1].packets[0]);
        expect(sized_once(kRow, 6 * 65536, 12 * 65536, 0, 0), "the second funnel's size is two bytes of its definition");
    }
    const TestFunnel &fourth = kTestFunnels[3];
    const auto deferred = [&fourth](CPUState &cpu) {
        at_the_seam(cpu, fourth, 100, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, fourth.projection, kStoreWord, 1);
        cpu.gpr[3] = 20u * 6u;
        disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
        const std::uint32_t body = cpu.gpr[22];
        cpu.gpr[22] = 6u;
        disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
        cpu.gpr[22] = body;
    };
    for (const std::uint32_t kind : {0u, 6u, 7u, 8u, 9u}) {
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, kind, 5u, 3u);
        deferred(cpu);
        expect(sized_once(kRow, 112 * 65536, 56 * 65536, 0, 1),
               "an actor's size follows the game's own steps and is marked as spanning its packet whole");
    }
    {
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kActor + 0x3Au, 7u, 1);
        deferred(cpu);
        expect(unsized(), "a picture taken without the actor's turn is another picture");
        fourth_actor(cpu, 0u, 5u, 3u);
        cpu.gpr[18] = 112u;
        deferred(cpu);
        expect(unsized(), "a width the steps do not end on leaves the place and no size");
        fourth_actor(cpu, 0u, 5u, 3u);
        cpu.gpr[19] = 56u;
        deferred(cpu);
        expect(unsized(), "and so does a height");
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kTable + 2u * 640u, 1024u, 2);
        cpu.gpr[18] = 1120u;
        cpu.gpr[19] = 560u;
        deferred(cpu);
        expect(unsized(), "nor does a side past the game's cap, where it rescales the other");
    }    for (const int broken : {0, 1, 2, 3}) {
        /* A shadow 60 x 32 units, 320 units under the eye, at depth 640: 15 x 8 pixels, centred on row 120 - 80. */
        CPUState cpu = ready(fourth);
        fourth_actor(cpu, 0u, 5u, 3u);
        put(kBody + 0xF8u, 60u, 4);
        put(kBody + 0xFCu, 32u, 4);
        put(kBody + 0xF4u, 100u, 4);
        put(kActor + 0x28u, 500u, 2);
        put(0x800775D0u, 80u, 4);
        put(kPacket + 8u, 200u | (36u + (broken == 3)) << 16, 4);
        put(kPacket + 16u, (214u + (broken == 1)) | 36u << 16, 4);
        put(kPacket + 24u, 200u | (43u + (broken == 2) + (broken == 3)) << 16, 4);
        run(cpu, fourth, fourth.packets[0]);
        if (broken) {
            expect(g_sizes.empty() && g_places.size() == 1 && g_places[0].dy == 0,
                   "a shadow whose numbers do not give its packet keeps the caster's column and no row or size");
            g_places.clear();
            g_notes.clear();
        } else {
            expect(g_places.size() == 1 && g_places[0].dy == 0, "a shadow drops the caster's row");
            expect(sized_once(40 * 65536, 15 * 65536, 8 * 65536, 1, 0), "and takes its own row and size, marked as a shadow");
        }
    }
    g_memory.clear();
}


void test_a_projection_keeps_what_the_divisions_dropped();

void test_sizes_run_after_places() {
    test_a_projection_keeps_what_the_divisions_dropped();
    test_a_sprite_keeps_its_size_before_the_cut();
    test_an_effect_keeps_its_size_before_both_cuts();
    test_the_renderer_takes_every_effect_size();
}

void test_a_projection_keeps_what_the_divisions_dropped() {
    for (const TestFunnel &funnel : kTestFunnels) {
        CPUState cpu{};
        /* 160 * 100 / 640 = 25 exactly, column 185, 178.75 after the squash. 160 * 37 / 640 = 9.25: a quarter pixel up. */
        at_the_seam(cpu, funnel, 100, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        /* The fourth funnel's own packet site is its shadow, which does not keep the caster's row. */
        const bool shadow = funnel.packets[0] == 0x8003D488u;
        expect(placed_once(kPacket, 11714560, shadow ? 0 : -16384), "the unrounded column and the dropped part of the row go with the packet");
        /* 160 * -77 / 1000 = -12.32: column 148 and -0.32 more, 150.76 after the squash. 160 * -45 / 1000 = -7.2: 0.2 down. */
        at_the_seam(cpu, funnel, -77, -45, 1000);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(placed_once(kPacket, 9880208, shadow ? 0 : 13107), "a remainder left of the centre and above it keeps its sign");

        at_the_seam(cpu, funnel, 100, 37, 640);
        ++cpu.gpr[3];
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a row quotient the operands do not give leaves the depth and no place");
        at_the_seam(cpu, funnel, 100, 37, 640);
        ++cpu.gpr[2];
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a stored column the operands do not give leaves the depth and no place");
        at_the_seam(cpu, funnel, 26000, 37, 640);
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a column too far out for 16.16 is not placed");
        at_the_seam(cpu, funnel, 100, 37, 640);
        cpu.gpr[4] = 1u;
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a point the game goes on to warp keeps its depth and no place");
        cpu.gpr[4] = 2u;
        disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, kStoreWord, 1);
        disruptor_sprite_depth_packet(&cpu, funnel.packets[0], kPacket);
        expect(placed_once(kPacket, 11714560, funnel.packets[0] == 0x8003D488u ? 0 : -16384), "any other value of the switch leaves the point alone");
        cpu.gpr[4] = 0u;
    }
    CPUState cpu{};
    at_the_seam(cpu, kTestFunnels[2], 100, 37, 640);
    cpu.read_word = nullptr;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[2].projection, kStoreWord, 1);
    disruptor_sprite_depth_packet(&cpu, kTestFunnels[2].packets[0], kPacket);
    expect(g_places.empty() && noted_once(kPacket, 640 * kUnits), "a stack operand that cannot be read leaves the depth and no place");

    const TestFunnel &deferring = kTestFunnels[3];
    at_the_seam(cpu, deferring, -77, -45, 1000);
    disruptor_sprite_depth_instruction_hook(&cpu, deferring.projection, kStoreWord, 1);
    cpu.gpr[3] = 20u * 9u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    at_the_seam(cpu, deferring, 100, 37, 640);
    disruptor_sprite_depth_instruction_hook(&cpu, deferring.projection, kStoreWord, 1);
    cpu.gpr[22] = 9u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 80u);
    expect(placed_once(kPacket + 80u, 9880208, 13107), "a deferred actor's place waits under its record with its depth");
    cpu.gpr[22] = 9u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 80u);
    expect(g_places.empty() && g_notes.empty(), "and is handed over once");
    disruptor_sprite_depth_packet(&cpu, deferring.packets[0], kPacket);
    g_places.clear();
    g_notes.clear();
}

void test_deferred_actor_waits_under_its_record() {
    CPUState cpu{};
    const TestFunnel &deferring = kTestFunnels[3];
    project(cpu, deferring, 410);
    cpu.gpr[3] = 20u * 7u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    project(cpu, deferring, 520);
    cpu.gpr[3] = 20u * 2u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);

    cpu.gpr[22] = 2u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket + 40u);
    expect(noted_once(kPacket + 40u, 520 * kUnits), "a deferred packet takes the depth stored under its record");
    cpu.gpr[22] = 7u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(noted_once(kPacket, 410 * kUnits), "records are told apart by their index, in any order");
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(g_notes.empty(), "a record's depth is handed over once");

    project(cpu, kTestFunnels[0], 300);
    cpu.gpr[3] = 20u * 4u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    cpu.gpr[22] = 4u;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    expect(g_notes.empty(), "a record does not take a depth projected by another funnel");

    project(cpu, deferring, 410);
    for (std::uint32_t offset : {20u * 4u + 1u, 20u * 256u}) {
        cpu.gpr[3] = offset;
        disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1);
    }
    cpu.gpr[3] = 20u * 4u;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord ^ 1u, 1);
    for (std::uint32_t index : {4u, 256u, 0xFFFFFFFFu}) {
        cpu.gpr[22] = index;
        disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    }
    expect(g_notes.empty(), "a misaligned or out-of-range record and a changed word store nothing");
}

/* Where each funnel holds its range at the projection, and whether the fourth's lead is in it by then. */
struct TestRange {
    int gpr;
    bool led;
};
constexpr std::array<TestRange, 4> kTestRanges{{{18, false}, {19, false}, {23, false}, {23, true}}};

/* The game's steps: the depth and three eighths of the step aside, then the fourth funnel's lead of 0x18 down to 8. */
std::int32_t game_range(bool led, std::int32_t depth, std::int32_t aside) {
    const std::int32_t range = depth + (3 * aside >> 3);
    return led ? std::max(range - 0x18, 8) : range;
}

/* The registers as a funnel has them at its projection, and its range after the seam. */
std::int32_t ranged(std::size_t which, std::int32_t depth, std::int32_t x, std::int32_t off_by = 0,
                    std::uint32_t word = kStoreWord, int phase = 1, bool stack = true) {
    const TestFunnel &funnel = kTestFunnels[which];
    CPUState cpu{};
    cpu.read_word = stack ? read_stack : nullptr;
    cpu.gpr[29] = kStack;
    for (int gpr = 1; gpr < 32; ++gpr)
        if (gpr != 29) cpu.gpr[gpr] = 0x40000000u + static_cast<std::uint32_t>(gpr);
    if (funnel.x_gpr != 0) cpu.gpr[funnel.x_gpr] = static_cast<std::uint32_t>(x);
    g_stack_x = static_cast<std::uint32_t>(x);
    g_stack_y = 0u;
    cpu.gpr[funnel.depth_gpr] = static_cast<std::uint32_t>(depth);
    cpu.gpr[kTestRanges[which].gpr] = static_cast<std::uint32_t>(game_range(kTestRanges[which].led, depth, std::abs(x)) + off_by);
    const CPUState before = cpu;
    disruptor_sprite_depth_instruction_hook(&cpu, funnel.projection, word, phase);
    for (int gpr = 0; gpr < 32; ++gpr)
        expect(gpr == kTestRanges[which].gpr || cpu.gpr[gpr] == before.gpr[gpr], "the seam may change the range register and no other");
    return static_cast<std::int32_t>(cpu.gpr[kTestRanges[which].gpr]);
}

void test_a_range_comes_under_the_squash() {
    for (std::size_t which = 0; which < kTestFunnels.size(); ++which) {
        const bool led = kTestRanges[which].led;
        g_range_num = g_range_den = 1;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 400), "at 4:3 the game's range stands");
        g_range_num = g_range_den = 3;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 400), "a ratio of one in other numbers is no squash");

        g_range_num = 3;
        g_range_den = 4;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 300) && ranged(which, 1000, -400) == game_range(led, 1000, 300),
               "at 16:9 a step aside counts for three quarters of itself, to either side");
        expect(game_range(led, 1000, 400) - game_range(led, 1000, 300) == 38, "which is 38 nearer here: over four sort slots");
        expect(ranged(which, 1000, 7) == game_range(led, 1000, 5), "the squashed step is cut to a whole unit before the eighths");
        expect(ranged(which, 1000, 0) == game_range(led, 1000, 0), "a sprite straight ahead keeps its range");
        expect(ranged(which, 1000, 400, 1) == game_range(led, 1000, 400) + 1 && ranged(which, 1000, 400, -1) == game_range(led, 1000, 400) - 1,
               "a register that does not hold the game's range of this depth and step is left alone");
        expect(ranged(which, 1000, 400, 0, kStoreWord ^ 1u) == game_range(led, 1000, 400) &&
                   ranged(which, 1000, 400, 0, kStoreWord, 0) == game_range(led, 1000, 400),
               "a changed word and the phase before the store change nothing");

        g_range_num = 4;
        g_range_den = 7;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 228), "at 21:9 it counts for four sevenths");
        g_range_num = 16;
        g_range_den = 15;
        expect(ranged(which, 1000, 400) == game_range(led, 1000, 426), "a screen narrower than 4:3 stretches it");
        for (const auto &[num, den] : {std::pair{0, 4}, std::pair{3, 0}, std::pair{-3, -4}}) {
            g_range_num = num;
            g_range_den = den;
            expect(ranged(which, 1000, 400) == game_range(led, 1000, 400), "a ratio that is not two positive numbers changes nothing");
        }
    }

    g_range_num = 3;
    g_range_den = 4;
    expect(ranged(3, 20, 40) == 8 && game_range(true, 20, 40) == 11 && game_range(true, 20, 30) == 8,
           "the fourth funnel's range keeps its lead and its floor");
    expect(ranged(3, 10, 8) == 8, "a range already on its floor stays there");
    for (std::size_t which = 0; which < kTestFunnels.size(); ++which)
        for (const std::int32_t depth : {0, -1000})
            expect(ranged(which, depth, 400) == game_range(kTestRanges[which].led, depth, 400), "a depth the game does not draw is not a sprite's: the range stands");
    expect(ranged(0, 20, 40) == game_range(false, 20, 30) && game_range(false, 20, 30) == 31, "the other funnels have no lead at the projection");
    expect(ranged(2, 1000, 400, 0, kStoreWord, 1, false) == game_range(false, 1000, 400),
           "the third funnel's step is on the stack: unreadable, the range stands");
    g_range_num = g_range_den = 1;
}


constexpr std::uint32_t kGp = 0x80010000u;
constexpr std::uint32_t kOrder = 0x80100000u;
constexpr std::uint32_t kRecords = 0x80080000u;
constexpr std::uint32_t kClutHolder = 0x800C5006u;
constexpr std::uint32_t kPackets = 0x80110000u;
constexpr std::uint32_t kArena = 0x80F00000u;
constexpr std::uint32_t kShadowPacket = 0x80118000u;
constexpr std::uint32_t kTableSlots = 256u;
constexpr std::uint32_t kSprite = 0x2Cu;
constexpr std::uint32_t kWorldQuad = 0x3Cu;
constexpr std::uint32_t kWorldTriangle = 0x34u;
/* The fourth funnel's actor at x 100, y 37, depth 640 covers 122..234 by 82..138 of the packet's coordinates. */
constexpr Box kOverBody{.left = 150, .top = 120, .right = 160, .bottom = 140};
constexpr Box kAside{.left = 300, .top = 10, .right = 310, .bottom = 20};
constexpr Box kAbove{.left = 150, .top = 70, .right = 160, .bottom = 80};
constexpr Box kBeside{.left = 240, .top = 120, .right = 250, .bottom = 130};

void write_half(std::uint32_t address, std::uint16_t value) { put(address, value, 2); }
void write_word(std::uint32_t address, std::uint32_t value) { put(address, value, 4); }

std::uint32_t word_at(std::uint32_t address) {
    return read_half(address) | static_cast<std::uint32_t>(read_half(address + 2u)) << 16;
}

std::uint32_t entry_of(std::uint32_t slot) { return kOrder + 4u * slot; }

std::uint32_t xy(std::int32_t x, std::int32_t y) {
    return static_cast<std::uint16_t>(x) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16;
}

/* The table as the hardware clears it: each slot names the one before it, slot 0 ends the chain. */
void lay_table() {
    for (std::uint32_t slot = 0; slot < kTableSlots; ++slot)
        put(entry_of(slot), slot == 0u ? 0x00FFFFFFu : entry_of(slot - 1u) & 0x00FFFFFFu, 4);
}

/* A packet of `words` words and `code` over `box`, linked at the head of `slot` as the game's 0x8004C600 does. */
void add(std::uint32_t slot, std::uint32_t packet, std::uint32_t words, std::uint32_t code, const Box &box) {
    put(packet, words << 24 | (word_at(entry_of(slot)) & 0x00FFFFFFu), 4);
    put(packet + 7u, code, 1);
    put(packet + 8u, xy(box.left, box.top), 4);
    put(packet + 16u, xy(box.right, box.top), 4);
    put(packet + 24u, xy(box.left, box.bottom), 4);
    put(packet + 32u, xy(box.right, box.bottom), 4);
    put(entry_of(slot), packet & 0x00FFFFFFu, 4);
}

/* The packets of `slot` in the order they are drawn. */
std::vector<std::uint32_t> drawn(std::uint32_t slot) {
    std::vector<std::uint32_t> packets;
    std::uint32_t next = word_at(entry_of(slot)) & 0x00FFFFFFu;
    for (int link = 0; link < 100 && next != 0x00FFFFFFu; ++link) {
        const std::uint32_t at = 0x80000000u | psx_mod_gpu_dma_resolve_address(next);
        if (at >= kOrder && at < entry_of(slot)) break;
        packets.push_back(at);
        next = word_at(at) & 0x00FFFFFFu;
    }
    return packets;
}

std::uint32_t key_at(std::uint32_t index) { return kRecords + 20u * index - 0x6160u; }

void forget() {
    g_notes.clear();
    g_places.clear();
    g_sizes.clear();
}

/* The fourth funnel has projected its actor, the game has its table, and is about to store `key` under record `index`. */
CPUState body_before_its_record(std::uint32_t index, std::uint32_t key) {
    const TestFunnel &fourth = kTestFunnels[3];
    g_record.fill(Sprite{});
    CPUState cpu = ready(fourth);
    fourth_actor(cpu, 0u, 5u, 3u);
    disruptor_sprite_depth_instruction_hook(&cpu, fourth.projection, kStoreWord, 1);
    cpu.gpr[28] = kGp;
    cpu.write_half = write_half;
    cpu.write_word = write_word;
    put(kGp + 836u, kOrder, 4);
    lay_table();
    cpu.gpr[1] = kRecords + 20u * index;
    cpu.gpr[3] = 20u * index;
    cpu.gpr[23] = key;
    put(key_at(index), key, 2);
    return cpu;
}

void record(CPUState &cpu) { disruptor_sprite_depth_instruction_hook(&cpu, kTestRecordSite, kTestRecordWord, 1); }

void test_a_body_sorts_in_front_of_its_floor() {
    CPUState cpu = body_before_its_record(5u, 600u);
    add(75u, kPackets + 256u, 9u, kSprite | 2u, kOverBody);
    add(70u, kPackets, 9u, kSprite, kOverBody);
    add(70u, kPackets + 64u, 9u, kSprite, kAside);
    add(69u, kPackets + 448u, 9u, kSprite, kAbove);
    add(69u, kPackets + 512u, 9u, kSprite, kBeside);
    add(68u, kPackets + 576u, 12u, kSprite, kOverBody);
    add(71u, kPackets + 128u, 9u, kWorldTriangle, kOverBody);
    add(72u, kPackets + 192u, 12u, kWorldQuad, kOverBody);
    add(73u, kArena + 0x100u, 9u, kSprite, kOverBody);
    add(74u, kPackets + 640u, 9u, kSprite, kOverBody);
    add(74u, kPackets + 704u, 9u, kSprite, kOverBody);
    add(67u, kPackets + 320u, 9u, kSprite, kOverBody);
    add(66u, kPackets + 384u, 9u, kSprite, kOverBody);
    record(cpu);
    forget();
    expect(read_half(key_at(5u)) == 536u, "the record's key, which the deferred pass cuts its list and links by, goes 64 nearer");
    expect(drawn(67u) == std::vector<std::uint32_t>{kPackets + 256u, kPackets + 704u, kPackets + 640u, kArena + 0x100u, kPackets,
                                                    kPackets + 320u},
           "the world sprites over the body that it passes go to its new slot in the order they were drawn, still drawn after it");
    bool whole = true;
    for (const std::uint32_t packet : drawn(67u)) whole = whole && word_at(packet) >> 24 == 9u;
    expect(whole, "and each keeps its length");
    expect(drawn(75u).empty() && drawn(74u).empty() && drawn(73u).empty() && drawn(70u) == std::vector<std::uint32_t>{kPackets + 64u},
           "a world sprite elsewhere on the screen stays where the game put it");
    expect(drawn(71u) == std::vector<std::uint32_t>{kPackets + 128u} && drawn(72u) == std::vector<std::uint32_t>{kPackets + 192u},
           "a world polygon, a triangle of nine words too, stays: the body passes it");
    expect(drawn(66u) == std::vector<std::uint32_t>{kPackets + 384u}, "and nothing nearer than the body's new slot moves");
    expect(drawn(69u) == std::vector<std::uint32_t>{kPackets + 512u, kPackets + 448u},
           "a sprite only above the body or only beside it is not over it");
    expect(drawn(68u) == std::vector<std::uint32_t>{kPackets + 576u}, "a packet of another length is no funnel's sprite");

    cpu = body_before_its_record(5u, 600u);
    put(kPackets, 9u << 24 | (word_at(entry_of(70u)) & 0x00FFFFFFu), 4);
    put(kPackets + 7u, kSprite, 1);
    put(kPackets + 8u, xy(kOverBody.left, kOverBody.top), 4);
    put(kPackets + 16u, xy(kOverBody.right, kOverBody.top), 4);
    put(kPackets + 24u, xy(kOverBody.left, kOverBody.bottom), 4);
    put(kPackets + 32u, xy(kOverBody.right, kOverBody.bottom), 4);
    put(entry_of(70u), (kPackets & 0x001FFFFFu) | 0x00600000u, 4);
    record(cpu);
    forget();
    expect(drawn(67u) == std::vector<std::uint32_t>{kPackets} && drawn(70u).empty(),
           "a node named in a mirror of RAM is read where the DMA reads it");

    cpu = body_before_its_record(5u, 5u);
    record(cpu);
    expect(read_half(key_at(5u)) == 5u, "a key under the game's floor is never put farther");
}

void test_a_body_that_cannot_move_stays() {
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        add(70u, kPackets, 9u, kSprite, kOverBody);
        put(entry_of(69u), 0x00FFFFFFu, 4);
        record(cpu);
        expect(read_half(key_at(5u)) == 600u && drawn(70u) == std::vector<std::uint32_t>{kPackets} && drawn(67u).empty(),
               "a chain that ends before the new slot moves nothing and keeps the game's key: no wall was looked for past its end");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        for (std::uint32_t sprite = 0; sprite < 65u; ++sprite) add(70u, kPackets + 64u * sprite, 9u, kSprite, kOverBody);
        record(cpu);
        expect(read_half(key_at(5u)) == 600u && drawn(70u).size() == 65u, "nor do more world sprites than it takes along");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        put(entry_of(70u), entry_of(70u) & 0x00FFFFFFu, 4);
        record(cpu);
        expect(read_half(key_at(5u)) == 600u && drawn(67u).empty(), "nor a chain that turns in a circle");
    }
    {
        CPUState cpu = body_before_its_record(5u, 12u);
        add(1u, kPackets, 9u, kSprite, kOverBody);
        record(cpu);
        expect(read_half(key_at(5u)) == 10u && drawn(1u) == std::vector<std::uint32_t>{kPackets},
               "a key that stays in its slot still goes nearer, to keep its order among bodies, and moves no sprite");
    }
    for (int *flag : {&g_ls_mode, &g_ls_replay_active}) {
        CPUState cpu = body_before_its_record(5u, 600u);
        add(70u, kPackets, 9u, kSprite, kOverBody);
        *flag = 1;
        record(cpu);
        *flag = 0;
        expect(read_half(key_at(5u)) == 600u && drawn(70u) == std::vector<std::uint32_t>{kPackets},
               "the comparator's halves keep the game's own order");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        cpu.write_word = nullptr;
        record(cpu);
        expect(read_half(key_at(5u)) == 600u, "without a way to write the table nothing moves");
    }
    {
        const TestFunnel &fourth = kTestFunnels[3];
        CPUState cpu = body_before_its_record(5u, 600u);
        cpu.gpr[4] = 1u;
        disruptor_sprite_depth_instruction_hook(&cpu, fourth.projection, kStoreWord, 1);
        cpu.gpr[3] = 100u;
        add(70u, kPackets, 9u, kSprite, kOverBody);
        record(cpu);
        expect(read_half(key_at(5u)) == 600u && drawn(70u) == std::vector<std::uint32_t>{kPackets},
               "a body without a place on the screen keeps the game's key: its walls cannot be looked for");
    }
    forget();
}

void shadow(CPUState &cpu, const Box &box) {
    add(0u, kShadowPacket, 9u, kSprite | 2u, box);
    put(entry_of(0u), 0x00FFFFFFu, 4);
    disruptor_sprite_depth_packet(&cpu, 0x8003D488u, kShadowPacket);
    forget();
}

void test_the_shadow_goes_with_its_body() {
    constexpr Box kFeet{.left = 140, .top = 136, .right = 240, .bottom = 146};
    constexpr Box kOverFeet{.left = 236, .top = 141, .right = 239, .bottom = 144};
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        record(cpu);
        add(72u, kPackets, 9u, kSprite, kOverFeet);
        shadow(cpu, kFeet);
        expect(cpu.gpr[23] == 536u, "the shadow sorts where its body does now");
        expect(drawn(67u) == std::vector<std::uint32_t>{kPackets} && drawn(72u).empty(), "and a world sprite over the shadow alone goes along");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        record(cpu);
        cpu.gpr[23] = 601u;
        shadow(cpu, kFeet);
        expect(cpu.gpr[23] == 601u, "a range that is not the body's record key is not its shadow's");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        put(entry_of(69u), 0x00FFFFFFu, 4);
        record(cpu);
        shadow(cpu, kFeet);
        expect(read_half(key_at(5u)) == 600u && cpu.gpr[23] == 600u, "a body whose walk fails keeps the game's key, and so does its shadow");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        record(cpu);
        add(72u, kPackets, 9u, kSprite, kOverFeet);
        g_ls_mode = 1;
        shadow(cpu, kFeet);
        g_ls_mode = 0;
        expect(cpu.gpr[23] == 600u && drawn(72u) == std::vector<std::uint32_t>{kPackets}, "the comparator's halves keep the shadow's slot");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        record(cpu);
        put(entry_of(69u), 0x00FFFFFFu, 4);
        shadow(cpu, kFeet);
        expect(cpu.gpr[23] == 600u, "a shadow whose chain cannot be walked stays");
    }
}

/* The deferred pass reads record `index` back into $s4 as `pass_key`, fades it from `start` and stores the CLUT. */
std::uint32_t clut_after(CPUState &cpu, std::uint32_t index, std::uint32_t start, std::uint32_t pass_key) {
    put(kGp + 1172u, start, 4);
    const auto used = static_cast<std::uint32_t>(std::clamp<std::int32_t>(static_cast<std::int32_t>(pass_key - start), 0, 767)) >> 6;
    put(kClutHolder + 8u, 0x7C20u + (used << 6), 2);
    cpu.gpr[17] = kClutHolder;
    cpu.gpr[20] = pass_key;
    const std::uint32_t body = cpu.gpr[22];
    cpu.gpr[22] = index;
    disruptor_sprite_depth_packet(&cpu, kTestDeferredSite, kPacket);
    cpu.gpr[22] = body;
    forget();
    return (read_half(kClutHolder + 8u) - 0x7C20u) >> 6;
}

void test_the_body_keeps_its_fade() {
    CPUState cpu = body_before_its_record(5u, 600u);
    record(cpu);
    expect(clut_after(cpu, 5u, 200u, 536u) == 6u, "its palette row is the game's key's: (600 - 200) >> 6, not the led key's 5");
    for (const auto &[key, nearer] : {std::pair{1100u, 1036u}, std::pair{230u, 166u}, std::pair{700u, 636u}}) {
        cpu = body_before_its_record(6u, key);
        record(cpu);
        expect(read_half(key_at(6u)) == nearer, "each body below is led, so its fade is the one put back");
    }
    expect(clut_after(cpu, 5u, 200u, 536u) == 5u, "a record is put back once");

    cpu = body_before_its_record(6u, 1100u);
    record(cpu);
    expect(clut_after(cpu, 6u, 200u, 1036u) == 11u, "past the last row both keys fade alike");
    cpu = body_before_its_record(6u, 230u);
    record(cpu);
    expect(clut_after(cpu, 6u, 300u, 166u) == 0u, "and before the first");
    cpu = body_before_its_record(7u, 700u);
    record(cpu);
    expect(clut_after(cpu, 7u, 0u, 637u) == 9u, "a pass that does not hold the led key is left alone");
    cpu = body_before_its_record(7u, 700u);
    record(cpu);
    expect(clut_after(cpu, 8u, 0u, 636u) == 9u, "another record's packet is left alone");
    cpu = body_before_its_record(5u, 600u);
    record(cpu);
    g_ls_replay_active = 1;
    expect(clut_after(cpu, 5u, 200u, 536u) == 5u, "the comparator's halves keep the pass's palette");
    g_ls_replay_active = 0;
}


std::uint32_t led_key(std::uint32_t key) {
    CPUState cpu = body_before_its_record(5u, key);
    record(cpu);
    forget();
    return read_half(key_at(5u));
}

void test_near_bodies_keep_their_order() {
    expect(led_key(600u) == 536u && led_key(136u) == 72u, "from 136 on a key goes the whole 64 nearer");
    expect(led_key(135u) == 71u && led_key(40u) == 24u && led_key(20u) == 14u, "under it halfway to the floor of 8");
    std::uint32_t previous = 0u;
    bool kept = true;
    for (std::uint32_t key = 9u; key < 400u; key += 3u) {
        const std::uint32_t now = led_key(key);
        kept = kept && now >= previous && now >= 8u && now <= key;
        previous = now;
    }
    expect(kept, "so a body never sorts past one the game put nearer, never under the floor, never farther");
}

void test_the_shadow_keeps_the_order_of_what_it_takes_along() {
    constexpr Box kFeet{.left = 140, .top = 136, .right = 240, .bottom = 146};
    constexpr Box kBoth{.left = 150, .top = 134, .right = 160, .bottom = 144};
    constexpr Box kFeetOnly{.left = 236, .top = 141, .right = 239, .bottom = 144};
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        add(75u, kPackets, 9u, kSprite, kBoth);
        add(72u, kPackets + 64u, 9u, kSprite, kFeetOnly);
        add(70u, kPackets + 128u, 9u, kSprite, kBoth);
        add(67u, kPackets + 192u, 9u, kSprite, kBoth);
        record(cpu);
        shadow(cpu, kFeet);
        expect(cpu.gpr[23] == 536u, "the shadow sorts where its body does");
        expect(drawn(67u) == std::vector<std::uint32_t>{kPackets, kPackets + 64u, kPackets + 128u, kPackets + 192u},
               "the sprites over the body and those over the shadow alone are drawn in the game's order");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        add(75u, kPackets + 64u, 9u, kSprite, kFeetOnly);
        add(75u, kPackets, 9u, kSprite, kBoth);
        record(cpu);
        shadow(cpu, kFeet);
        expect(drawn(67u) == std::vector<std::uint32_t>{kPackets, kPackets + 64u}, "also within one slot: the body's first");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        add(75u, kPackets, 9u, kSprite, kBoth);
        add(75u, kPackets + 64u, 9u, kSprite, kFeetOnly);
        record(cpu);
        shadow(cpu, kFeet);
        expect(drawn(67u) == std::vector<std::uint32_t>{kPackets + 64u, kPackets}, "and the shadow's first");
    }
    {
        CPUState cpu = body_before_its_record(5u, 12u);
        record(cpu);
        cpu.gpr[23] = 12u;
        shadow(cpu, kFeet);
        expect(read_half(key_at(5u)) == 10u && cpu.gpr[23] == 10u, "a shadow whose body stays in its slot goes along without a walk");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        add(75u, kPackets, 9u, kSprite, kBoth);
        add(72u, kPackets + 64u, 9u, kSprite, kFeetOnly);
        record(cpu);
        add(67u, kPackets + 128u, 9u, kSprite, kAside);
        shadow(cpu, kFeet);
        expect(cpu.gpr[23] == 600u && drawn(72u) == std::vector<std::uint32_t>{kPackets + 64u},
               "a slot whose head is no longer the body's sprites moves nothing for the shadow");
    }
}


struct Corner {
    double x;
    double y;
    std::uint16_t z;
};

/* A world polygon (POLY_GT4 of twelve words by default) with the GTE's corners at `stride` bytes apart, linked at the head
 * of `slot`; `tracked` false leaves its corners without a projection the runtime kept. */
void add_face(std::uint32_t slot, std::uint32_t packet, const std::array<Corner, 4> &corners, bool tracked = true,
              std::uint32_t code = 0x3Cu, std::uint32_t words = 12u, std::uint32_t count = 4u, std::uint32_t stride = 12u) {
    put(packet, words << 24 | (word_at(entry_of(slot)) & 0x00FFFFFFu), 4);
    put(packet + 7u, code, 1);
    for (std::uint32_t corner = 0; corner < count; ++corner) {
        const std::uint32_t at = packet + 8u + stride * corner;
        const std::uint32_t word = xy(static_cast<std::int32_t>(corners[corner].x), static_cast<std::int32_t>(corners[corner].y));
        put(at, word, 4);
        if (tracked) {
            g_projected[at - 0x80000000u] = {word, static_cast<std::int32_t>(corners[corner].x * 65536.0),
                                             static_cast<std::int32_t>(corners[corner].y * 65536.0), corners[corner].z};
        }
    }
    put(entry_of(slot), packet & 0x00FFFFFFu, 4);
}

/* A wall facing the eye at camera depth `z` over the screen columns `left`..`right`, rows 60..160. */
std::array<Corner, 4> wall(std::uint16_t z, double left = 100.0, double right = 260.0) {
    return {{{left, 60.0, z}, {right, 60.0, z}, {left, 160.0, z}, {right, 160.0, z}}};
}

/* Floor 100 under the eye, from depth 2000 to 6000, 1000 wide: it passes under the body's box. */
constexpr std::array<Corner, 4> kFloor{{{120.0, 128.0, 2000u}, {200.0, 128.0, 2000u}, {146.67, 122.67, 6000u}, {173.33, 122.67, 6000u}}};

void test_a_wall_in_front_stops_a_body() {
    /* The body at depth 640 is 5120 GTE units deep and reaches 1792 towards the eye: a face nearer than 3328 hides it. */
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add(75u, kPackets, 9u, kSprite, kOverBody);
        add(72u, kPackets + 64u, 9u, kSprite, kOverBody);
        add(70u, kPackets + 128u, 9u, kSprite, kOverBody);
        add_face(70u, kPackets + 192u, wall(2000u));
        add(70u, kPackets + 256u, 9u, kSprite, kOverBody);
        add_face(70u, kPackets + 320u, kFloor);
        record(cpu);
        forget();
        expect(read_half(key_at(5u)) == 560u, "a wall in front of the body over its box holds it in the wall's slot");
        expect(drawn(70u) == std::vector<std::uint32_t>{kPackets, kPackets + 64u, kPackets + 320u, kPackets + 256u, kPackets + 192u,
                                                        kPackets + 128u},
               "the sprites it passed go to that slot ahead of everything there, those already there stay");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(4000u));
        record(cpu);
        expect(read_half(key_at(5u)) == 560u, "a wall within the body's reach holds it when the body's middle is past it, as a shut door over a pickup");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 115.0, 4000u}, {260.0, 115.0, 4000u}, {100.0, 160.0, 4000u}, {260.0, 160.0, 4000u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "but not over the lower half of the box only, as the riser of a step before a body");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 60.0, 3500u}, {260.0, 60.0, 12000u}, {100.0, 160.0, 3500u}, {260.0, 160.0, 12000u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a wall within its reach with the body's middle on the eye's side, as one it lies along");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{215.0, 60.0, 5500u}, {260.0, 60.0, 7040u}, {215.0, 160.0, 5500u}, {260.0, 160.0, 7040u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a slanted wall the body's middle is past whose part over the box lies behind the body");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, kFloor);
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor the floor in front of it");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 66.67, 1200u}, {260.0, 66.67, 1200u}, {100.0, 93.33, 2400u}, {260.0, 93.33, 2400u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 600u, "a level face over the top of the body, nearer than its reach, keeps the game's key, as a floor over a drone does");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 66.67, 1200u}, {150.0, 66.67, 1200u}, {100.0, 93.33, 2400u}, {150.0, 93.33, 2400u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 600u, "so does a floor edge over one side of the body, away from its middle");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 66.67, 3000u}, {260.0, 66.67, 3000u}, {100.0, 93.33, 6000u}, {260.0, 93.33, 6000u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "a level face over the top of the body within its reach does not hold it");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 125.0, 1200u}, {260.0, 125.0, 1200u}, {100.0, 135.0, 2400u}, {260.0, 135.0, 2400u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a level face in front over the lower half of the box only, as the floor before a body lying on it is");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u, 280.0, 320.0));
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a wall in front that is not over the body");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u), false);
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a face whose corners the runtime did not keep");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(75u, kPackets + 192u, wall(2000u));
        record(cpu);
        expect(read_half(key_at(5u)) == 600u, "a wall in the body's own slot keeps it there");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u, 20.0, 110.0));
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a wall in front beside the body");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 60.0, 2000u}, {130.0, 60.0, 2000u}, {100.0, 90.0, 2000u}, {0.0, 0.0, 0u}}}, true, 0x34u, 9u, 3u);
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a wall triangle whose corner only comes near the body");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u), true, 0x3Cu, 9u);
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a packet whose length is not its kind's");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u), true, 0x7Cu);
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor a packet that is no polygon");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, {{{100.0, 60.0, 2000u}, {100.0, 60.0, 2000u}, {180.0, 200.0, 2000u}, {260.0, 60.0, 2000u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 560u, "a quad's second triangle holds the body too");
    }
    {
        constexpr Box kFeet{.left = 140, .top = 136, .right = 250, .bottom = 146};
        CPUState cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        record(cpu);
        add_face(72u, kPackets + 192u, wall(2000u, 240.0, 250.0));
        shadow(cpu, kFeet);
        expect(read_half(key_at(5u)) == 536u && cpu.gpr[23] == 600u, "a wall in front of the shadow alone keeps the shadow where it was");
    }
    g_projected.clear();
    forget();
}


/* Another body of the same frame, projected where the first was, stores `key` under record `index`. */
void another_record(CPUState &cpu, std::uint32_t index, std::uint32_t key) {
    cpu.gpr[1] = kRecords + 20u * index;
    cpu.gpr[3] = 20u * index;
    cpu.gpr[23] = key;
    put(key_at(index), key, 2);
    record(cpu);
}

void test_walls_hold_bodies_in_order() {
    {
        CPUState cpu = body_before_its_record(0u, 580u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u));
        record(cpu);
        another_record(cpu, 1u, 600u);
        expect(read_half(key_at(0u)) == 564u && read_half(key_at(1u)) == 565u,
               "two bodies held by one wall keep the game's order: the farther one goes no nearer than the nearer one");
        cpu = body_before_its_record(0u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u));
        record(cpu);
        another_record(cpu, 1u, 580u);
        expect(read_half(key_at(0u)) == 565u && read_half(key_at(1u)) == 564u, "also when the farther one was recorded first");
        cpu = body_before_its_record(0u, 580u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u));
        record(cpu);
        another_record(cpu, 1u, 600u);
        shadow(cpu, Box{.left = 140, .top = 136, .right = 240, .bottom = 146});
        expect(read_half(key_at(1u)) == 565u && cpu.gpr[23] == 560u, "a body kept in order behind its walk leaves its shadow where the walk went");
        cpu = body_before_its_record(0u, 600u);
        g_projected.clear();
        record(cpu);
        another_record(cpu, 1u, 580u);
        another_record(cpu, 2u, 600u);
        expect(read_half(key_at(0u)) == 536u && read_half(key_at(1u)) == 516u && read_half(key_at(2u)) == 536u,
               "bodies no wall holds keep their leads, equal keys stay equal");
        cpu = body_before_its_record(0u, 9u);
        g_projected.clear();
        record(cpu);
        another_record(cpu, 1u, 8u);
        expect(read_half(key_at(0u)) == 9u && read_half(key_at(1u)) == 8u,
               "a body on the floor that keeps its key still puts a farther one recorded before it behind it");
    }
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u, 150.2, 150.8));
        record(cpu);
        expect(read_half(key_at(5u)) == 560u, "a wall thinner than a pixel holds the body");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(70u, kPackets + 192u, wall(2000u), true, kSprite, 9u, 4u, 8u);
        record(cpu);
        expect(read_half(key_at(5u)) == 560u && drawn(70u) == std::vector<std::uint32_t>{kPackets + 192u},
               "a flat textured quad of the world is a wall, not a sprite to take along");
    }
    g_projected.clear();
    forget();
}


/* The fourth funnel projects its actor again, at `depth` this time. */
void project_at(CPUState &cpu, std::int32_t depth) {
    const std::uint32_t reciprocal = static_cast<std::uint32_t>(65536 / depth);
    at_the_seam(cpu, kTestFunnels[3], 100, 37, depth);
    put(kTable + 2u * static_cast<std::uint32_t>(depth), reciprocal, 2);
    cpu.gpr[18] = (448u * 160u * reciprocal) >> 16;
    cpu.gpr[19] = (224u * 160u * reciprocal) >> 16;
    disruptor_sprite_depth_instruction_hook(&cpu, kTestFunnels[3].projection, kStoreWord, 1);
}

void test_a_face_sorted_behind_a_body_it_hides() {
    constexpr Box kFeet{.left = 140, .top = 136, .right = 240, .bottom = 146};
    {
        CPUState cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, wall(4000u));
        record(cpu);
        expect(read_half(key_at(5u)) == 608u, "a face the game sorted behind a body lying past it takes the body to its slot, drawn before it");
        shadow(cpu, kFeet);
        expect(cpu.gpr[23] == 608u, "and the body's shadow with it");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(79u, kPackets + 192u, wall(4000u));
        record(cpu);
        expect(read_half(key_at(5u)) == 632u, "as far as the game's own lead and a slot more");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(80u, kPackets + 192u, wall(4000u));
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "but no farther");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, wall(2000u, 280.0, 320.0));
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor for a face that is not over the body");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, {{{100.0, 60.0, 3500u}, {260.0, 60.0, 12000u}, {100.0, 160.0, 3500u}, {260.0, 160.0, 12000u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor for one whose plane has the body's middle on the eye's side");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, {{{100.0, 60.0, 2651u}, {240.0, 60.0, 33445u}, {100.0, 160.0, 2651u}, {240.0, 160.0, 33445u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor for a wall nearer than the body's reach that the body is not past, as one it lies along");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, {{{100.0, 160.0, 2000u}, {260.0, 160.0, 2000u}, {100.0, 115.0, 2000u}, {260.0, 60.0, 2000u}}});
        record(cpu);
        expect(read_half(key_at(5u)) == 608u, "a quad whose first triangle lies over the lower half only still takes the body back by its second");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, wall(4000u));
        put(kPackets + 192u, 12u << 24 | 0x00FFFFFFu, 4);
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "a chain that ends before the body's slot takes it nowhere, a face on the way or not");
        cpu = body_before_its_record(5u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, wall(4000u));
        put(kPackets + 192u, 12u << 24 | ((kPackets + 192u) & 0x00FFFFFFu), 4);
        record(cpu);
        expect(read_half(key_at(5u)) == 536u, "nor one that runs in a circle after the face");
    }
    {
        CPUState cpu = body_before_its_record(0u, 600u);
        g_projected.clear();
        add_face(76u, kPackets + 192u, wall(4000u));
        record(cpu);
        project_at(cpu, 400);
        another_record(cpu, 1u, 601u);
        expect(read_half(key_at(0u)) == 608u && read_half(key_at(1u)) == 601u,
               "a body in front of that face, sorted farther by the game, stays in front of it: no nearer than its own key");
    }
    g_projected.clear();
    forget();
}


void test_the_nearest_depth_over_a_box() {
    const auto near = [](const Box &box, std::array<std::array<double, 3>, 3> corners) {
        for (auto &corner : corners) corner[2] = 1.0 / corner[2];
        return nearest_over(corners, box);
    };
    const Box middle{.left = 10, .top = 10, .right = 19, .bottom = 19};
    expect(std::fabs(near(middle, {{{0.0, 0.0, 1000.0}, {40.0, 0.0, 1000.0}, {0.0, 40.0, 1000.0}}}) - 1000.0) < 1e-6,
           "a face of one depth over the box is that deep");
    expect(std::fabs(near(middle, {{{0.0, 0.0, 1000.0}, {30.0, 0.0, 2000.0}, {0.0, 30.0, 2000.0}}}) -
                     1.0 / (1.0 / 1000.0 + (1.0 / 2000.0 - 1.0 / 1000.0) * 20.0 / 30.0)) < 1e-6,
           "a sloped face is as near as its nearest point over the box: the box's corner nearest the face's near corner");
    expect(near(middle, {{{30.0, 0.0, 1000.0}, {40.0, 0.0, 1000.0}, {30.0, 10.0, 1000.0}}}) == 0.0, "a face beside the box is not over it");
    expect(near(middle, {{{0.0, 0.0, 1000.0}, {12.0, 0.0, 1000.0}, {0.0, 12.0, 1000.0}}}) == 0.0,
           "nor one whose bounds cross the box but not the face itself");
    expect(near(middle, {{{19.2, 0.0, 1000.0}, {19.8, 0.0, 1000.0}, {19.2, 40.0, 1000.0}}}) > 0.0,
           "the box's last column counts: its right edge is the far side of that pixel");
    const Box left_edge{.left = -27, .top = 10, .right = 5, .bottom = 30};
    expect(std::fabs(near(left_edge, {{{-40.0, 0.0, 1000.0}, {-20.0, 0.0, 1000.0}, {-40.0, 40.0, 1000.0}}}) - 1000.0) < 1e-6,
           "a box partly left of the screen keeps its left edge as a left edge");
    expect(near(left_edge, {{{-60.0, 0.0, 1000.0}, {-30.0, 0.0, 1000.0}, {-60.0, 40.0, 1000.0}}}) == 0.0,
           "and a face left of it is not over it");
}

}  // namespace

int main() {
    test_a_range_comes_under_the_squash();
    test_every_funnel_hands_its_depth_over();
    test_a_depth_stays_in_its_funnel();
    test_only_the_reviewed_instructions_count();
    test_deferred_actor_waits_under_its_record();
    test_sizes_run_after_places();
    test_a_body_sorts_in_front_of_its_floor();
    test_a_body_that_cannot_move_stays();
    test_the_shadow_goes_with_its_body();
    test_the_body_keeps_its_fade();
    test_near_bodies_keep_their_order();
    test_the_shadow_keeps_the_order_of_what_it_takes_along();
    test_a_wall_in_front_stops_a_body();
    test_walls_hold_bodies_in_order();
    test_a_face_sorted_behind_a_body_it_hides();
    test_the_nearest_depth_over_a_box();
    expect(g_places.empty() && g_sizes.empty(), "no test leaves a place or a size behind");
    if (g_failures) return 1;
    std::cout << "Disruptor sprite depth tests passed\n";
    return 0;
}
