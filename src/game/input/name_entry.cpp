/*
 * A save's name typed on the keyboard (SLUS-00224).
 *
 * The front end's save screen is one routine, 0x8001C484(buttons pressed this frame). On its name rows the name
 * is eight codes at 0x8007175C (the table at 0x80056F08: letters, digits, blank), the cursor a word at gp + 0x538
 * with 8 for DONE, the row a word at gp + 0x33C. Keys the host hands over are put there on the way into the
 * routine, and that frame's presses are dropped: a letter key is a pad button as well. Enter only moves the
 * cursor to DONE and stays a press: the game's own confirm then goes on to ACCEPT NAME.
 *
 * The pad is sampled from the keyboard's state, which shows a key up to a frame before its event comes, so on the
 * name rows a key that types is also kept out of that state for the pad, until it is let go.
 */

#include "cpu_state.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>

extern "C" uint64_t psx_get_cycle_count(void);

namespace {

constexpr uint32_t kSaveScreen = 0x8001C484u;
constexpr uint32_t kRow = 0x80071488u;     /* gp + 0x33C */
constexpr uint32_t kCursor = 0x80071684u;  /* gp + 0x538 */
constexpr uint32_t kName = 0x8007175Cu;
constexpr uint32_t kLettersRow = 5u, kLastNameRow = 7u;
constexpr uint32_t kLength = 8u;  /* the cursor past the last letter stands on DONE */
constexpr uint8_t kDigits = 26u, kBlank = 36u;
constexpr unsigned kLeftScreen = 8u;  /* VBlanks without the routine: the menu runs it every frame */
constexpr uint64_t kVBlankCycles = 564480u;  /* the framework's VBLANK_CYCLES */
constexpr unsigned kLetGo = 120u;     /* VBlanks without a key event: a held key repeats many times in that */

/* SDL scancodes, the same numbers in SDL 2 and SDL 3. */
constexpr int kKeyA = 4, kKeyZ = 29, kKey1 = 30, kKey9 = 38, kKey0 = 39, kBackspace = 42, kSpace = 44, kDelete = 76;
constexpr int kPad1 = 89, kPad9 = 97, kPad0 = 98, kEnter = 40, kPadEnter = 88, kSlash = 56, kSecondBackslash = 100;

enum class Act : uint8_t { Put, Back, Clear, Done };

struct Typed {
    Act act;
    uint8_t code;
};

std::array<Typed, 32> g_typed{};
std::size_t g_count = 0;
std::bitset<512> g_held;
std::bitset<512> g_withheld;  /* keys the pad does not see: a typing key found down on the name rows, until it is up */
std::size_t g_text = 0;  /* how many of the typed are text: Enter is not */
uint64_t g_entered = 0, g_keyed = 0;  /* the game's clock at the routine's last entry and at the last key event */
bool g_entered_once = false;
bool g_on = false, g_letters = false;  /* at the routine's last entry: the name rows took text, and the row was the letters' */

/* A key as the layout names it when that is a Latin letter, a digit or a space. A letter key the layout gives a
 * sign past ASCII is the letter of its place on the board, and the digit row and the keypad are digits in every layout. */
[[nodiscard]] constexpr std::optional<Typed> typed_of(int scancode, uint32_t keycode) {
    if (scancode == kBackspace) return Typed{Act::Back, kBlank};
    if (scancode == kDelete) return Typed{Act::Clear, kBlank};
    if (keycode >= 'a' && keycode <= 'z') return Typed{Act::Put, static_cast<uint8_t>(keycode - 'a')};
    if (keycode >= 'A' && keycode <= 'Z') return Typed{Act::Put, static_cast<uint8_t>(keycode - 'A')};
    if (keycode >= '0' && keycode <= '9') return Typed{Act::Put, static_cast<uint8_t>(kDigits + keycode - '0')};
    if (keycode == ' ' || scancode == kSpace) return Typed{Act::Put, kBlank};
    if (keycode >= 0x80u && scancode >= kKeyA && scancode <= kKeyZ) return Typed{Act::Put, static_cast<uint8_t>(scancode - kKeyA)};
    if (scancode >= kKey1 && scancode <= kKey9) return Typed{Act::Put, static_cast<uint8_t>(kDigits + 1 + scancode - kKey1)};
    if (scancode >= kPad1 && scancode <= kPad9) return Typed{Act::Put, static_cast<uint8_t>(kDigits + 1 + scancode - kPad1)};
    if (scancode == kKey0 || scancode == kPad0) return Typed{Act::Put, kDigits};
    return std::nullopt;
}

/* A key of the typing block, which some layout types with: letters, digits and the signs beside them, space, the
 * keypad's digits, both erasers. Enter, Tab and Escape are not of it. */
[[nodiscard]] constexpr bool in_typing_block(int scancode) {
    return (scancode >= kKeyA && scancode <= kKey0) || (scancode >= kSpace && scancode <= kSlash) || scancode == kSecondBackslash ||
           (scancode >= kPad1 && scancode <= kPad0) || scancode == kBackspace || scancode == kDelete;
}

[[nodiscard]] bool run_in_step() { return g_ls_mode || g_ls_replay_active || psx_netplay_active() != 0; }

/* The screen was left: the routine has not run for kLeftScreen VBlanks of the game's own clock. A VBlank plugin
 * cannot count them: the framework calls one for a mod package only, never for this game. */
[[nodiscard]] bool left_screen() {
    return !g_entered_once || psx_get_cycle_count() - g_entered >= kLeftScreen * kVBlankCycles;
}

/* Whether the name rows are up now, for the pad. The row is read as it is, not as the routine last found it: the
 * routine itself moves it, and a key can go down between that and its next entry. */
[[nodiscard]] bool taking_text() {
    if (left_screen() || run_in_step()) return false;
    const uint32_t row = psx_mod_read_word(kRow);
    return row >= kLettersRow && row <= kLastNameRow;
}

void forget() {
    g_count = 0;
    g_text = 0;
    g_held.reset();
}

void save_screen_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kSaveScreen) return;
    if (left_screen()) forget();
    g_entered = psx_get_cycle_count();
    g_entered_once = true;
    const uint32_t row = psx_mod_read_word(kRow);
    g_on = row >= kLettersRow && row <= kLastNameRow && !g_ls_mode && !g_ls_replay_active && !psx_netplay_active();
    g_letters = row == kLettersRow;
    if (!g_on) {
        forget();
        return;
    }
    if (g_held.any() && g_entered - g_keyed >= kLetGo * kVBlankCycles) g_held.reset();
    if (g_count == 0 && g_held.none()) return;
    if (g_text != 0 || g_held.any()) cpu->gpr[4] = 0;
    uint32_t cursor = psx_mod_read_word(kCursor);
    if (cursor > kLength) cursor = kLength;
    bool changed = false;
    for (std::size_t index = 0; index < g_count; ++index) {
        const Typed typed = g_typed[index];
        if (typed.act == Act::Done) {
            if (row != kLettersRow) continue;
            cursor = kLength;
            changed = true;
            continue;
        }
        if (typed.act == Act::Back ? cursor == 0 : cursor >= kLength) continue;
        if (typed.act == Act::Back) --cursor;
        psx_mod_write_byte(kName + cursor, typed.code);
        if (typed.act == Act::Put) ++cursor;
        changed = true;
    }
    g_count = 0;
    g_text = 0;
    if (!changed) return;
    psx_mod_write_word(kCursor, cursor);
    psx_mod_write_word(kRow, kLettersRow);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_name_entry) {
    psx_mod_register_function_entry_plugin("disruptor.name_entry", kSaveScreen, save_screen_entry);
}

}  // namespace

/* The keyboard's state as the pad may read it: `count` keys copied to `kept`, the withheld ones as up. A key held
 * while the name rows are left stays withheld, or the next screen would take it for a fresh press. A run in step
 * with another gets the keyboard whole. */
extern "C" void disruptor_name_entry_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count) {
    const bool taking = taking_text();
    if (run_in_step()) g_withheld.reset();
    for (int scancode = 0; scancode < count; ++scancode) {
        const auto key = static_cast<std::size_t>(scancode);
        if (key >= g_withheld.size()) {
            kept[scancode] = keys[scancode];
            continue;
        }
        if (keys[scancode] == 0) g_withheld.reset(key);
        else if (taking && in_typing_block(scancode)) g_withheld.set(key);
        kept[scancode] = g_withheld.test(key) ? uint8_t{0} : keys[scancode];
    }
}

/* A save state was loaded: what was typed, and where the screen stood, belong to the time before it. The clock
 * cannot tell: a state's clock may lie anywhere beside the one it replaces. */
extern "C" void disruptor_name_entry_state_loaded(void) {
    g_on = g_letters = g_entered_once = false;
    forget();
}

/* A key went down or up. `plain` is 0 with Ctrl, Alt or the system key held. Returns 1 when the key was taken for the name. */
extern "C" int disruptor_name_entry_key(int scancode, uint32_t keycode, int down, int plain) {
    if (left_screen()) {
        g_on = g_letters = false;
        forget();
    }
    if (scancode == kEnter || scancode == kPadEnter) {
        if (down && plain && g_letters && g_count < g_typed.size()) g_typed[g_count++] = Typed{Act::Done, kBlank};
        return 0;
    }
    const std::optional<Typed> typed = typed_of(scancode, keycode);
    if (!typed || !in_typing_block(scancode)) return 0;  /* text only from keys the pad never sees on these rows */
    g_keyed = psx_get_cycle_count();
    if (!down) {
        g_held.reset(static_cast<std::size_t>(scancode));
        return 0;
    }
    /* By the rows as the routine last found them, not as they are: the pad shows a key before its event comes, and a
     * key that confirmed the row before can have brought these rows up by now. */
    if (!g_on || !plain) return 0;
    g_held.set(static_cast<std::size_t>(scancode));
    if (g_count < g_typed.size()) {
        g_typed[g_count++] = *typed;
        ++g_text;
    }
    return 1;
}
