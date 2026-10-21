#include "cpu_state.h"
#include "mod_plugins.h"

#include <cstdint>
#include <iostream>

namespace {

constexpr std::uint32_t kTestClock = 0x800716F8u;
constexpr std::uint32_t kTestModeSite = 0x8003D840u, kTestModeWord = 0x93840310u;
constexpr std::uint32_t kTestDownSite = 0x8003E014u, kTestDownWord = 0x8C637670u;
constexpr std::uint32_t kTestUpSite = 0x8003E098u, kTestUpWord = 0x8C427670u;
constexpr std::uint32_t kTestDown = 0x4000u, kTestUp = 0x1000u;
std::uint32_t g_clock = 0;
int g_notes = 0;
std::uint8_t g_noted_list = 0;
std::uint32_t g_marked = 0xFFu;
int g_marks = 0;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
std::uint32_t g_test_hand = 0u;
std::uint8_t g_test_hand_state = 0u;
std::uint32_t psx_mod_read_word(std::uint32_t address) {
    return address == kTestClock ? g_clock : address == 0x80071320u ? g_test_hand : 0u;
}
std::uint8_t psx_mod_read_byte(std::uint32_t address) { return g_test_hand && address == g_test_hand + 0x62u ? g_test_hand_state : 0u; }
void psx_mod_write_byte(std::uint32_t address, std::uint8_t value) {
    if (g_test_hand && address == g_test_hand + 0x62u) g_test_hand_state = value;
}
void disruptor_capsule_note_list(std::uint8_t list) {
    g_noted_list = list;
    ++g_notes;
}
void disruptor_hud_widgets_mode(std::uint32_t mode) {
    g_marked = mode;
    ++g_marks;
}
}

#include "../../src/game/input/weapon_list.cpp"

namespace {

struct Frame {
    std::uint32_t mode;
    std::uint32_t down;
    std::uint32_t up;
};

/* One pass of the HUD: its read of the mode, then the two reads of the buttons the list makes at mode 2. */
Frame hud(std::uint32_t mode, std::uint32_t buttons) {
    CPUState cpu{};
    cpu.gpr[4] = mode;
    disruptor_weapon_list_instruction_hook(&cpu, kTestModeSite, kTestModeWord, 1);
    Frame frame{cpu.gpr[4], 0u, 0u};
    if (frame.mode != 2u) return frame;
    cpu.gpr[3] = buttons;
    disruptor_weapon_list_instruction_hook(&cpu, kTestDownSite, kTestDownWord, 1);
    cpu.gpr[2] = buttons;
    disruptor_weapon_list_instruction_hook(&cpu, kTestUpSite, kTestUpWord, 1);
    frame.down = cpu.gpr[3];
    frame.up = cpu.gpr[2];
    return frame;
}

void reset() {
    disruptor_weapon_list_state_loaded();
    g_notes = 0;
    g_noted_list = 0;
    g_marked = 0xFFu;
    g_marks = 0;
    g_ls_mode = 0;
    g_ls_replay_active = 0;
    g_clock = 1000u;
}

void test_shown_for_a_second() {
    reset();
    expect(hud(0u, 0u).mode == 0u, "the HUD sees play until a number key picks");
    disruptor_weapon_list_show();
    expect(g_notes == 1, "a capsule records the list");
    expect(hud(0u, 0u).mode == 2u, "a pick shows the list");
    g_clock += 60u;
    expect(hud(0u, 0u).mode == 2u, "for a second of the game's clock");
    g_clock += 1u;
    expect(hud(0u, 0u).mode == 0u, "and no longer");
    g_clock += 1u;
    expect(hud(0u, 0u).mode == 0u, "and stays hidden");

    reset();
    disruptor_weapon_list_show();
    hud(0u, 0u);
    g_clock += 30u;
    disruptor_weapon_list_show();
    hud(0u, 0u);
    g_clock += 60u;
    expect(hud(0u, 0u).mode == 2u, "a second pick starts the second over");

    reset();
    disruptor_weapon_list_show();
    g_clock += 100u;
    expect(hud(4u, 0u).mode == 4u, "the pause keeps its mode");
    g_clock += 60u;
    expect(hud(0u, 0u).mode == 2u, "the second starts at the HUD's first read after the pick");
    g_clock += 1u;
    expect(hud(0u, 0u).mode == 0u, "wherever that read was");

    reset();
    disruptor_weapon_list_show();
    hud(0u, 0u);
    g_clock = 5u;
    expect(hud(0u, 0u).mode == 0u, "a clock that started over ends the list");
    g_clock = 1000u;
    expect(hud(0u, 0u).mode == 0u, "for good");
}

void test_play_goes_on() {
    reset();
    disruptor_weapon_list_show();
    const Frame shown = hud(0u, kTestDown | kTestUp | 0x4u);
    expect(shown.down == 0u && shown.up == 0u, "up and down do not move the list it shows");

    reset();
    const Frame held = hud(2u, kTestDown | kTestUp);
    expect(held.mode == 2u && held.down == (kTestDown | kTestUp) && held.up == (kTestDown | kTestUp),
           "the list's own button keeps up and down");
    disruptor_weapon_list_show();
    const Frame both = hud(2u, kTestDown);
    expect(both.mode == 2u && both.down == kTestDown, "with its button held as well");

    for (std::uint32_t mode : {1u, 3u, 4u, 5u}) {
        reset();
        disruptor_weapon_list_show();
        expect(hud(mode, 0u).mode == mode, "the map, the psionics list and the pause keep their mode");
    }
}

void test_widgets_told() {
    reset();
    hud(3u, 0u);
    expect(g_marks == 1 && g_marked == 3u, "the widgets hear the mode the HUD read");
    disruptor_weapon_list_show();
    hud(0u, 0u);
    expect(g_marks == 2 && g_marked == 2u, "and the list a pick shows");
    g_ls_mode = 1;
    hud(2u, 0u);
    expect(g_marks == 2, "never in the comparator's runs");
    g_ls_mode = 0;
    CPUState cpu{};
    disruptor_weapon_list_instruction_hook(&cpu, kTestDownSite, kTestDownWord, 1);
    expect(g_marks == 2, "nor at a read of the buttons");
}

void test_psionics_list() {
    reset();
    disruptor_psionic_list_show();
    expect(g_notes == 1 && g_noted_list == 2u, "a capsule records the psionics list");
    CPUState cpu{};
    cpu.gpr[4] = 0u;
    disruptor_weapon_list_instruction_hook(&cpu, kTestModeSite, kTestModeWord, 1);
    expect(cpu.gpr[4] == 3u && g_marked == 3u, "a power's key shows the psionics list");
    cpu.gpr[3] = kTestDown;
    disruptor_weapon_list_instruction_hook(&cpu, 0x8003E7B8u, 0x8C637670u, 1);
    cpu.gpr[2] = kTestUp;
    disruptor_weapon_list_instruction_hook(&cpu, 0x8003E84Cu, 0x8C427670u, 1);
    expect(cpu.gpr[3] == 0u && cpu.gpr[2] == 0u, "and its up and down see no press");
    cpu.gpr[3] = kTestDown;
    disruptor_weapon_list_instruction_hook(&cpu, kTestDownSite, kTestDownWord, 1);
    expect(cpu.gpr[3] == kTestDown, "the weapon list's reads are left alone while the psionics list shows");
    disruptor_weapon_list_show();
    expect(g_noted_list == 1u && hud(0u, 0u).mode == 2u, "a weapon's key after it shows the weapon list");
    reset();
    cpu.gpr[4] = 3u;
    cpu.gpr[3] = kTestDown;
    disruptor_weapon_list_instruction_hook(&cpu, kTestModeSite, kTestModeWord, 1);
    disruptor_weapon_list_instruction_hook(&cpu, 0x8003E7B8u, 0x8C637670u, 1);
    expect(cpu.gpr[3] == kTestDown, "the list's own button keeps its moves");
}

/* The psionics branch: its own read of the mode, then the store that idles the hand, done here as the game does it. */
std::uint32_t psionics_branch(std::uint32_t mode) {
    CPUState cpu{};
    cpu.gpr[4] = mode;
    disruptor_weapon_list_instruction_hook(&cpu, kTestModeSite, kTestModeWord, 1);
    cpu.gpr[3] = mode;
    disruptor_weapon_list_instruction_hook(&cpu, 0x8003E0F8u, 0x93830310u, 1);
    if (cpu.gpr[3] == 3u) {
        g_test_hand_state = 0u;  // 0x8003E118 idles the hand
        g_test_hand_state = 1u;  // 0x8003E8C4 flags it
    }
    disruptor_weapon_list_instruction_hook(&cpu, 0x8003E8C8u, 0x8FBF00DCu, 1);
    return cpu.gpr[3];
}

void test_psionics_hand() {
    reset();
    g_test_hand = 0x80060200u;
    g_test_hand_state = 7u;
    disruptor_psionic_list_show();
    expect(psionics_branch(0u) == 3u && g_test_hand_state == 7u,
           "the psionics branch sees its list, and the hand keeps its state through the list's idling and flagging");
    g_test_hand_state = 7u;
    expect(psionics_branch(3u) == 3u && g_test_hand_state == 1u, "the game's own list leaves the hand as before");
    reset();
    disruptor_weapon_list_show();
    expect(psionics_branch(0u) == 0u, "the weapon list a key shows leaves the psionics branch shut");
    g_test_hand = 0u;
}

void test_loaded_state() {
    reset();
    disruptor_weapon_list_show();
    hud(0u, 0u);
    disruptor_weapon_list_state_loaded();
    expect(hud(0u, 0u).mode == 0u, "a loaded state ends the list");
    disruptor_weapon_list_show();
    disruptor_weapon_list_state_loaded();
    expect(hud(0u, 0u).mode == 0u, "and a pick the HUD has not seen yet");
}

void test_guards() {
    reset();
    disruptor_weapon_list_show();
    g_ls_mode = 1;
    expect(hud(0u, 0u).mode == 0u, "the comparator's compiled run never sees the list");
    reset();
    disruptor_weapon_list_show();
    g_ls_replay_active = 1;
    expect(hud(0u, 0u).mode == 0u, "nor its interpreted run");

    reset();
    disruptor_weapon_list_show();
    CPUState cpu{};
    disruptor_weapon_list_instruction_hook(&cpu, kTestModeSite, kTestModeWord, 0);
    disruptor_weapon_list_instruction_hook(&cpu, kTestModeSite + 4u, kTestModeWord, 1);
    disruptor_weapon_list_instruction_hook(&cpu, kTestModeSite, kTestModeWord + 1u, 1);
    disruptor_weapon_list_instruction_hook(nullptr, kTestModeSite, kTestModeWord, 1);
    expect(cpu.gpr[4] == 0u, "only the pinned word, after it ran, at its own address");
    cpu.gpr[3] = kTestDown;
    disruptor_weapon_list_instruction_hook(&cpu, kTestDownSite, kTestDownWord, 1);
    expect(cpu.gpr[3] == kTestDown, "no read of the buttons is changed before the HUD showed the list");
}

}  // namespace

int main() {
    test_shown_for_a_second();
    test_play_goes_on();
    test_widgets_told();
    test_psionics_list();
    test_psionics_hand();
    test_loaded_state();
    test_guards();
    if (g_failures != 0) return 1;
    std::cout << "disruptor weapon list: PASS\n";
    return 0;
}
