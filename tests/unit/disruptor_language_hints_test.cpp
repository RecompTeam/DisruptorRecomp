#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
}

namespace {
std::unordered_map<std::uint32_t, std::uint16_t> g_halves;
std::vector<std::pair<std::uint32_t, PSXModFunctionEntryCallback>> g_hooks;
std::uint32_t g_allocated_at = 0;
std::uint32_t g_allocated_bytes = 0;
int g_allocations = 0;
int g_writes = 0;
int g_stray_writes = 0;
int g_reads_outside_ram = 0;
int g_odd_reads = 0;
int g_started = 1;
int g_netplay = 0;
int g_tall = 1;
int g_registrations = 0;
int g_registration_that_fails = 0;
bool g_memory_left = true;
constexpr std::uint32_t kModMemory = 0x9F000000u;
}  // namespace

int psx_mod_game_started(void) { return g_started; }
int psx_netplay_active(void) { return g_netplay; }
uint16_t psx_mod_read_half(uint32_t address) {
    if (address < 0x80000000u || address > 0x801FFFFEu) ++g_reads_outside_ram;
    if (address % 2 != 0) ++g_odd_reads;
    const auto found = g_halves.find(address);
    return found == g_halves.end() ? std::uint16_t{0} : found->second;
}
void psx_mod_write_half(uint32_t address, uint16_t value) {
    ++g_writes;
    if (address < g_allocated_at || address + 2 > g_allocated_at + g_allocated_bytes) ++g_stray_writes;
    g_halves[address] = value;
}
uint32_t psx_mod_alloc_guest_memory(uint32_t size, uint32_t) {
    ++g_allocations;
    if (!g_memory_left) return 0;
    g_allocated_at = kModMemory;
    g_allocated_bytes = size;
    return g_allocated_at;
}
int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    if (++g_registrations == g_registration_that_fails) return 0;
    g_hooks.emplace_back(address, callback);
    return 1;
}
extern "C" int disruptor_language_disc_tall_hints(void) { return g_tall; }

#include "../../src/game/language/language_hints.cpp"

namespace {

constexpr std::uint32_t kRect = 0x801FFE40u, kOtherRect = 0x801FFD00u;
constexpr std::uint32_t kReadFrom = 0x80020928u, kStageFrom = 0x80020954u, kFirstMoveFrom = 0x80043508u, kSecondMoveFrom = 0x80043518u;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void put_rect(std::uint32_t address, std::uint16_t x, std::uint16_t y, std::uint16_t width, std::uint16_t rows) {
    const std::uint16_t fields[4] = {x, y, width, rows};
    for (std::uint32_t field = 0; field < 4; ++field) g_halves[address + 2 * field] = fields[field];
}

struct Call {
    std::uint32_t first;
    std::uint32_t second;
    std::uint32_t third;
};

/* What a hooked routine is left with in $a0, $a1 and $a2 when it is called with these and returns to that address. */
Call entered(std::size_t hook, std::uint32_t returns_to, Call call) {
    CPUState cpu{};
    for (std::uint32_t index = 0; index < 32; ++index) cpu.gpr[index] = 0x1000u + index;
    cpu.gpr[4] = call.first;
    cpu.gpr[5] = call.second;
    cpu.gpr[6] = call.third;
    cpu.gpr[31] = returns_to;
    const CPUState before = cpu;
    g_hooks[hook].second(&cpu, g_hooks[hook].first);
    const Call left = {cpu.gpr[4], cpu.gpr[5], cpu.gpr[6]};
    cpu.gpr[4] = before.gpr[4];
    cpu.gpr[6] = before.gpr[6];
    expect(std::memcmp(&cpu, &before, sizeof cpu) == 0, "nothing of the CPU state but $a0 and $a2 may change");
    return left;
}

bool same(const Call &left, const Call &right) { return left.first == right.first && left.second == right.second && left.third == right.third; }

const Call kReadCall = {7, 0x80100000u, 0x5000u}, kStageCall = {kRect, 0x80100000u, 9}, kFirstMoveCall = {kRect, 0, 0xA0}, kSecondMoveCall = {kRect, 0, 0x190};

bool read_tall() { return entered(0, kReadFrom, kReadCall).third == 0xA000u; }
bool staged_tall() { return entered(1, kStageFrom, kStageCall).first == kModMemory; }
bool moves_left_alone() { return same(entered(2, kFirstMoveFrom, kFirstMoveCall), kFirstMoveCall) && same(entered(2, kSecondMoveFrom, kSecondMoveCall), kSecondMoveCall); }
bool moved_tall() {
    const Call first = entered(2, kFirstMoveFrom, kFirstMoveCall), second = entered(2, kSecondMoveFrom, kSecondMoveCall);
    return first.first == kModMemory && first.second == 0 && first.third == 0x90 && second.first == kModMemory && second.second == 0 && second.third == 0x180;
}

bool tall_rect_at(std::uint32_t address) {
    return g_halves[address] == 0x2C0 && g_halves[address + 2] == 0x1C0 && g_halves[address + 4] == 0x140 && g_halves[address + 6] == 0x40;
}

void test_three_routines_are_hooked() {
    expect(g_hooks.size() == 3 && g_hooks[0].first == 0x80011888u && g_hooks[1].first == 0x8004D348u && g_hooks[2].first == 0x8004D410u && g_hooked,
           "the read, LoadImage and MoveImage must be hooked, and nothing else");
}

void test_nothing_changes_without_a_tall_disc() {
    put_rect(kRect, 0x2C0, 0x1E0, 0x140, 0x20);
    int *const switches[] = {&g_tall, &g_started, &g_netplay, &g_ls_mode, &g_ls_replay_active};
    for (int *one : switches) {
        *one = !*one;
        expect(!read_tall() && !staged_tall() && moves_left_alone(),
               "a hint is left as the US code has it without a tall disc, before the game runs, and under netplay, lockstep or a replay");
        *one = !*one;
    }
    expect(!staged_tall() && moves_left_alone(), "and a hint that was read as the US code reads it is staged and moved so, whatever has changed since");
    expect(g_allocations == 0 && g_writes == 0, "no mod memory is taken for it");
}

void test_a_tall_hint_is_read_staged_and_moved() {
    const Call read = entered(0, kReadFrom, kReadCall);
    expect(read.first == 7 && read.second == 0x80100000u && read.third == 0xA000u, "the read of a hint takes twice the bytes");
    const Call stage = entered(1, kStageFrom, kStageCall);
    expect(stage.first == kModMemory && stage.second == 0x80100000u && stage.third == 9 && tall_rect_at(kModMemory),
           "LoadImage is handed a rectangle 32 rows higher and 64 tall, from mod memory, and the same data");
    expect(moved_tall() && moved_tall(), "both moves take that rectangle and put it 16 rows higher on their frame buffer, as often as they are made");
    expect(g_allocations == 1 && g_allocated_bytes == 8 && g_writes == 4 && g_stray_writes == 0 && g_reads_outside_ram == 0 && g_odd_reads == 0,
           "the rectangle is written once, into eight bytes of mod memory, and nothing is read outside RAM");
    expect(g_halves[kRect] == 0x2C0 && g_halves[kRect + 2] == 0x1E0 && g_halves[kRect + 6] == 0x20, "the game's own rectangle is as it was");
}

void test_one_decision_serves_a_whole_hint() {
    expect(read_tall(), "a hint is read tall");
    g_netplay = 1;
    g_tall = 0;
    expect(staged_tall() && moved_tall(), "and then staged and moved tall, though the disc and netplay have changed since");
    expect(!read_tall() && !staged_tall() && moves_left_alone(), "the next one is read, staged and moved as the US code does it");
    g_netplay = 0;
    g_tall = 1;
    expect(!staged_tall() && moves_left_alone(), "and stays so when a tall disc is back before it is staged");

    expect(read_tall() && same(entered(0, kReadFrom, {7, 0x80100000u, 0x654u}), {7, 0x80100000u, 0x654u}) && !staged_tall() && moves_left_alone(),
           "a read of another size from the hint's place ends the hint before it");
    put_rect(kOtherRect, 0x2C0, 0x1E0, 0x140, 0x40);
    expect(read_tall() && same(entered(1, kStageFrom, {kOtherRect, 0, 0}), {kOtherRect, 0, 0}) && moves_left_alone() && !staged_tall(),
           "a hint read tall and staged with another rectangle is not moved tall, nor staged tall after all");
    expect(read_tall() && staged_tall() && read_tall() && moves_left_alone(), "a hint read again is not moved before it is staged again");
    expect(staged_tall() && moved_tall(), "and is, once it is");
}

void test_only_the_hint_calls_are_touched() {
    expect(read_tall(), "a hint is read tall");
    for (const std::uint32_t returns_to : {0x80020924u, 0x8002092Cu, 0x80012940u, kStageFrom, kFirstMoveFrom})
        expect(same(entered(0, returns_to, kReadCall), kReadCall), "a read from another place is left alone");
    const Call tall = {7, 0x80100000u, 0xA000u};
    for (const std::uint32_t returns_to : {0x80020950u, 0x80020958u, kReadFrom, kFirstMoveFrom})
        expect(same(entered(1, returns_to, kStageCall), kStageCall), "a LoadImage from another place is left alone");
    expect(staged_tall(), "and none of them ends the hint");
    for (const std::uint32_t returns_to : {0x80043488u, 0x80043498u, 0x80043504u, 0x8004350Cu, 0x80043514u, 0x8004351Cu, kStageFrom})
        expect(same(entered(2, returns_to, kFirstMoveCall), kFirstMoveCall), "a MoveImage from another place is left alone");
    for (const Call &move : {Call{kRect, 0, 0x190}, Call{kRect, 0, 0x90}, Call{kRect, 0, 0}})
        expect(same(entered(2, kFirstMoveFrom, move), move), "the first move to another row is left alone");
    for (const Call &move : {Call{kRect, 0, 0xA0}, Call{kRect, 0, 0x180}})
        expect(same(entered(2, kSecondMoveFrom, move), move), "so is the second");
    const std::uint16_t fields[4] = {0x2C0, 0x1E0, 0x140, 0x20};
    for (std::uint32_t field = 0; field < 4; ++field) {
        put_rect(kRect, fields[0], fields[1], fields[2], fields[3]);
        g_halves[kRect + 2 * field] ^= 0x10;
        expect(moves_left_alone(), "a move with another rectangle is left alone");
        expect(read_tall() && !staged_tall(), "and a LoadImage with one");
        put_rect(kRect, fields[0], fields[1], fields[2], fields[3]);
        expect(read_tall() && staged_tall(), "the next hint is staged tall again");
    }
    for (const std::uint32_t outside : {0x1F801000u, 0x7FFFFFF8u, 0x801FFFFAu, 0x80200000u, 0x9F000000u, 0x801FFE01u, 0u}) {
        put_rect(outside, 0x2C0, 0x1E0, 0x140, 0x20);
        const Call far_move = {outside, 0, 0xA0}, far_stage = {outside, 0x80100000u, 9};
        expect(same(entered(2, kFirstMoveFrom, far_move), far_move), "a rectangle that is not whole in RAM, or at an odd address, is not read by a move");
        expect(read_tall() && same(entered(1, kStageFrom, far_stage), far_stage), "nor by a LoadImage");
        expect(read_tall() && staged_tall(), "the next hint is staged tall again");
    }
    expect(g_reads_outside_ram == 0 && g_odd_reads == 0, "none of them was read");
    put_rect(0x801FFFF8u, 0x2C0, 0x1E0, 0x140, 0x20);
    expect(read_tall() && entered(1, kStageFrom, {0x801FFFF8u, 0, 0}).first == kModMemory && g_reads_outside_ram == 0, "a rectangle that ends with RAM is read");
    expect(same(entered(0, kReadFrom, tall), tall) && !staged_tall(), "a read from the hint's place that is tall already is no US hint");
    expect(g_allocations == 1 && g_writes == 4, "and the rectangle in mod memory is the one written before");
}

void test_no_mod_memory_no_tall_hint() {
    g_tall_stage = 0;
    g_memory_left = false;
    const int writes = g_writes;
    expect(!read_tall() && g_writes == writes, "without mod memory for the rectangle a hint is read as the US code reads it");
    g_memory_left = true;
    expect(!staged_tall() && moves_left_alone(), "and staged and moved so, though there is memory by then");
    expect(read_tall() && staged_tall() && moved_tall() && tall_rect_at(kModMemory), "the next hint has its rectangle");
}

void test_three_hooks_or_none() {
    for (int failing = 1; failing <= 3; ++failing) {
        g_registrations = 0;
        g_registration_that_fails = failing;
        g_hooked = hook_all();
        expect(!g_hooked && g_registrations == failing, "a hook that cannot be registered stops the rest");
        expect(!read_tall() && !staged_tall() && moves_left_alone(), "and with fewer than three hooks no hint is touched");
    }
    g_registrations = 0;
    g_registration_that_fails = 0;
    g_hooked = hook_all();
    expect(g_hooked && g_registrations == 3 && read_tall() && staged_tall() && moved_tall(), "with all three a hint is shown tall");
}

}  // namespace

int main() {
    test_three_routines_are_hooked();
    test_nothing_changes_without_a_tall_disc();
    test_a_tall_hint_is_read_staged_and_moved();
    test_one_decision_serves_a_whole_hint();
    test_only_the_hint_calls_are_touched();
    test_no_mod_memory_no_tall_hint();
    test_three_hooks_or_none();
    if (g_failures != 0) return 1;
    std::cout << "disruptor language hints: PASS\n";
    return 0;
}
