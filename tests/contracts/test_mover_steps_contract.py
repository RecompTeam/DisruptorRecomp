#!/usr/bin/env python3
"""Version and source contract for the half steps of doors, lifts, enemies and shots under 60 FPS gameplay."""

import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXT_OFFSET = 0x800

# Tick increment, odd-tick mover skip, two-tick step, mover and sector renderer calls, the mover's list and count loads.
RETAIL_INSTRUCTIONS = {
    0x80039B54: 0x8F8205AC,
    0x80039B68: 0x24420001,
    0x80039B6C: 0xAF8205AC,
    0x80039D28: 0x8F8205AC,
    0x80039D30: 0x30420001,
    0x80039D34: 0x14400010,
    0x80039D38: 0x34020002,
    0x80039D3C: 0xAF8203DC,
    0x80039D50: 0x0C008F4B,
    0x80041198: 0x8C440010,
    0x8004119C: 0x0C01184D,
    0x800411A4: 0x0C00EDE3,
    0x8003A46C: 0x0C01184D,
    0x8002400C: 0x0C0115E9,
    0x80039D7C: 0x90847630,
    0x80023D2C: 0x8F82034C,
    0x80023D68: 0x8F8302D8,
    # A lift: routine, sector index, draw offset, carry test, height writes, the frame loop's draw and waits.
    0x800236A8: 0x8F8202BC,
    0x800236E4: 0x8F8305F0,
    0x8003A40C: 0x9043002E,
    0x8003A428: 0x8C6375D0,
    0x8003A42C: 0x8C440014,
    0x8003A43C: 0x00641823,
    0x8003A468: 0x8C440004,
    0x8003A0B4: 0x8C6375C4,
    0x8003A0C4: 0xAC2375D0,
    0x80043B70: 0x0C010399,
    0x80040E64: 0x8F84056C,
    0x80040E68: 0x3C028007,
    0x80043B90: 0x1040000D,
    0x80043BA0: 0x0C012CF5,
    0x80043E6C: 0x10400003,
    0x80043E74: 0x0C00F543,
    0x80043E7C: 0x0C012CF5,
    0x800236DC: 0x3C158007,
    0x800236E0: 0x26B575C4,
    0x800238E8: 0x8E020008,
    0x800238EC: 0x9204001B,
    0x800238F4: 0x10800013,
    0x800238FC: 0x8EA40000,
    0x80023900: 0x8EA50044,
    0x80023908: 0x00851023,
    0x8002390C: 0x2442FFDC,
    0x80023910: 0x0043102A,
    0x80023914: 0x1040000B,
    0x8002392C: 0x8E020008,
    0x80023934: 0x00451021,
    0x8002393C: 0xAEA20000,
    0x80023940: 0xAEA3000C,
    # Shots and actors: the world loop's draws of a sector's shots and actors, their lists, coordinates and links.
    0x800411B4: 0x0C00F000,
    0x800411BC: 0x0C00F2CE,
    0x8003C034: 0x3C018007,
    0x8003C03C: 0x8C337BF4,
    0x8003C8B4: 0x8E640004,
    0x8003C8B8: 0x8E650008,
    0x8003C8BC: 0x8E66000C,
    0x8003CAF4: 0x8E730000,
    0x8003CB6C: 0x3C018008,
    0x8003CB74: 0x8C358004,
    0x8003CCCC: 0x96A40016,
    0x8003CCEC: 0x96A40018,
    0x8003CD44: 0x96A5001A,
    0x8003D4C8: 0x8EB50000,
    0x80026C64: 0x90620038,
    0x80026C8C: 0xAC238004,
}
# The mover routine, the vertex mover, and the sector renderer up to its vertex loop.
RETAIL_CODE = {
    (0x80023D2C, 0x80024058): "d27ec7902c7cd5e5470997e69c84a25465e2326c7330a965ff01ea4664c549aa",
    (0x800457A4, 0x8004596C): "3618e40cd62514ef2bb80f27aadc91afefe119c0288e3f5147ad758e363c5a0a",
    (0x80046134, 0x80046250): "9a28aaf92b94355b2c4076d2c7979949d1ec717fd4dfecafd64b9d7e1f4b0439",
}
HOOKED = (0x80039450, 0x8003B78C, 0x80040E68, 0x80046134, 0x8004B3D4)


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
steps = read("src/game/render/mover_steps.h")
objects = read("src/game/render/object_steps.h")
motion = read("src/game/render/mover_motion.cpp")
guest = read("src/game/guest.h")
host = read("src/game/host.cpp")

require(
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/mover_motion.cpp"\n' in cmake
    and "    disruptor_unit_test(disruptor_mover_steps tests/unit/disruptor_mover_steps_test.cpp)\n" in cmake
    and "    disruptor_unit_test(disruptor_object_steps tests/unit/disruptor_object_steps_test.cpp)\n" in cmake
    and "    disruptor_contract(mover_steps RETAIL)\n" in cmake,
    "the mover source, its unit test and this contract with its input must stay registered",
)
for name in ("game.toml", "game-widescreen.toml"):
    listed = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", read(name))
    require(
        listed is not None and all(f'"0x{address:08X}"' in listed[1] for address in HOOKED),
        f"{name}: the tick, the sector renderer, the routine drawn after a sector and the wait for the display must call the entry hook",
    )
require(
    "constexpr uint32_t kDrawSector = 0x80046134u;\n" in motion
    and "constexpr uint32_t kAfterSector = 0x8003B78Cu;\n" in motion
    and "constexpr uint32_t kTick = 0x80039450u;\n" in motion
    and "constexpr uint32_t kVSync = 0x8004B3D4u;" in motion
    and "constexpr uint32_t kTicks = 0x800716F8u;" in motion
    and "constexpr uint32_t kMoverList = 0x80071424u;" in motion
    and "constexpr uint32_t kMoverCount = 0x80071498u;" in motion
    and "constexpr uint32_t kPlayerSector = 0x80077630u;\n" in motion
    and "inline constexpr uint32_t kSectors = 0x800776E4u;" in steps,
    "the hooked routines, the tick counter, the movers' list and count, the player's sector and the sector table are the retail addresses",
)
require(
    "bool wanted() { return !g_off && disruptor_frame_rate_unlocked() && !psx_netplay_active(); }\n" in motion
    and "void draw_sector(CPUState *cpu, uint32_t) {\n"
    "    Guest guest;\n"
    "    take_back(guest, g_shifts);\n"
    "    if (!cpu || cpu->gpr[31] != kWorldLoop || !wanted() || (guest.word(kTicks) & 1u) == 0u) return;\n"
    "    lead(guest, g_shifts, cpu->gpr[4], guest.word(kMoverList), guest.word(kMoverCount), guest.byte(kPlayerSector));\n"
    "}\n"
    in motion
    and "constexpr uint32_t kWorldLoop = 0x800411A4u;" in motion
    and "void sector_drawn(CPUState *, uint32_t) {\n    Guest guest;\n    take_back(guest, g_shifts);\n}\n" in motion
    and "void draw_world(CPUState *, uint32_t) {\n"
    "    Guest guest;\n"
    "    catch_up(guest, g_lifts);\n"
    "    disruptor::objects::catch_up(guest, g_objects);\n"
    "    if (!wanted()) return;\n"
    "    disruptor::objects::lag(guest, g_objects);\n"
    "    const uint32_t sector = guest.word(kSectors + 4u * guest.byte(kPlayerSector));\n"
    "    const uint32_t riding = sector != 0u ? guest.byte(sector + kSectorLift) : kNoSector;\n"
    "    lag(guest, g_lifts, guest.word(kLiftRecords), kCameraHeight, guest.word(kPlayerHeight) - guest.word(kPlayerStands), riding);\n"
    "}\n"
    in motion
    and "void frame_waits(CPUState *cpu, uint32_t) {\n    Guest guest;\n    take_back(guest, g_shifts);\n" in motion
    and "    if (cpu && cpu->gpr[31] == kFrameWait) {\n"
    "        catch_up(guest, g_lifts);\n"
    "        disruptor::objects::catch_up(guest, g_objects);\n"
    "    }\n"
    in motion
    and "void tick_begins(CPUState *, uint32_t) {\n"
    "    Guest guest;\n"
    "    take_back(guest, g_shifts);\n"
    "    catch_up(guest, g_lifts);\n"
    "    note(guest, g_lifts, guest.word(kLiftRecords), guest.word(kLiftCount));\n"
    "    disruptor::objects::catch_up(guest, g_objects);\n"
    "    disruptor::objects::note(guest, g_objects);\n"
    "}\n"
    in motion
    and '    if (!psx_mod_register_function_entry_plugin("disruptor.mover_motion.drawn", kAfterSector, sector_drawn) ||\n'
    '        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.tick", kTick, tick_begins) ||\n'
    '        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.vsync", kVSync, frame_waits) ||\n'
    '        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.draw", kDrawSector, draw_sector) ||\n'
    '        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.world", kDrawWorld, draw_world)) {\n'
    "        g_off = true;\n"
    in motion
    and "constexpr uint32_t kDrawWorld = 0x80040E68u;\n" in motion
    and "constexpr uint32_t kFrameWait = 0x80043E84u;" in motion
    and "constexpr uint32_t kLiftCount = 0x80071408u;" in motion
    and "constexpr uint32_t kLiftRecords = 0x8007173Cu;" in motion
    and "constexpr uint32_t kCameraHeight = 0x800775D0u;\nconstexpr uint32_t kPlayerHeight = 0x800775C4u;\n" in motion
    and "constexpr uint32_t kPlayerStands = 0x80077608u;" in motion
    and "psx_mod_write" not in motion
    and '#include "game/guest.h"\n' in motion
    and "using disruptor::Guest;\n" in motion
    and "    void write(uint32_t address, uint32_t value) { psx_mod_write_word(address, value); }\n" in guest
    and "    void write_half(uint32_t address, uint16_t value) { psx_mod_write_half(address, value); }\n" in guest,
    "a door's half step is taken back before a sector is drawn, after it, when the frame waits and before a tick, and given only in "
    "the world's loop of sectors after an odd tick. A lift's lag is given when the world's draw begins and taken back at the frame "
    "loop's wait after its last draw and before a tick, where the lifts' heights are noted. Both only under 60 FPS gameplay, outside a network game and "
    "with every hook registered. An enemy's or a shot's lag is given and taken back where a lift's is, and the module writes "
    "guest memory only through the shared Guest's word and halfword calls",
)
require(
    'extern "C" void disruptor_mover_motion_state_loaded(void) {\n    forget(g_shifts);\n    forget(g_lifts);\n'
    "    disruptor::objects::forget(g_objects);\n}\n"
    in motion
    and "    disruptor_mover_motion_state_loaded();\n" in host,
    "a loaded state has no half step and no lag of this run to take back",
)
require(
    "inline constexpr uint32_t kLiftBytes = 36u;" in steps
    and "inline constexpr uint32_t kLiftHeight = 20u;" in steps
    and "inline constexpr uint32_t kSectorLift = 46u;" in steps
    and "inline constexpr uint32_t kLiftFloor = 8u;" in steps
    and "inline constexpr uint32_t kLiftCarries = 27u;" in steps
    and "    if (lifts.count != 0 || records != lifts.records) return;\n" in steps
    and "        if (step / 2 == 0 || step > kMostLiftStep || step < -kMostLiftStep) continue;\n" in steps
    and "            if (address == camera && !carries) continue;\n" in steps
    and "        const bool carries = index == riding && memory.byte(record + kLiftCarries) != 0u && memory.word(record + kLiftFloor) == stands;\n"
    in steps
    and "        if (memory.word(one.address) == one.left) memory.write(one.address, one.left + one.back);\n" in steps,
    "a lift is held back by half the step it took in the tick, from heights noted for the same records, the camera only with the "
    "lift of the player's sector that carries the player and has them on its floor (what the game's own carrying leaves), and a "
    "word is given back only while it is what was left there",
)
require(
    steps.count("memory.write(") == 4
    and "            if (memory.word(at) == shifts.left[index]) memory.write(at, shifts.left[index] - one.delta);\n"
    in steps
    and "                shifts.left[shifts.words] = memory.word(at) + delta;\n"
    "                memory.write(at, shifts.left[shifts.words++]);\n"
    in steps
    and "            if (delta == 0u || shifts.words + (found[part].last - found[part].first) / 4u > kMostHeld) continue;\n"
    in steps
    and "    if (shifts.count != 0) return;\n" in steps
    and "        if (sector == kNoSector || sector == player) continue;\n" in steps
    and "psx_mod" not in steps,
    "vertex words are the only memory written, a half step is taken back only from words left as it wrote them, none "
    "is given over one still held, and never to the mover the player stands in",
)
require(
    "inline constexpr uint32_t kSectorShots = 0x80077BF4u;" in objects
    and "inline constexpr uint32_t kSectorActors = 0x80078004u;" in objects
    and "inline constexpr uint32_t kSectorCount = 256u;" in objects
    and "inline constexpr uint32_t kShotLife = 32u;" in objects
    and "    return kind == Kind::shot ? record + 4u + 4u * static_cast<uint32_t>(axis) : record + 22u + 2u * static_cast<uint32_t>(axis);\n"
    in objects
    and "        if (read(memory, one.address, one.kind) == one.left) write(memory, one.address, one.kind, one.left + one.back);\n"
    in objects
    and "    if (steps.count != 0u) return;\n" in objects
    and "psx_mod" not in objects,
    "a sector's shots and actors are the lists the world's loop draws, a shot's coordinates are the words and an actor's the "
    "halfwords those draws read, and a coordinate is given back only while it is what was left there",
)

mesh = read("psxrecomp/runtime/include/gpu_temporal_mesh.h")
require(
    "            for (int index = 0; index <= candidate_count; ++index) {\n"
    "                const int *proposed = index < candidate_count ? candidates[index].shift : zero;\n"
    in mesh
    and "                const int rides = !proposed[0] && !proposed[1] && !proposed[2];\n" in mesh
    and "                    near_camera &= rides || abs(proposed[axis] - best_shift[axis]) <= GPU_TEMPORAL_TWIN_OBJECT_REACH;\n"
    in mesh
    and "                    trusted[triangle] = rides ? 3 : 2;\n" in mesh
    and "        const int rides = trusted[points[point].corner / 3] == 3;\n" in mesh
    and "        if (point && gpu_temporal_mesh_compare_point(&points[point - 1], &points[point])) last[0] = last[1] = -1;\n"
    in mesh
    and "        if (last[rides] >= 0) gpu_temporal_mesh_union(nodes, last[rides], points[point].corner);\n" in mesh
    and "                (trusted[a->triangle] == 3) == (trusted[b->triangle] == 3) &&\n" in mesh
    and "        if (has_camera && trusted[triangle] >= 2) dynamic = 1;\n" in mesh
    and "        if (trusted && trusted[triangle] >= 2) continue;" in mesh
    and mesh.index("    twins = gpu_temporal_mesh_pairs(prev, prev_n, curr, curr_n, matches, pairs, trusted, shift);\n")
    < mesh.index("        const int rides = trusted[points[point].corner / 3] == 3;\n"),
    "in an in-between picture the faces that kept their place before a moving camera are a rider with its own motion, whatever "
    "the camera's step, the pairs are known before corners are welded, and no corner of a rider is welded to one of the world",
)

frontend = read("psxrecomp/runtime/src/main.cpp")
saving = read("psxrecomp/runtime/src/savestate.c")
require(
    "            psx_frontend_before_savestate_save();\n            CPUState snap = *cpu;\n" in saving
    and 'extern "C" void psx_frontend_before_savestate_save(void) {\n#ifdef PSX_HAS_GAME_HOST\n    psx_game_state_saving();\n#endif\n}\n'
    in frontend
    and "void psx_game_state_saving(void);\n" in read("psxrecomp/runtime/include/game_host.h")
    and "void psx_game_state_saving(void) {\n    disruptor_mover_motion_state_saving();\n" in host
    and 'extern "C" void disruptor_mover_motion_state_saving(void) {\n    Guest guest;\n    take_back(guest, g_shifts);\n'
    "    catch_up(guest, g_lifts);\n    disruptor::objects::catch_up(guest, g_objects);\n}\n"
    in motion,
    "a half step and a lag are taken out of memory before a state is written, so that no saved state holds one",
)

image_path = ROOT / "input" / "SLUS_002.24"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    "the build gate must check the retail image: it is absent",
)
if not image_path.exists():
    print("Disruptor mover steps source contract: PASS (retail image absent)")
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
        f"the retail code from 0x{first:08X} to 0x{last:08X} is not the code the half steps were written against",
    )

print("Disruptor mover steps source and version contract: PASS")
