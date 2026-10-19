#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "spu.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
}

namespace {
int g_detailed = 0;
std::vector<std::string> g_notes;
bool g_voice_on = true;
}  // namespace

extern "C" int disruptor_diagnostics_detailed(void) { return g_detailed; }
extern "C" void disruptor_diagnostics_note(const char *format, ...) {
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    g_notes.emplace_back(text);
}
extern "C" void spu_get_voice_state(int voice, SpuVoiceState *out) {
    *out = SpuVoiceState{};
    out->active = voice == 21 && g_voice_on ? 1 : 0;
}

namespace {
std::unordered_map<std::uint32_t, std::uint32_t> g_words;
std::unordered_map<std::uint32_t, std::uint8_t> g_bytes;
PSXModFunctionEntryCallback g_hook = nullptr;
std::uint32_t g_hooked_at = 0;
int g_started = 1;
int g_netplay = 0;
int g_wait = 588;
std::uint32_t g_asked_mission = 0xFFFFFFFFu;
}  // namespace

int psx_mod_game_started(void) { return g_started; }
int psx_netplay_active(void) { return g_netplay; }
uint8_t psx_mod_read_byte(uint32_t address) {
    const auto found = g_bytes.find(address);
    return found == g_bytes.end() ? 0u : found->second;
}
uint32_t psx_mod_read_word(uint32_t address) {
    const auto found = g_words.find(address);
    return found == g_words.end() ? 0u : found->second;
}
void psx_mod_write_word(uint32_t address, uint32_t value) { g_words[address] = value; }
int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    g_hooked_at = address;
    g_hook = callback;
    return 1;
}
extern "C" int disruptor_language_disc_briefing_wait(uint32_t mission) {
    g_asked_mission = mission;
    return g_wait;
}

#include "../../src/game/language/language_briefing.cpp"

namespace {

constexpr std::uint32_t kCountAt = 0x80071700u, kMissionAt = 0x80071518u;
constexpr std::uint32_t kCallbackMoves[] = {0x800122A4u, 0x800122B4u, 0x800122D8u, 0x800122E8u};

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* One VBlank of the loading screen's callback: its three MoveImage calls, then its raise of the count. */
void callback_runs() {
    for (const std::uint32_t returns_to : kCallbackMoves) {
        CPUState cpu{};
        cpu.gpr[31] = returns_to;
        g_hook(&cpu, kMoveImage);
    }
    g_words[kCountAt] += 1u;
}

/* The US loop after the key-on zeroed the count: the VBlank whose raise first lets `slti v0, count, 540` fall through. */
int mission_starts_after(int most = 5000) {
    g_words[kCountAt] = 0u;
    for (int vblank = 1; vblank <= most; ++vblank) {
        callback_runs();
        if (static_cast<int32_t>(g_words[kCountAt]) >= 540) return vblank;
    }
    return -1;
}

void test_the_mission_starts_on_the_tables_vblank() {
    g_bytes[kMissionAt] = 4u;
    for (const int wait : {588, 1447, 541, 540, 539, 300, 1, 0}) {
        g_wait = wait;
        const int starts = mission_starts_after();
        expect(starts == (wait < 1 ? 1 : wait),
               ("the mission starts on the VBlank the table names, a wait of 0 on the first, wait " + std::to_string(wait)).c_str());
    }
    expect(g_asked_mission == 4u, "the wait is the one of the mission loaded");
    expect(g_hooked_at == 0x8004D410u, "the hook sits at MoveImage's entry");
}

void test_the_count_carries_the_wait() {
    g_wait = 588;
    g_words[kCountAt] = 0u;
    for (int vblank = 1; vblank <= 560; ++vblank) callback_runs();
    const auto saved = g_words[kCountAt];
    expect(static_cast<int32_t>(saved) < 0, "a long wait past 539 is kept below zero, where the US loop still waits");
    int left = 0;
    while (static_cast<int32_t>(g_words[kCountAt]) < 540 && left < 100) {
        callback_runs();
        ++left;
    }
    expect(left == 588 - 560, "from what the count holds alone the wait goes on to its end, as after a state load");
    g_words[kCountAt] = saved;
    g_hooked = false;
    int without = 0;
    while (static_cast<int32_t>(g_words[kCountAt]) < 540 && without < 2000) {
        callback_runs();
        ++without;
    }
    g_hooked = true;
    expect(without == (588 - 560) + 540, "a build without the module waits the rest and 540 more, and never forever");
}

void test_the_us_wait_stands_otherwise() {
    g_wait = -1;
    expect(mission_starts_after() == 540, "without a language disc the US wait stands");
    g_wait = 588;
    g_netplay = 1;
    expect(mission_starts_after() == 540, "under netplay the US wait stands");
    g_netplay = 0;
    g_ls_mode = 1;
    expect(mission_starts_after() == 540, "under lockstep too");
    g_ls_mode = 0;
    g_ls_replay_active = 1;
    expect(mission_starts_after() == 540, "and in a lockstep replay");
    g_ls_replay_active = 0;
    g_started = 0;
    expect(mission_starts_after() == 540, "and before the game runs");
    g_started = 1;
}

void test_only_the_callbacks_last_move_counts() {
    g_wait = 300;
    g_words[kCountAt] = 299u;
    CPUState cpu{};
    for (const std::uint32_t returns_to : {0x800122A4u, 0x800122B4u, 0x800122D8u, 0x800122ECu, 0x80043510u}) {
        cpu.gpr[31] = returns_to;
        g_hook(&cpu, kMoveImage);
    }
    expect(g_words[kCountAt] == 299u, "a MoveImage that does not return before the raise of the count leaves it");
    g_hook(nullptr, kMoveImage);
    expect(g_words[kCountAt] == 299u, "and so does a call without a CPU");
    cpu.gpr[31] = 0x800122E8u;
    g_hook(&cpu, kMoveImage);
    expect(g_words[kCountAt] == 539u, "the one before the raise sets it");
}

void test_the_count_after_the_wait_is_left() {
    g_wait = 300;
    for (const std::uint32_t after : {540u, 541u, 100000u}) {
        g_words[kCountAt] = after;
        CPUState cpu{};
        cpu.gpr[31] = 0x800122E8u;
        g_hook(&cpu, kMoveImage);
        expect(g_words[kCountAt] == after, "a count the wait is over for is never touched");
    }
}

void test_a_briefing_is_noted_once_in_the_detailed_log() {
    g_notes.clear();
    g_detailed = 1;
    g_wait = 588;
    g_bytes[kMissionAt] = 4u;
    (void)mission_starts_after();
    expect(g_notes.size() == 1 && g_notes[0] == "mission 4 briefing begins, the language disc's player starts the mission 588 VBlanks later, the US one 540",
           "the detailed log notes a briefing once, at its first VBlank");
    g_notes.clear();
    g_voice_on = false;
    (void)mission_starts_after();
    expect(g_notes.empty(), "a loading screen with no briefing playing is not noted");
    g_voice_on = true;
    g_detailed = 0;
    (void)mission_starts_after();
    expect(g_notes.empty(), "nor anything without the detailed log");
}

}  // namespace

int main() {
    test_the_mission_starts_on_the_tables_vblank();
    test_the_count_carries_the_wait();
    test_the_us_wait_stands_otherwise();
    test_only_the_callbacks_last_move_counts();
    test_the_count_after_the_wait_is_left();
    test_a_briefing_is_noted_once_in_the_detailed_log();
    if (g_failures == 0) std::cout << "disruptor language briefing tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
