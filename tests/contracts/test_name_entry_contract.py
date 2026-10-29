#!/usr/bin/env python3
"""Version and source contract for typing a save's name on the keyboard."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800
ROUTINE = 0x8001C484
RETURN = 0x8001D3D0
CALL = 0x0C007121
PRESSED = 21

# Routine entry and argument copy into $s5, row and letter reads, the cursor held at 8, the row set to 6 from DONE, the wrapping blank.
RETAIL_INSTRUCTIONS = {
    0x8001C484: 0x27BDFFA8,
    0x8001C49C: 0x0080A821,
    0x80020328: CALL,
    0x8001CFE8: 0x8F87033C,
    0x8001CFF0: 0x28E20005,
    0x8001D02C: 0x9022175C,
    0x8001D038: 0x90266F08,
    0x8001D050: 0x8F870538,
    0x8001D158: 0x34020006,
    0x8001D15C: 0xAF82033C,
    0x8001D19C: 0xAF820538,
    0x8001D1C8: 0x28420009,
    0x8001D1D0: 0x34020008,
    0x8001D1D4: 0xAF820538,
    0x8001D2A4: 0x34030024,
}
ENTRY_LIST = (
    'mod_function_entry_funcs = ["0x80011888", "0x80011944", "0x80013544", "0x8001A46C", "0x8001C044", "0x8001C484", "0x80020DD8", "0x80032514", "0x80039450", "0x8003B78C", "0x8003FE6C", "0x80040E68", "0x800427D0", '
    '"0x800429DC", "0x80044A10", "0x80044BDC", "0x80046134", "0x8004B3D4", "0x8004D348", "0x8004D410", "0x8004D684"]'
)
LOADS = range(0x20, 0x27)
NO_SPECIAL_TARGET = {0x08, 0x0C, 0x0D, 0x11, 0x13, 0x18, 0x19, 0x1A, 0x1B}


def written(word: int) -> int | None:
    """The general register an instruction writes, from the R3000's opcode table."""
    operation, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
    if operation == 0x00:
        return None if word & 0x3F in NO_SPECIAL_TARGET else rd
    if operation == 0x01:
        return 31 if rt & 0x10 else None
    if operation == 0x03:
        return 31
    if 0x08 <= operation <= 0x0F or operation in LOADS:
        return rt
    if operation in (0x10, 0x12) and rs in (0x00, 0x02):
        return rt
    return None


def faults(words: list[int]) -> list[str]:
    """What in the save screen's routine would let a press through that the module took away, or take one from another routine."""

    def at(address: int) -> int:
        return words[(address - LOAD_ADDRESS) // 4]

    found = []
    if [LOAD_ADDRESS + 4 * index for index, word in enumerate(words) if word == CALL] != [0x80020328]:
        found.append("the routine is called from elsewhere than the menu's one place")
    writers = {address for address in range(ROUTINE, RETURN, 4) if written(at(address)) == PRESSED}
    if writers != {0x8001C49C, 0x8001CBA0, 0x8001D3B4} or at(0x8001CBA0) != 0x0000A821 or at(0x8001D3B4) != 0x8FB5004C:
        found.append(
            f"the pressed buttons are written at {sorted(map(hex, writers))} and not only copied from the argument, cleared and restored"
        )
    early = [
        address
        for address in range(ROUTINE, 0x8001C49C, 4)
        if at(address) >> 21 & 31 == 4
        or (at(address) >> 26 == 0x00 and at(address) >> 16 & 31 == 4)
        or written(at(address)) == 4
    ]
    if early:
        found.append(f"the argument is used or changed before it is copied, at {sorted(map(hex, early))}")
    return found


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
module = read("src/game/input/name_entry.cpp")
host = read("psxrecomp/runtime/src/main.cpp")
game_side = read("src/game/host.cpp")

require(
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/name_entry.cpp"\n' in cmake
    and "    PSX_HAS_GAME_HOST=1" in cmake
    and '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/host.cpp"\n' in cmake
    and "    disruptor_unit_test(disruptor_name_entry " in cmake
    and "    disruptor_contract(name_entry RETAIL)\n" in cmake,
    "the module, its switch for the host, its unit test and this contract with its input must stay registered",
)
require(
    all(read(name).count(ENTRY_LIST) == 1 for name in ("game.toml", "game-widescreen.toml")),
    "both game configs must emit an entry hook for the save screen's routine",
)
require(
    "constexpr uint32_t kSaveScreen = 0x8001C484u;" in module
    and "constexpr uint32_t kRow = 0x80071488u;" in module
    and "constexpr uint32_t kCursor = 0x80071684u;" in module
    and "constexpr uint32_t kName = 0x8007175Cu;" in module
    and "constexpr uint32_t kLettersRow = 5u, kLastNameRow = 7u;" in module
    and "constexpr uint32_t kLength = 8u;" in module
    and "constexpr uint8_t kDigits = 26u, kBlank = 36u;" in module
    and '    psx_mod_register_function_entry_plugin("disruptor.name_entry", kSaveScreen, save_screen_entry);\n'
    in module,
    "the module must name the routine, the row, the cursor and the name as the game has them, and hook the routine",
)
require(
    "    g_on = row >= kLettersRow && row <= kLastNameRow && !g_ls_mode && !g_ls_replay_active && !psx_netplay_active();\n"
    in module
    and "    if (g_count == 0 && g_held.none()) return;\n    if (g_text != 0 || g_held.any()) cpu->gpr[4] = 0;\n"
    in module
    and module.count("cpu->gpr[") == 1
    and module.count("psx_mod_write_") == 3
    and "        psx_mod_write_byte(kName + cursor, typed.code);\n" in module
    and "    psx_mod_write_word(kCursor, cursor);\n    psx_mod_write_word(kRow, kLettersRow);\n" in module
    and "    if (!g_on || !plain) return 0;\n" in module
    and "    if (!typed || !in_typing_block(scancode)) return 0;" in module
    and "    if (left_screen() || run_in_step()) return false;\n"
    "    const uint32_t row = psx_mod_read_word(kRow);\n"
    "    return row >= kLettersRow && row <= kLastNameRow;\n"
    in module
    and "[[nodiscard]] bool run_in_step() { return g_ls_mode || g_ls_replay_active || psx_netplay_active() != 0; }\n"
    in module,
    "keys are text only from the typing block, on the name rows as the routine last found them, and outside "
    "lockstep and net games, "
    "the frame's presses are the one register changed, "
    "and the name, the cursor and the row the only memory written",
)
require(
    '#ifdef PSX_HAS_GAME_HOST\n#include "game_host.h"\n#endif\n' in host
    and "int psx_game_key(int scancode, uint32_t keycode, int down, int plain) {\n"
    "    return disruptor_name_entry_key(scancode, keycode, down, plain);\n}\n"
    in game_side
    and "#ifdef PSX_HAS_GAME_HOST\n            } else if (ev.type == SDL_KEYUP) {\n#if defined(PSX_SDL3)\n"
    "                (void)psx_game_key((int)ev.key.scancode, (uint32_t)ev.key.key, 0, 1);\n#else\n"
    "                (void)psx_game_key((int)ev.key.keysym.scancode, (uint32_t)ev.key.keysym.sym, 0, 1);\n"
    "#endif\n#endif\n            } else if (!ui_consumed && ev.type == SDL_KEYDOWN) {\n"
    in host
    and "#ifdef PSX_HAS_GAME_HOST\n"
    "                if (psx_game_key((int)scancode, (uint32_t)key, 1, !(mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI))))\n"
    "                    continue; /* the game took the key */\n"
    in host
    and host.count("psx_game_key(") == 3,
    "the host must hand every key it did not use itself to the module, a release whoever used the press, and leave a taken key alone",
)
keydown = host.index("            } else if (!ui_consumed && ev.type == SDL_KEYDOWN) {\n")
require(
    keydown
    < host.index("if (psx_game_key((int)scancode")
    < host.index("if (key >= SDLK_F1 && key <= SDLK_F12) {", keydown)
    and host.index('netplay_soft_exit("netplay_escape");', keydown) < host.index("if (psx_game_key((int)scancode"),
    "a taken key must be taken before the host's own keys are looked at, and after a net game's Escape",
)
sources = [found for found in (ROOT / "src").rglob("*") if found.suffix in {".cpp", ".h"}]
require(
    "    return !g_entered_once || psx_get_cycle_count() - g_entered >= kLeftScreen * kVBlankCycles;\n" in module
    and "    if (left_screen()) forget();\n    g_entered = psx_get_cycle_count();\n    g_entered_once = true;\n"
    in module
    and "    if (left_screen()) {\n        g_on = g_letters = false;\n        forget();\n    }\n" in module
    and "    if (g_held.any() && g_entered - g_keyed >= kLetGo * kVBlankCycles) g_held.reset();\n" in module
    and "    g_keyed = psx_get_cycle_count();\n" in module
    and "constexpr uint64_t kVBlankCycles = 564480u;" in module
    and "#define VBLANK_CYCLES   564480u" in read("psxrecomp/runtime/src/interrupts.c")
    and "constexpr unsigned kLeftScreen = 8u;" in module
    and "constexpr unsigned kLetGo = 120u;" in module
    and 'extern "C" void disruptor_name_entry_state_loaded(void) {\n'
    "    g_on = g_letters = g_entered_once = false;\n    forget();\n}\n"
    in module
    and "    disruptor_capsule_state_loaded();\n    disruptor_selection_wheel_cancel();\n"
    "    disruptor_name_entry_state_loaded();\n"
    in game_side,
    "the screen counts as left, and a held key as let go, by the game's own clock in the framework's VBlanks, "
    "and a loaded save state, whose clock may lie anywhere, resets it all",
)
require(
    "    for (const ModResolution::Plugin& plugin : s.plan.plugins)\n        mod_invoke_vblank_plugin(plugin.id);\n"
    in read("psxrecomp/runtime/src/mod_runtime.cpp")
    and not [
        found.name
        for found in sources
        if "psx_mod_register_vblank_plugin" in found.read_text(encoding="utf-8")
        or "psx_mod_register_activation_plugin" in found.read_text(encoding="utf-8")
    ],
    "the framework calls a VBlank or an activation plugin for a mod package only, never for the game itself: "
    "no source of the game may count on one",
)
controls = read("src/game/input/mouse_aim.cpp")
require(
    "static const Uint8* pad_keys(void) {\n"
    "    int count = 0;\n"
    "    const Uint8* keys = SDL_GetKeyboardState(&count);\n"
    "#ifdef PSX_HAS_GAME_HOST\n"
    "    static Uint8 kept[512];\n"
    "    if (!keys || count <= 0 || count > (int)sizeof(kept)) return keys;\n"
    "    psx_game_keys_for_pad(keys, kept, count);\n"
    "    return kept;\n"
    in host
    and host.count("const Uint8* keys = pad_keys();\n") == 4
    and all(
        host.count(f"{reader}(keys, ") == uses
        for reader, uses in (("psx_keybinds_pad_word", 1), ("psx_keybinds_sticks", 2), ("psx_keybinds_dpad_active", 1))
    )
    and "void psx_game_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count) {\n"
    "    disruptor_name_entry_keys_for_pad(keys, kept, count);\n}\n"
    in game_side,
    "every reading of the keyboard for a pad must go through the game, which holds back the keys it is typing with",
)
require(
    "        disruptor_name_entry_keys_for_pad(keys, pad_keys.data(), static_cast<int>(pad_keys.size()));\n"
    "        keys = pad_keys.data();\n"
    in controls
    and controls.index("disruptor_name_entry_keys_for_pad(keys, pad_keys.data()")
    < controls.index("        if (key_down(keys, arrow) && !key_bound(arrow)")
    and "    const bool taking = taking_text();\n    if (run_in_step()) g_withheld.reset();\n" in module
    and "        if (keys[scancode] == 0) g_withheld.reset(key);\n"
    "        else if (taking && in_typing_block(scancode)) g_withheld.set(key);\n"
    "        kept[scancode] = g_withheld.test(key) ? uint8_t{0} : keys[scancode];\n"
    in module
    and module.count("g_withheld.reset(") == 3
    and "    g_held.set(static_cast<std::size_t>(scancode));\n    g_withheld.set(static_cast<std::size_t>(scancode));"
    in module
    and "        g_held.reset(static_cast<std::size_t>(scancode));\n        g_withheld.reset(static_cast<std::size_t>(scancode));\n        return 0;"
    in module
    and "    return (scancode >= kKeyA && scancode <= kKey0) || (scancode >= kSpace && scancode <= kSlash) || "
    "scancode == kSecondBackslash ||\n"
    "           (scancode >= kPad1 && scancode <= kPad0) || scancode == kBackspace || scancode == kDelete;\n"
    in module
    and "kEnter = 40, kPadEnter = 88, kSlash = 56, kSecondBackslash = 100;\n" in module,
    "on the name rows a key of the typing block is no button, in the modern controls as well, from the moment the "
    "keyboard shows it down, which can be a frame before its event, until it is let go and not merely until the rows "
    "are left, and a run in step with another gets the keyboard whole",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    retail = list(struct.unpack_from(f"<{(len(image) - ROM_TEXT_OFFSET) // 4}I", image, ROM_TEXT_OFFSET))

    def changed(address: int, word: int) -> list[int]:
        copy = list(retail)
        copy[(address - LOAD_ADDRESS) // 4] = word
        return copy

    for address, instruction in RETAIL_INSTRUCTIONS.items():
        require(
            retail[(address - LOAD_ADDRESS) // 4] == instruction, f"retail save screen code changed at 0x{address:08X}"
        )
    require(not faults(retail), f"the retail save screen is not what the module takes it for: {faults(retail)}")
    seeded = {
        "a second caller of the routine": (0x80020330, CALL, "called from elsewhere"),
        "the pressed buttons loaded again inside the routine": (0x8001C4D8, 0x8F950538, "pressed buttons are written"),
        "the routine no longer clearing them where it does": (0x8001CBA0, 0x00000000, "pressed buttons are written"),
        "the argument changed before its copy": (0x8001C494, 0x24840001, "before it is copied"),
        "the argument read before its copy": (0x8001C494, 0x8C820000, "before it is copied"),
    }
    for name, (address, word, complaint) in seeded.items():
        said = faults(changed(address, word))
        require(any(complaint in fault for fault in said), f"the scan must refuse {name}, it said {said}")

checked = f"retail image {'checked' if image_path.exists() else 'absent'}"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    f"the build gate must check the retail image: {checked}",
)
print(f"Disruptor name entry source contract: PASS ({checked})")
