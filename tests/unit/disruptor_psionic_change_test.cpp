#include "cpu_state.h"
#include "mod_plugins.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

namespace {

constexpr std::uint32_t kTestTick = 0x80039450u;
constexpr std::uint32_t kTestLoadKind = 0x80071324u;

std::map<std::uint32_t, std::uint8_t> g_ram;
std::vector<int> g_speeds;
PSXModFunctionEntryCallback g_tick = nullptr;
int g_number_keys = 1, g_keys_use = 1, g_netplay = 0;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" {
std::uint8_t psx_mod_read_byte(std::uint32_t address) { return g_ram[address]; }
int psx_mod_register_function_entry_plugin(const char *, std::uint32_t address, PSXModFunctionEntryCallback callback) {
    if (address == kTestTick) g_tick = callback;
    return 1;
}
void cdrom_set_speed(int divisor) { g_speeds.push_back(divisor); }
int psx_netplay_active(void) { return g_netplay; }
int disruptor_number_keys_enabled(void) { return g_number_keys; }
int disruptor_psionic_keys_use_enabled(void) { return g_keys_use; }
}

#include "../../src/game/input/psionic_change.cpp"

namespace {

using namespace disruptor::psionic_change;

/* One tick: the load kind the game has, then the tick's entry. */
std::vector<int> tick(std::uint8_t load) {
    g_ram[kTestLoadKind] = load;
    g_speeds.clear();
    CPUState cpu{};
    if (g_tick) g_tick(&cpu, kTestTick);
    return g_speeds;
}

void test_pictures_read_quickly() {
    expect(g_tick != nullptr, "the hook is on the tick's entry");
    expect(tick(0u) == std::vector<int>{kStockDivisor}, "outside a change every tick gives the stock speed");
    expect(tick(kPicturesLoad) == std::vector<int>{kQuickDivisor}, "a power's pictures are read at 4x");
    expect(tick(kPicturesLoad) == std::vector<int>{kQuickDivisor}, "at every tick of them: no cache stands between");
    expect(tick(0u) == std::vector<int>{kStockDivisor}, "the stock speed comes back when they are read");
    expect(tick(1u) == std::vector<int>{kStockDivisor}, "another load kind is not a power's");
}

void test_settings_and_network() {
    g_keys_use = 0;
    expect(tick(kPicturesLoad) == std::vector<int>{kStockDivisor}, "with the option off a change keeps the stock speed");
    g_keys_use = 1;
    g_number_keys = 0;
    expect(tick(kPicturesLoad) == std::vector<int>{kStockDivisor}, "and without the keys");
    g_number_keys = 1;
    g_netplay = 1;
    expect(tick(kPicturesLoad) == std::vector<int>{kStockDivisor}, "and in a network game");
    g_netplay = 0;
    g_off = true;
    expect(tick(kPicturesLoad) == std::vector<int>{kStockDivisor}, "and when PSX_DISRUPTOR_QUICK_POWER=0 turned it off");
    g_off = false;
}

void test_powers() {
    expect(power_of(kTeraBlast) == kBlast && power_of(2u) == 2u && power_of(kNoPsionic) == kNoPsionic,
           "TeraBlast is Blast's power, the rest are their own");
}

}  // namespace

int main() {
    test_pictures_read_quickly();
    test_settings_and_network();
    test_powers();
    if (g_failures != 0) return 1;
    std::cout << "disruptor psionic change: PASS\n";
    return 0;
}
