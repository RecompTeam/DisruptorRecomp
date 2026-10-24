#!/usr/bin/env python3
"""Version and source contract for the reactor's explosion and the HUD message at their stock pace under 60 FPS gameplay."""

import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXT_OFFSET = 0x800

# The effects' chain and the explosion's step, their two callers, the reactor's end, the teleport, the death test, the
# message's draw and its caller, the tick count, the count's and a pickup's message writes, and the sound manager's
# call, its busy test, its cut at 241 and its step of a slot's age.
RETAIL_INSTRUCTIONS = {
    0x8003FE70: 0x90637636,
    0x8003FF70: 0x90637634,
    0x8003FFB8: 0x90637637,
    0x80040004: 0x93830310,
    0x80040008: 0x34020004,
    0x80040020: 0x90637647,
    0x8004002C: 0x2C62001E,
    0x80040050: 0x26317649,
    0x80040054: 0x92230000,
    0x800400DC: 0x92220001,
    0x800401C8: 0x9223FFF2,
    0x8004021C: 0xA222FFF2,
    0x800410F0: 0x0C00FF9B,
    0x8004217C: 0x0C00FF9B,
    0x80039C80: 0x2463763B,
    0x80039C90: 0x34020030,
    0x80039C94: 0xA0620000,
    0x8003A038: 0x8C42765C,
    0x8003A05C: 0xA0227637,
    0x800444AC: 0x9063763B,
    0x800427D0: 0x8F8202CC,
    0x800427EC: 0x2442FFFF,
    0x800427F0: 0xAF8202CC,
    0x80042800: 0x261076D8,
    0x8004281C: 0x24847694,
    0x80043C9C: 0x0C0109F4,
    0x80039B6C: 0xAF8205AC,
    0x80039C38: 0x24847694,
    0x80039C64: 0xAF8202CC,
    0x80034428: 0x24A502E0,
    0x80034438: 0xAF8202CC,
    0x80043AD0: 0x0C004D51,
    0x80014220: 0x26733DE4,
    0x800144A8: 0x92620000,
    0x800142A4: 0x8C223DDC,
    0x800142AC: 0x284200F1,
    0x800144C0: 0x8C223DDC,
    0x800144C8: 0x24420001,
    0x800144D4: 0xAC223DDC,
    0x800144E0: 0x2A220016,
    0x800144E8: 0x26100018,
}
RETAIL_CODE = {
    (0x8003FE6C, 0x80040238): "208be7a2af46196893c1cf3d56b37ad5be4f3e5e256f7c2f986df9deb72ae323",
    (0x800427D0, 0x80042830): "e491af2f28c8b10dc29a7b24f0fbb84c07542eed2e41f11a2599fde0c56650e9",
}
HOOKED = (0x8003FE6C, 0x800427D0, 0x8004B3D4, 0x80013544)


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
frames = read("src/game/frame/stock_frames.h")
hooks = read("src/game/frame/stock_frames.cpp")
host = read("src/game/host.cpp")

require(
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/frame/stock_frames.cpp"\n' in cmake
    and "    disruptor_unit_test(disruptor_stock_frames tests/unit/disruptor_stock_frames_test.cpp)\n" in cmake
    and "    disruptor_contract(stock_frames RETAIL)\n" in cmake,
    "the stock frames source, its unit test and this contract with its input must stay registered",
)
for name in ("game.toml", "game-widescreen.toml"):
    listed = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", read(name))
    require(
        listed is not None and all(f'"0x{address:08X}"' in listed[1] for address in HOOKED),
        f"{name} must emit the entries the stock frames hook",
    )
require(
    "constexpr uint32_t kEffects = 0x8003FE6Cu;" in hooks
    and "constexpr uint32_t kMessageDraw = 0x800427D0u;" in hooks
    and "constexpr uint32_t kFrameWait = 0x80043E84u;" in hooks
    and '"disruptor.stock_frames.effects", kEffects, effects' in hooks
    and '"disruptor.stock_frames.message", kMessageDraw, message' in hooks
    and '"disruptor.stock_frames.sounds", kSounds, sounds' in hooks
    and "constexpr uint32_t kSounds = 0x80013544u;" in hooks
    and "constexpr uint32_t kSoundsSite = 0x80043AD8u;" in hooks
    and "    if (!cpu || cpu->gpr[31] != kSoundsSite || !wanted()) return;\n    Guest guest;\n"
    "    sounds_begin(guest, g_frames, cpu->gpr[31]);\n" in hooks
    and '"disruptor.stock_frames.tick", kTick, tick_begins' in hooks
    and '"disruptor.stock_frames.vsync", kVSync, frame_waits' in hooks
    and "constexpr uint32_t kTick = 0x80039450u;" in hooks
    and "bool wanted() { return !g_off && disruptor_frame_rate_gameplay_unlocked() && !psx_netplay_active(); }" in hooks
    and "    if (!cpu || !wanted()) return;\n    Guest guest;\n    effects_begin(guest, g_frames, cpu->gpr[31]);\n"
    in hooks
    and "    if (cpu) message_begins(guest, g_frames, cpu->gpr[31]);\n" in hooks
    and "    frame_over(guest, g_frames, true);\n" in hooks
    and "    frame_over(guest, g_frames, false);\n" in hooks
    and "    return g_unlocked.load(std::memory_order_relaxed) && simulation_follows_elapsed_vblanks() ? 1 : 0;\n"
    in read("src/game/frame/frame_rate.cpp"),
    "the explosion's and the message's hooks stand at their routines' entries, act once a call site a frame, and only while"
    " the loop runs a tick a VBlank under 60 FPS outside a network game",
)
require(
    "inline constexpr uint32_t kExplosion = 0x8007763Bu;" in frames
    and "inline constexpr uint32_t kFirstEffects[] = {0x80077636u, 0x80077634u, 0x80077637u};" in frames
    and "inline constexpr uint32_t kShortEffect = 0x80077647u;" in frames
    and "inline constexpr uint32_t kLastEffects[] = {0x80077649u, 0x8007764Au};" in frames
    and "inline constexpr uint32_t kPlayerState = 0x8007145Cu;" in frames
    and "inline constexpr uint32_t kMessage = 0x80077694u;" in frames
    and "inline constexpr uint32_t kMessageBytes = 68u;" in frames
    and "inline constexpr uint32_t kMessageTime = 0x80071418u;" in frames
    and "inline constexpr uint32_t kTicks = 0x800716F8u;" in frames
    and "inline constexpr uint32_t kSoundSlots = 0x80073DD8u;" in frames
    and "inline constexpr uint32_t kSoundSlotCount = 22u, kSoundSlotBytes = 24u;" in frames
    and "inline constexpr uint32_t kSoundAge = 4u;" in frames,
    "the bytes the effects try before the explosion, the message, the tick count and the sound slots are the retail ones"
    " pinned below",
)
require(
    "void psx_game_state_loaded(void) {" in host
    and "    disruptor_mover_motion_state_loaded();\n    disruptor_stock_frames_state_loaded();\n" in host
    and "    disruptor_mover_motion_state_saving();\n    disruptor_stock_frames_state_saving();\n" in host
    and 'extern "C" void disruptor_stock_frames_state_saving(void) {\n    Guest guest;\n    state_saving(guest, g_frames);\n}\n'
    in hooks
    and 'extern "C" void disruptor_stock_frames_state_loaded(void) {\n    const Guest guest;\n    loaded(guest, g_frames);\n}\n'
    in hooks
    and "           disruptor_stock_frames_save_deferred() || disruptor_list_message_save_deferred();\n}\n" in host
    and "    return saving_deferred(guest, g_frames) ? 1 : 0;\n" in hooks,
    "no state is written from a hook's change to the frame's wait, a message shown in place is given back before one,"
    " and a loaded state forgets the cadence",
)

image_path = ROOT / "input" / "SLUS_002.24"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    "the build gate must check the retail image: it is absent",
)
if not image_path.exists():
    print("Disruptor stock frames source contract: PASS (retail image absent)")
    sys.exit(0)
image = image_path.read_bytes()
load = struct.unpack_from("<I", image, 0x18)[0]
for address, instruction in RETAIL_INSTRUCTIONS.items():
    require(
        struct.unpack_from("<I", image, TEXT_OFFSET + address - load)[0] == instruction,
        f"the retail instruction at 0x{address:08X} is not 0x{instruction:08X}",
    )
for (first, last), digest in RETAIL_CODE.items():
    require(
        hashlib.sha256(image[TEXT_OFFSET + first - load : TEXT_OFFSET + last - load]).hexdigest() == digest,
        f"the retail code from 0x{first:08X} to 0x{last:08X} is not the code the stock frames were written against",
    )

print("Disruptor stock frames source and version contract: PASS")
