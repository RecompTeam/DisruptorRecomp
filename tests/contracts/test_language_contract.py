#!/usr/bin/env python3
"""Version and codegen contract for Disruptor's language packs."""

import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

# Each text routine whole: end, hash and delay-slot return. Two return the width drawn, the third the characters read, so it has no hook.
WIDTH = 0x00AB1022  # sub $v0, $a1, $t3
COUNT = 0x008D1022  # sub $v0, $a0, $t5
RETAIL_ROUTINES = {
    0x80044A10: (0x80044AE8, "b25ced31cce0a8a7da088e1ad787912c99b3d0b8ba7522fd21f9ac53235a2022", WIDTH),
    0x80044AE8: (0x80044BDC, "0310e98bf8cd4eea0d57e1b6505cfe848c788195f4e4394b196d146469fcb094", COUNT),
    0x80044BDC: (0x80044D18, "28928e38f6567ec7ab94c186892dd61b04e1eb1212e87b4208c1e4e85ddd8060", WIDTH),
}
ROUTINES = (0x80044A10, 0x80044BDC)
COUNTING_ROUTINE = 0x80044AE8
# The menu's string routine (string, x, y, colour), whole, and the instruction that takes x from $a1.
MENU_DRAW = 0x8001A46C
MENU_DRAW_END = 0x8001A6A8
MENU_DRAW_HASH = "fff16046098263f730ade1904d023480666e0f9dad1a90373788a2f2f0b286a4"
MENU_DRAW_TAKES_X = (0x8001A47C, 0x00A09821)  # move $s3, $a1
# The pause menu's line routine (list, string, x, y), whole, and the instruction that takes x from $a2.
STRIP_DRAW = 0x800429DC
STRIP_DRAW_END = 0x80042BB8
STRIP_DRAW_HASH = "6b5a00ff0109987f4309f88c39514959585b2fc74521a459c7f48d9b9077d0da"
STRIP_DRAW_TAKES_X = (0x800429F4, 0x00C0A021)  # move $s4, $a2
JR_RA = 0x03E00008
# The hint module's three hooked routines and the US calls for a hint, told apart by return address.
HINT_ROUTINES = (0x80011888, 0x8004D348, 0x8004D410)
HINT_INSTRUCTIONS = {
    0x80011888: 0x27BDFFE0,  # addiu $sp, $sp, -0x20: the read's first instruction
    0x80011890: 0x00C08021,  # move $s0, $a2: it takes its size from $a2
    0x8004D348: 0x27BDFFE0,  # LoadImage's first
    0x8004D350: 0x00808021,  # move $s0, $a0: it takes its rectangle from $a0
    0x8004D410: 0x27BDFFE0,  # MoveImage's first
    0x8004D418: 0x00808021,  # move $s0, $a0: its rectangle too
    0x8001FC60: 0x3C108007,  # lui $s0, 0x8007
    0x8001FC64: 0x261076DC,  # addiu $s0, $s0, 0x76dc
    0x8001FC68: 0x8E050000,  # lw $a1, ($s0): the menu piece is read to the start of that buffer, so a hint lands on it
    0x80043554: 0x0C004622,  # jal 0x80011888: the level is read after the hint's two moves
    0x80020904: 0x3C108007,  # lui $s0, 0x8007
    0x80020908: 0x261076DC,  # addiu $s0, $s0, 0x76dc
    0x8002090C: 0x8E050000,  # lw $a1, ($s0): the hint is read into the buffer this pointer names
    0x80015404: 0x3C128007,  # lui $s2, 0x8007
    0x80015408: 0x265276DC,  # addiu $s2, $s2, 0x76dc
    0x8001540C: 0x8E450000,  # lw $a1, ($s2): the same buffer
    0x80015400: 0x3C060007,  # lui $a2, 7
    0x80015420: 0x34C63800,  # ori $a2, $a2, 0x3800: takes a level's 0x73800 bytes
    0x80015424: 0x3C110008,  # lui $s1, 8
    0x80015428: 0x0C004622,  # jal 0x80011888
    0x8001542C: 0x00B12821,  # addu $a1, $a1, $s1: 0x80000 bytes in, so it has room for a hint twice as long
    0x80020920: 0x0C004622,  # jal 0x80011888: the hint is read
    0x80020924: 0x34065000,  # ori $a2, $zero, 0x5000
    0x8002092C: 0x27A40058,  # addiu $a0, $sp, 0x58: the rectangle it is staged in
    0x80020930: 0x340202C0,  # ori $v0, $zero, 0x2c0
    0x80020938: 0x340201E0,  # ori $v0, $zero, 0x1e0
    0x80020940: 0x34020140,  # ori $v0, $zero, 0x140
    0x80020948: 0x34020020,  # ori $v0, $zero, 0x20
    0x8002094C: 0x0C0134D2,  # jal 0x8004d348
    0x800434D8: 0x02802021,  # move $a0, $s4: the same rectangle on the loading screen
    0x800434E0: 0x340600A0,  # ori $a2, $zero, 0xa0: its row in the first frame buffer
    0x800434E4: 0x340202C0,
    0x800434EC: 0x340201E0,
    0x800434F4: 0x34020140,
    0x800434FC: 0x34020020,
    0x80043500: 0x0C013504,  # jal 0x8004d410
    0x80043508: 0x02802021,  # move $a0, $s4
    0x80043510: 0x0C013504,  # jal 0x8004d410
    0x80043514: 0x34060190,  # ori $a2, $zero, 0x190: its row in the second
}
LOAD_CHARACTER = 0x90820000  # lbu $v0, 0($a0)
# The US movie player's code the movie sound module relies on: $s5 cleared as a movie begins, the wait, the key-on test, the two words read.
MOVIE_SOUND_INSTRUCTIONS = {
    0x800483B8: 0x0000A821,  # move $s5, $zero: the sound is not keyed on when a movie begins
    0x800485BC: 0x0C012CF5,  # jal VSync
    0x800485C0: 0x00002021,  # move $a0, $zero: the wait after a frame's fetch, returning to 0x800485C4
    0x80048648: 0x32A200FF,  # andi $v0, $s5, 0xff
    0x8004864C: 0x14400004,  # bnez $v0: keyed on already, no key-on
    0x80048654: 0x0C012862,  # jal SpuSetKey
    0x8004865C: 0x34150001,  # li $s5, 1
    0x80048B10: 0x3C018007,  # lui $at, 0x8007
    0x80048B14: 0xAC2215B8,  # sw $v0, 0x15b8($at): the fetched frame's number
    0x800147D8: 0xAF9405A8,  # sw $s4, 0x5a8($gp): the movie playing, at 0x800716F4
    0x8004B414: 0x3C028006,  # lui $v0, 0x8006: VSync(-1) returns the VBlank count
    0x8004B418: 0x8C42B920,  # lw $v0, -0x46e0($v0): kept at 0x8005B920
}
# The briefing's wait: voice 21 keyed on, the count zeroed, raised by the loading screen's VSync callback, waited on for 540.
BRIEFING_INSTRUCTIONS = {
    0x80043518: 0x938305A4,  # lbu $v1, 0x5a4($gp): the loop mode, 3 in the attract demo
    0x8004351C: 0x34020003,  # li $v0, 3
    0x80043520: 0x1062003B,  # beq $v1, $v0, 0x80043610: the demo keys no briefing on and zeroes no count
    0x800437AC: 0x938305A4,  # lbu $v1, 0x5a4($gp)
    0x800437B0: 0x34020003,  # li $v0, 3
    0x800437B4: 0x10620006,  # beq $v1, $v0, 0x800437D0: and skips the wait only then
    0x80043528: 0x938203CC,  # lbu $v0, 0x3cc($gp): the mission picks its briefing, at 0x80071518
    0x80043600: 0x34040001,  # li $a0, 1
    0x80043604: 0x0C012862,  # jal SpuSetKey
    0x80043608: 0x3C050020,  # lui $a1, 0x20: voice 21
    0x8004360C: 0xAF8005B4,  # sw $zero, 0x5b4($gp): the count, at 0x80071700
    0x80043448: 0x3C048001,  # lui $a0, 0x8001
    0x8004344C: 0x24842238,  # addiu $a0, $a0, 0x2238: the loading screen's callback
    0x80043450: 0x0C012D96,  # jal VSyncCallback
    0x800122E0: 0x0C013504,  # jal MoveImage, returning to 0x800122E8
    0x80012310: 0x8F8205B4,  # lw $v0, 0x5b4($gp)
    0x80012318: 0x24420001,  # addiu $v0, $v0, 1
    0x8001231C: 0xAF8205B4,  # sw $v0, 0x5b4($gp)
    0x800437BC: 0x8F8205B4,  # lw $v0, 0x5b4($gp)
    0x800437C4: 0x2842021C,  # slti $v0, $v0, 540: signed, so a count below zero waits
    0x800437C8: 0x1440FFFC,  # bnez $v0, 0x800437BC
    0x800437E0: 0x0C012A21,  # jal SpuWrite: the music over the voice once the wait is over
    0x800437E8: 0x938303CC,  # lbu $v1, 0x3cc($gp)
}
BRIEFING_COUNT_STORES = (0x8001231C, 0x8004360C)
GP = 0x8007114C


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def writes_register(word: int, register: int) -> bool:
    """Whether an R3000 instruction can write a general register, by the opcode table: rd of SPECIAL, rt of the
    immediates (ADDI..LUI) and of the loads (LB..LWR), rt of MFC/CFC from a coprocessor, $ra of JAL and the link of
    REGIMM's BLTZAL and BGEZAL."""
    opcode, rs, rt, rd = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
    if opcode == 0x00:
        return rd == register
    if opcode == 0x01:
        return rt in (0x10, 0x11) and register == 31
    if opcode == 0x03:
        return register == 31
    if 0x08 <= opcode <= 0x0F or 0x20 <= opcode <= 0x26:
        return rt == register
    if opcode in (0x10, 0x11, 0x12, 0x13):
        return rs in (0x00, 0x02) and rt == register
    return False


cmake = read("CMakeLists.txt")
source = read("src/game/language/language.cpp")
rules = read("src/game/language/language_pack.h")
header = read("src/game/language/language.h")
hints = read("src/game/language/language_hints.cpp")

require(
    '/src/game/language/language.cpp"' in cmake
    and "    disruptor_unit_test(disruptor_language " in cmake
    and "    disruptor_contract(language RETAIL)\n" in cmake,
    "the language source and both of its tests must remain registered, the contract with its inputs required",
)
require(
    "constexpr std::array<uint32_t, 2> kTextRoutines = {0x80044A10u, 0x80044BDCu};" in source
    and "0x80044AE8u" not in source
    and "constexpr int kStringRegister = 4;" in source
    and '        if (!psx_mod_register_function_entry_plugin("disruptor.language.text", routine, text_entry))' in source
    and "    for (const uint32_t routine : {kMenuDraw, kStripDraw}) {\n"
    '        if (!psx_mod_register_function_entry_plugin("disruptor.language.layout", routine, place_entry))'
    in source
    and "constexpr uint32_t kMenuDraw = 0x8001A46Cu;" in source
    and "constexpr uint32_t kStripDraw = 0x800429DCu;" in source
    and "constexpr int kXRegister = 5;" in source
    and "constexpr int kStripXRegister = 6;" in source
    and "constexpr int kReturnRegister = 31;" in source
    and source.count("psx_mod_register_function_entry_plugin(") == 2,
    "the two routines that return a width must be hooked at their audited entries, with the string in $a0",
)
require(
    "    if (!cpu || !may_run()) return;\n"
    "    if (g_state == State::kUnread) read_pack();\n"
    "    if (g_state != State::kOn) return;\n"
    "    if (const uint32_t other = stand_in(cpu->gpr[kStringRegister], routine == kLargeFont)) cpu->gpr[kStringRegister] = other;"
    in source
    and "constexpr uint32_t kLargeFont = kTextRoutines[1];" in source
    and source.count("kLargeFont") == 2
    and "    if (large_font) {\n"
    "        std::string given;\n"
    "        for (const char letter : words) {\n"
    "            const std::string &sign = g_signs[static_cast<uint8_t>(letter)];\n"
    "            changed = changed || !sign.empty();\n"
    "            given += sign.empty() ? std::string(1, letter) : sign;"
    in source
    and "    if (!changed || words.empty() || words.size() > kLongestString) return 0u;" in source
    and source.count("g_signs") == 3
    and "    const auto place = g_places.find(cpu->gpr[kReturnRegister]);\n"
    "    if (place == g_places.end() || !called_from(place->first, routine)) return;\n"
    "    const int x = routine == kStripDraw ? kStripXRegister : kXRegister;\n"
    "    const auto [home_x, other_x] = place->second;\n"
    "    if (home_x == kAnyX) cpu->gpr[x] += static_cast<uint32_t>(other_x);\n"
    "    else if (static_cast<int32_t>(cpu->gpr[x]) == home_x) cpu->gpr[x] = static_cast<uint32_t>(other_x);"
    in source
    and "    return word == (0x0C000000u | (routine >> 2 & 0x03FFFFFFu));" in source
    and "constexpr int32_t kAnyX = -32768;" in rules
    and "using rules::kAnyX;" in source
    and source.count(
        "if (!cpu || !may_run()) return;\n    if (g_state == State::kUnread) read_pack();\n    if (g_state != State::kOn) return;\n"
    )
    == 2
    and source.count("cpu->gpr[") == 6
    and "    return psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0;"
    in source
    and "    if (g_state == State::kOn || !may_run()) return 0;\n    const int taken = take_pack(data, size);" in source
    and source.count("take_pack(") == 2,
    "a text hook may change $a0 alone, the menu hook $a1 alone and the pause menu's line hook $a2 alone, where the pack names a jal of "
    "that routine and either the US x is the one "
    "it expects or the place is a step, "
    "only with a pack taken, and neither they nor the loader may act under netplay, lockstep or a replay",
)
require(
    source.count("psx_mod_write_") == 2
    and source.count("write_guest(") == 2
    and "    write_guest(g_scratch, words);" in source
    and "    g_scratch = psx_mod_alloc_guest_memory(static_cast<uint32_t>(kLongestString) + 1u, 4u);" in source
    and source.count("    g_scratch = ") == 1,
    "words may be written to the module's own mod memory only",
)
require(
    "    if (!rules::readable(address)) return false;" in source
    and "    return address >= kRamFirst && address < kRamEnd - static_cast<uint32_t>(kLongestString);" in rules
    and "constexpr uint32_t kRamFirst = 0x80000000u;" in rules
    and "constexpr uint32_t kRamEnd = 0x80200000u;" in rules
    and "constexpr size_t kLongestString = 96;" in rules
    and source.count("psx_mod_read_") == 2
    and source.count("if (!rules::readable(") == 2
    and "    if (!rules::readable(at)) return false;\n    uint32_t word = 0;\n" in source,
    "guest memory may be read from main RAM only: a string through the one bounded reader, a place's call where it is bounded too",
)
require(
    "        if (!guest_string(address, text) || rules::hash_of(text) != hash || !rules::entry_fits(text, words, format)) return 0;"
    in source
    and "    for (const char byte : text) hash = (hash ^ static_cast<uint8_t>(byte)) * 16777619u;" in rules
    and "    uint32_t hash = 2166136261u;" in rules
    and "std::count(format.kinds.begin(), format.kinds.end(), 's') <= 1;" in rules
    and "    if (text.empty() || text.size() > kLongestString || words.empty() || words.size() > kLongestString) return false;\n"
    "    if (words.find('\\0') != std::string_view::npos) return false;\n"
    "    if (text.find('%') == std::string_view::npos) return !has_part(words, 0);\n"
    "    return parse_format(text, format) && !has_part(words, format.kinds.size());"
    in rules
    and all(name not in source for name in ("parse_format(", "has_part(", "kRamEnd", "0x34504C44"))
    and "        const bool text_part = fits->kinds[static_cast<size_t>(byte - 1)] == 's';" in source
    and "        many = !taken.empty();\n        taken = parts;\n        parts.resize(before);\n        if (many) return false;"
    in source
    and "        if (take_parts(format, text, parts, many)) fits = fits ? fits : &format;\n        else if (many) return false;"
    in source
    and "        if (format.kinds[part] == 'd' && !next.empty() && next.front() >= '0' && next.front() <= '9') return false;"
    in rules
    and "    if (!head || word_at(head) != rules::kMagic) return 0;" in source
    and "constexpr uint32_t kMagic = 0x34504C44u;" in rules
    and "    if (count == 0u || count > rules::kMostStrings) return 0;" in source
    and "    if (places > rules::kMostPlaces) return 0;" in source
    and "        if (!place) return 0;" in source
    and "    if (!last) return 0;" in source
    and "    if (signs > rules::kMostSigns || pack.left() != signs * 4u) return 0;" in source
    and "        const bool sized = sign[1] == 2u || (sign[1] == 1u && sign[3] == 0u);\n"
    "        if (!sized || !rules::sign_fits(sign[0], drawn) || !given[sign[0]].empty()) return 0;"
    in source
    and "constexpr uint32_t kMostSigns = 8u;" in rules
    and "    return written >= 'a' && written <= 'z' && !drawn.empty() && drawn.size() <= 2 && std::all_of(drawn.begin(), drawn.end(), drawable);"
    in rules
    and "        return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= kLastDrawn);"
    in rules
    and "constexpr uint8_t kLastDrawn = 0x90;" in rules
    and "        if (count > left_) return nullptr;" in source
    and source.count("data[") == 4
    and source.count("entry[") == 2,
    "a pack must be refused whole unless every string it names is in RAM with the hash the pack carries for it, "
    "by the rules the disc code writes it by",
)
require(
    '/src/game/language/language_hints.cpp"' in cmake
    and "    disruptor_unit_test(disruptor_language_hints " in cmake
    and "constexpr uint32_t kRead = 0x80011888u, kLoadImage = 0x8004D348u, kMoveImage = 0x8004D410u;" in hints
    and "constexpr uint32_t kReadReturn = 0x80020928u, kStageReturn = 0x80020954u;" in hints
    and "constexpr std::array<Move, 2> kMoves = {{{0x80043508u, 0xA0u}, {0x80043518u, 0x190u}}};" in hints
    and "constexpr std::array<uint16_t, 4> kStage = {0x2C0, 0x1E0, 0x140, 0x20};" in hints
    and "constexpr std::array<uint16_t, 4> kTallStage = {0x2C0, 0x1C0, 0x140, 0x40};" in hints
    and "constexpr uint32_t kRowsHigher = 16;" in hints
    and "constexpr int kRectRegister = 4;\nconstexpr int kSizeRegister = 6;\nconstexpr int kRowRegister = 6;\nconstexpr int kReturnRegister = 31;"
    in hints
    and "    return g_hooked && psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0 &&\n"
    "           disruptor_language_disc_tall_hints() != 0;"
    in hints
    and "    if (!cpu || cpu->gpr[kReturnRegister] != kReadReturn) return;\n"
    "    g_hint = Hint::kUntouched;\n"
    "    if (cpu->gpr[kSizeRegister] != disruptor::kHintBytes || !tall_hints() || tall_stage() == 0u) return;\n"
    "    cpu->gpr[kSizeRegister] = disruptor::kTallHintBytes;\n"
    "    g_hint = Hint::kReadTall;"
    in hints
    and "    if (!cpu || cpu->gpr[kReturnRegister] != kStageReturn) return;\n"
    "    const bool tall = g_hint == Hint::kReadTall && us_stage(cpu->gpr[kRectRegister]);\n"
    "    g_hint = tall ? Hint::kStagedTall : Hint::kUntouched;\n"
    "    if (tall) cpu->gpr[kRectRegister] = g_tall_stage;"
    in hints
    and "    if (!cpu || g_hint != Hint::kStagedTall) return;" in hints
    and "    if (move == kMoves.end() || cpu->gpr[kRowRegister] != move->row || !us_stage(cpu->gpr[kRectRegister])) return;"
    in hints
    and "    if (rect < kRamFirst || rect > kRamEnd - 2u * kStage.size() || rect % 2u != 0u) return false;" in hints
    and "    g_hooked = hook_all();" in hints
    and hints.count("!tall_hints()") == 1
    and hints.count("cpu->gpr[") == 11
    and hints.count("psx_mod_write_") == 1
    and "        psx_mod_write_half(g_tall_stage + 2u * static_cast<uint32_t>(field), kTallStage[field]);" in hints
    and hints.count("psx_mod_register_function_entry_plugin(") == 3
    and "int disruptor_language_disc_tall_hints(void);" in header,
    "the hint module may change the size of the hint's read, the rectangle of its LoadImage and the rectangle and row of its two "
    "MoveImage calls, each told by its return address and the US numbers, with all three hooks in place. It decides at the read, "
    "only for a disc with tall hints and never under netplay, lockstep or a replay, stages tall only what was read tall and moves "
    "tall only what was staged tall, and it may write to its own mod memory only",
)
for token in ("disruptor_language_load_pack", "disruptor_language_active"):
    require(token in header and token in source, f"language API is missing {token}")

movie_sound = read("src/game/language/language_movie_sound.cpp")
require(
    '/src/game/language/language_movie_sound.cpp"' in cmake
    and "    disruptor_unit_test(disruptor_language_movie_sound " in cmake
    and "constexpr uint32_t kFrameShown = 0x800485C4u;" in movie_sound
    and "constexpr uint32_t kFrameNumber = 0x800715B8u;" in movie_sound
    and "constexpr uint32_t kMovie = 0x800716F4u;" in movie_sound
    and "constexpr int kStarted = 21;" in movie_sound
    and '"disruptor.language.movie_sound", kVSync, vsync_entry' in movie_sound
    and "psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0" in movie_sound
    and "constexpr uint32_t kHeld = 2u;" in movie_sound
    and "    if (!active()) {\n        if (started == kHeld) started = 0u;\n    } else {\n" in movie_sound
    and "constexpr uint32_t kVBlanks = 0x8005B920u;" in movie_sound
    and "    if (g_holding && psx_mod_read_word(kVBlanks) - g_held_at > kMostVBlanks) g_holding = false;\n"
    in movie_sound
    and "    g_holding = started == kHeld;\n    g_held_at = psx_mod_read_word(kVBlanks);\n}\n" in movie_sound
    and "    disruptor_language_movie_sound_state_loaded();\n" in read("src/game/host.cpp")
    and "disruptor_language_movie_sound_save_deferred() ||\n" in read("src/game/host.cpp")
    and "int disruptor_language_disc_sound_start(uint32_t movie);" in header
    and "    if (g_state != State::kOn || movie >= disruptor::kMovies) return 0;\n    return g_disc.sound_starts()[movie];"
    in read("src/game/language/language_source.cpp"),
    "a PAL disc's movie sound waits for its start at the player's wait after each frame's fetch, by a value of $s5 the player "
    "never sets, a save waits until no hold of this run is left so a build without the module never loads one, never under "
    "netplay or lockstep, where a hold found is let go",
)

briefing = read("src/game/language/language_briefing.cpp")
require(
    '/src/game/language/language_briefing.cpp"' in cmake
    and "    disruptor_unit_test(disruptor_language_briefing " in cmake
    and "constexpr uint32_t kMoveImage = 0x8004D410u;" in briefing
    and "constexpr uint32_t kBeforeCount = 0x800122E8u;" in briefing
    and "constexpr uint32_t kCount = 0x80071700u;" in briefing
    and "constexpr uint32_t kMission = 0x80071518u;" in briefing
    and "constexpr int32_t kUsWait = 540;" in briefing
    and "constexpr int kBriefingVoice = 21;" in briefing
    and '"disruptor.language.briefing", kMoveImage, move_entry' in briefing
    and "psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0" in briefing
    and "    if (!cpu || cpu->gpr[kReturn] != kBeforeCount || !active()) return;\n" in briefing
    and "    if (wait < 0) return;\n" in briefing
    and "    if (count >= kUsWait) return count;\n"
    "    if (count < 0) return count == -1 ? kUsWait - 1 : count;\n"
    "    if (count + 1 >= wait) return kUsWait - 1;\n"
    "    return count == kUsWait - 1 ? kUsWait - 1 - wait : count;\n"
    in briefing
    and briefing.count("psx_mod_write_") == 1
    and "    if (left != count) psx_mod_write_word(kCount, static_cast<uint32_t>(left));\n" in briefing
    and "int disruptor_language_disc_briefing_wait(uint32_t mission);" in header
    and "    if (g_state != State::kOn || mission >= disruptor::kMissions) return -1;\n    return g_disc.briefing_waits()[mission];"
    in read("src/game/language/language_source.cpp")
    and GP + 0x5B4 == 0x80071700
    and GP + 0x3CC == 0x80071518,
    "a language disc's briefing plays as long as its own player waits, by the count the US loop waits on, set only right before "
    "the loading screen's callback raises it, never once the wait is over, never under netplay or lockstep",
)

for name in ("game.toml", "game-widescreen.toml"):
    entries = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", read(name))
    require(
        entries is not None
        and all(
            f'"0x{routine:08X}"' in entries.group(1) for routine in (*ROUTINES, MENU_DRAW, STRIP_DRAW, *HINT_ROUTINES)
        )
        and f'"0x{COUNTING_ROUTINE:08X}"' not in entries.group(1),
        f"{name} must emit an entry hook for the two width routines, the menu's, the pause menu's line routine and the hint's three, and "
        "none for the counting one",
    )

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    for first, (end, digest, returned) in RETAIL_ROUTINES.items():
        body = image[ROM_TEXT_OFFSET + first - LOAD_ADDRESS : ROM_TEXT_OFFSET + end - LOAD_ADDRESS]
        require(len(body) == end - first, f"retail text routine outside image: 0x{first:08X}")
        require(hashlib.sha256(body).hexdigest() == digest, f"retail text routine changed: 0x{first:08X}")
        words = struct.unpack(f"<{len(body) // 4}I", body)
        calls = [word for word in words if word >> 26 == 3 or (word >> 26 == 0 and word & 0x3F == 9)]
        require(
            not calls
            and words.count(JR_RA) == 1
            and words[-2:] == (JR_RA, returned)
            and words.count(LOAD_CHARACTER) == 1,
            f"the routine at 0x{first:08X} must call nothing, read its string through $a0 and return as audited",
        )
    require(
        all(RETAIL_ROUTINES[routine][2] == WIDTH for routine in ROUTINES)
        and RETAIL_ROUTINES[COUNTING_ROUTINE][2] == COUNT,
        "only routines that return a width may be hooked",
    )
    for address, instruction in HINT_INSTRUCTIONS.items():
        require(
            struct.unpack_from("<I", image, ROM_TEXT_OFFSET + address - LOAD_ADDRESS)[0] == instruction,
            f"the retail code the hint module relies on changed at 0x{address:08X}",
        )
    for address, instruction in MOVIE_SOUND_INSTRUCTIONS.items():
        require(
            struct.unpack_from("<I", image, ROM_TEXT_OFFSET + address - LOAD_ADDRESS)[0] == instruction,
            f"the retail code the movie sound module relies on changed at 0x{address:08X}",
        )
    for address, instruction in BRIEFING_INSTRUCTIONS.items():
        require(
            struct.unpack_from("<I", image, ROM_TEXT_OFFSET + address - LOAD_ADDRESS)[0] == instruction,
            f"the retail code the briefing module relies on changed at 0x{address:08X}",
        )
    text = struct.unpack_from(f"<{(len(image) - ROM_TEXT_OFFSET) // 4}I", image, ROM_TEXT_OFFSET)
    stores = [
        LOAD_ADDRESS + 4 * index
        for index, word in enumerate(text)
        if word >> 26 in (0x28, 0x29, 0x2A, 0x2B, 0x2E)
        and (word >> 21) & 31 == 28
        and word & 0xFFFF in range(0x5B4 - 3, 0x5B4 + 4)
    ]
    absolute = [
        LOAD_ADDRESS + 4 * index
        for index, word in enumerate(text)
        if word >> 26 in (0x28, 0x29, 0x2A, 0x2B, 0x2E) and word & 0xFFFF in range(0x1700 - 3, 0x1700 + 4)
    ]
    pointers = [
        LOAD_ADDRESS + 4 * index
        for index, word in enumerate(text)
        if word >> 26 in (0x08, 0x09, 0x0D)
        and (
            ((word >> 21) & 31 == 28 and word & 0xFFFF in range(0x5B4 - 3, 0x5B4 + 4))
            or word & 0xFFFF in range(0x1700 - 3, 0x1700 + 4)
        )
    ]
    require(
        tuple(stores) == BRIEFING_COUNT_STORES and not absolute and not pointers,
        "no store by $gp or with the count's low half but the key-on's and the callback's may reach the briefing's count, and no "
        "register may be made from $gp near it or with that low half (a write through another base shows only in a write trace)",
    )
    raise_words = struct.unpack_from("<14I", image, ROM_TEXT_OFFSET + 0x800122E8 - LOAD_ADDRESS)

    def leaves_before_raise(at: int, word: int) -> bool:
        opcode = word >> 26
        if opcode in (2, 3) or (opcode == 0 and word & 0x3F in (8, 9)):
            return True
        if opcode == 1 or 4 <= opcode <= 7:
            target = at + 4 + (struct.unpack("<h", struct.pack("<H", word & 0xFFFF))[0] << 2)
            return not 0x800122E8 <= target <= 0x80012310
        return False

    require(
        not any(
            word >> 26 in (0x28, 0x29, 0x2A, 0x2B, 0x2E) and word & 0xFFFF in range(0x5B4 - 3, 0x5B4 + 4)
            for word in raise_words[:-1]
        )
        and not any(leaves_before_raise(0x800122E8 + 4 * index, word) for index, word in enumerate(raise_words[:-1]))
        and raise_words[-1] == 0xAF8205B4,
        "between the callback's last MoveImage and its raise of the count nothing may store to the count, and no branch or jump may "
        "leave or pass the load the raise starts from",
    )
    gp_setups = [index for index in range(len(text) - 1) if text[index] == 0x3C1C8007 and text[index + 1] == 0x279C114C]
    require(
        len(gp_setups) == 1,
        "the executable must set $gp to 0x8007114C, which the briefing's count and mission are read by",
    )
    between = struct.unpack_from("<33I", image, ROM_TEXT_OFFSET + 0x800485C4 - LOAD_ADDRESS)
    require(
        not any(writes_register(word, 21) for word in between),
        "nothing between the wait and the key-on test may write $s5 itself",
    )
    menu = image[ROM_TEXT_OFFSET + MENU_DRAW - LOAD_ADDRESS : ROM_TEXT_OFFSET + MENU_DRAW_END - LOAD_ADDRESS]
    takes_x = struct.unpack_from("<I", image, ROM_TEXT_OFFSET + MENU_DRAW_TAKES_X[0] - LOAD_ADDRESS)[0]
    require(
        hashlib.sha256(menu).hexdigest() == MENU_DRAW_HASH and takes_x == MENU_DRAW_TAKES_X[1],
        "the menu's string routine must be the audited one and take its x from $a1",
    )
    lines = image[ROM_TEXT_OFFSET + STRIP_DRAW - LOAD_ADDRESS : ROM_TEXT_OFFSET + STRIP_DRAW_END - LOAD_ADDRESS]
    takes_line_x = struct.unpack_from("<I", image, ROM_TEXT_OFFSET + STRIP_DRAW_TAKES_X[0] - LOAD_ADDRESS)[0]
    require(
        hashlib.sha256(lines).hexdigest() == STRIP_DRAW_HASH and takes_line_x == STRIP_DRAW_TAKES_X[1],
        "the pause menu's line routine must be the audited one and take its x from $a2",
    )


def hook_at_entry(own: str, routine: int) -> bool:
    """Whether a generated routine runs its entry hook once, before its first block and with nothing between."""
    hook = f"psx_mod_function_entry(cpu, 0x{routine:08X}u);"
    first_block = re.search(r"^block_[0-9A-F]{8}:", own, re.MULTILINE)
    if own.count(hook) != 1 or first_block is None or first_block.group(0) != f"block_{routine:08X}:":
        return False
    between = own[own.index(hook) + len(hook) : first_block.start()] if own.index(hook) < first_block.start() else "x"
    return re.fullmatch(r"(\s|/\*.*?\*/)*", between, re.DOTALL) is not None


generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    text = "".join(path.read_text(encoding="utf-8") for path in generated)
    for routine in (*ROUTINES, MENU_DRAW, STRIP_DRAW, *HINT_ROUTINES):
        hook = f"psx_mod_function_entry(cpu, 0x{routine:08X}u);"
        start = text.find(f"\nvoid func_{routine:08X}(CPUState* cpu)\n{{")
        own = text[start : text.find("\nvoid func_", start + 1)] if start >= 0 else ""
        require(
            text.count(hook) == 1 and hook_at_entry(own, routine),
            f"generated guest code must run exactly one entry hook at 0x{routine:08X}, in that routine, right before its first block",
        )
        label = f"block_{routine:08X}:"
        late = own.replace(hook, "", 1).replace(label, f"{label}\n    {hook}", 1)
        busy = own.replace(hook, f"{hook}\n    cpu->gpr[4] = 0;", 1)
        require(
            hook in late
            and not hook_at_entry(late, routine)
            and not hook_at_entry(busy, routine)
            and not hook_at_entry(own + hook, routine),
            "the check of a hook's place must refuse a hook inside a block, one with a statement before the first block, and two",
        )
    require(
        f"psx_mod_function_entry(cpu, 0x{COUNTING_ROUTINE:08X}u);" not in text,
        "the routine that returns a character count must stay without a hook",
    )

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("generated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the generated code: {checked}",
)
print(f"Disruptor language source/codegen contract: PASS ({checked})")
