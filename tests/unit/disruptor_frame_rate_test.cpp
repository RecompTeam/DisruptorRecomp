#include "cpu_state.h"
#include "interrupts.h"
#include "mod_plugins.h"
#include "psx_cyc.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

constexpr std::uint32_t kVSync = 0x8004B3D4u;
constexpr std::uint32_t kGateReturn = 0x80043E84u;
constexpr std::uint32_t kLoopBegin = 0x800438FCu;
constexpr std::uint32_t kLoopEnd = 0x800442F4u;
constexpr std::uint32_t kElsewhere = 0x80020000u;
constexpr std::uint32_t kVBlankCounterWord = 0x8005B920u;
constexpr std::uint32_t kFixedStepWord = 0x80071438u;
constexpr std::uint32_t kLoopModeByte = 0x800716F0u;
constexpr std::uint32_t kPlayerStateByte = 0x8007145Cu;
constexpr std::uint32_t kCyclesPerVBlank = 564480u;
constexpr std::uint32_t kPoll = 0xFFFFFFFFu;
constexpr int kLastFrameRegister = 22;

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
CPUState g_cpu{};
PSXModFunctionEntryCallback g_hook = nullptr;
std::uint32_t g_hook_address = 0;
std::uint32_t g_cycles_to_vblank = kCyclesPerVBlank;
int g_failures = 0;

std::size_t physical(std::uint32_t address) {
    return static_cast<std::size_t>(address & 0x001FFFFFu);
}

void seed_word(std::uint32_t address, std::uint32_t value) {
    const std::size_t p = physical(address);
    for (int i = 0; i < 4; i++)
        g_ram[p + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

uint32_t g_psx_cyc_overclock_shift = 0;
uint32_t g_psx_cyc_overclock_carry = 0;

uint8_t psx_mod_read_byte(uint32_t address) {
    return g_ram[physical(address)];
}

uint32_t psx_mod_read_word(uint32_t address) {
    const std::size_t p = physical(address);
    std::uint32_t value = 0;
    for (int i = 0; i < 4; i++)
        value |= static_cast<std::uint32_t>(g_ram[p + i]) << (8 * i);
    return value;
}

int psx_mod_register_function_entry_plugin(
        const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    g_hook_address = address;
    g_hook = callback;
    return 1;
}

uint32_t interrupts_cycles_to_vblank(void) { return g_cycles_to_vblank; }

#include "../../src/game/frame/frame_rate.cpp"

namespace {

/* One guest call of VSync(argument): what the hook leaves in $a0. */
std::uint32_t vsync(std::uint32_t argument, std::uint32_t return_address) {
    g_cpu.gpr[4] = argument;
    g_cpu.gpr[31] = return_address;
    g_hook(&g_cpu, kVSync);
    return g_cpu.gpr[4];
}

/* A poll from `return_address` after a wait: the divisor it leaves. */
std::uint32_t poll(std::uint32_t return_address) {
    (void)vsync(1u, kElsewhere);
    (void)vsync(kPoll, return_address);
    return g_psx_cyc_overclock_shift;
}

void retail_state() {
    seed_word(kFixedStepWord, 0u);
    g_ram[physical(kLoopModeByte)] = 0u;
    g_ram[physical(kPlayerStateByte)] = 0u;
}

bool held_at_retail_cadence() {
    const bool gate = vsync(0u, kGateReturn) == 2u;
    return gate && poll(kLoopBegin) == 0u;
}

/* `frames` gameplay frames, each `period` VBlanks long and half a VBlank of
 * work past its last one. */
void run_frames(std::uint32_t first_vblank, std::uint32_t frames,
                std::uint32_t period) {
    seed_word(kVBlankCounterWord, first_vblank + 100000u);
    g_cpu.gpr[kLastFrameRegister] = first_vblank;
    (void)vsync(0u, kGateReturn);
    g_cycles_to_vblank = kCyclesPerVBlank / 2u;
    for (std::uint32_t frame = 0; frame < frames; frame++) {
        const std::uint32_t began = first_vblank + frame * period;
        g_cpu.gpr[kLastFrameRegister] = began;
        seed_word(kVBlankCounterWord, began + period - 1u);
        (void)vsync(0u, kGateReturn);
    }
}

}  // namespace

int main() {
    expect(g_hook != nullptr && g_hook_address == kVSync,
           "the module hooks the VSync entry");
    if (!g_hook) return 1;

    retail_state();
    disruptor_frame_rate_set_unlocked(0);
    expect(disruptor_frame_rate_unlocked() == 0, "the switch reads back off");
    expect(vsync(0u, kGateReturn) == 2u,
           "locked, the frame gate waits for the second VBlank");
    expect(poll(kLoopBegin) == 0u,
           "locked, the frame loop keeps the stock instruction cost");
    expect(vsync(0u, kElsewhere) == 0u, "another VSync(0) is left alone");
    expect(vsync(3u, kGateReturn) == 3u,
           "a longer wait at the gate is left alone");
    g_cpu.gpr[4] = 0u;
    g_cpu.gpr[31] = kGateReturn;
    g_hook(&g_cpu, kVSync + 4u);
    expect(g_cpu.gpr[4] == 0u, "another function entry is left alone");

    disruptor_frame_rate_set_unlocked(1);
    expect(disruptor_frame_rate_unlocked() == 1, "the switch reads back on");
    expect(vsync(0u, kGateReturn) == 0u,
           "unlocked, the frame gate waits for the next VBlank only");
    expect(poll(kLoopBegin) != 0u && poll(kLoopEnd - 4u) != 0u,
           "unlocked, a poll inside the frame loop starts the fast section");
    expect(poll(kLoopBegin - 4u) == 0u && poll(kLoopEnd) == 0u,
           "a poll outside the frame loop does not start it");
    (void)vsync(kPoll, kLoopBegin);
    (void)vsync(kPoll, kElsewhere);
    expect(g_psx_cyc_overclock_shift != 0u,
           "a poll from library code does not end the fast section");
    (void)vsync(0u, kElsewhere);
    expect(g_psx_cyc_overclock_shift == 0u,
           "any waiting VSync ends the fast section");
    expect(poll(kLoopBegin) == 2u, "a section at four times");
    g_psx_cyc_overclock_carry = 3u;
    (void)vsync(kPoll, kElsewhere);
    (void)vsync(kPoll, kLoopBegin);
    expect(g_psx_cyc_overclock_carry == 3u, "keeps its carry through library and frame loop polls");
    (void)vsync(0u, kElsewhere);
    expect(g_psx_cyc_overclock_shift == 0u && g_psx_cyc_overclock_carry == 0u,
           "and the wait that ends it ends its carry");
    disruptor_frame_rate_set_cpu_multiplier(8);
    expect(poll(kLoopBegin) == 3u && g_psx_cyc_overclock_carry == 0u,
           "so a section at another divisor starts from none");
    g_psx_cyc_overclock_carry = 7u;
    disruptor_frame_rate_set_cpu_multiplier(4);
    (void)vsync(kPoll, kLoopBegin);
    expect(g_psx_cyc_overclock_shift == 2u && g_psx_cyc_overclock_carry == 0u,
           "and a divisor changed between two polls of one section drops the carry too");
    (void)vsync(0u, kElsewhere);

    seed_word(kFixedStepWord, 1u);
    expect(held_at_retail_cadence(),
           "a fixed step override keeps the retail cadence");
    retail_state();
    g_ram[physical(kLoopModeByte)] = 3u;
    expect(held_at_retail_cadence(),
           "the attract mode keeps the retail cadence");
    retail_state();
    g_ram[physical(kPlayerStateByte)] = 5u;
    expect(held_at_retail_cadence(), "player state 5 keeps the retail cadence");
    retail_state();
    g_ram[physical(kLoopModeByte)] = 2u;
    g_ram[physical(kPlayerStateByte)] = 4u;
    expect(vsync(0u, kGateReturn) == 0u && poll(kLoopBegin) != 0u,
           "other loop modes and player states stay unlocked");
    retail_state();

    DisruptorFrameRateWindow window{};
    run_frames(1000u, 60u, 1u);
    expect(disruptor_frame_rate_last_window(&window) == 1 &&
               window.frames == 60u && window.vblanks == 60u &&
               window.late_frames == 0u &&
               window.average_work_permille == 500u &&
               window.peak_work_permille == 500u,
           "sixty frames of half a VBlank fill one window, none late");
    run_frames(5000u, 30u, 2u);
    expect(disruptor_frame_rate_last_window(&window) == 1 &&
               window.frames == 30u && window.vblanks == 60u &&
               window.late_frames == 30u &&
               window.average_work_permille == 1500u &&
               window.peak_work_permille == 1500u,
           "thirty frames of one and a half VBlanks fill one window, all late");
    expect(disruptor_frame_rate_last_window(nullptr) == 0,
           "a missing output is refused");

    if (g_failures != 0) return 1;
    std::cout << "Disruptor frame-rate tests passed\n";
    return 0;
}
