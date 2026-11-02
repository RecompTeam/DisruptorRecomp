#!/usr/bin/env python3
"""Source contract for the emulation thread's short cuts: each one keeps exactly the work of the path it stands for.

The block-edge interrupt check returns in the caller only where psx_check_interrupts' own fast path would return with
nothing else done. The store path calls its trace hooks only when their own armed flag is set. The BIOS dispatch skips
its table search only where the generated table holds no key, read back from the generated table itself.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    """A source as the compiler sees its code: comments removed, so a commented copy can neither pass nor hide a pin."""
    text = (ROOT / path).read_text(encoding="utf-8")
    if path.endswith((".c", ".cpp", ".h", ".inc")):
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
        text = re.sub(r"(?m)^((?:[^\"'\n]|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*')*?)//[^\n]*", r"\1", text)
    return text


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"hot paths contract: FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


cpu_state = read("psxrecomp/runtime/include/cpu_state.h")
interrupts = read("psxrecomp/runtime/src/interrupts.c")
interrupts_header = read("psxrecomp/runtime/include/interrupts.h")
memory = read("psxrecomp/runtime/src/memory.c")
parity = read("psxrecomp/runtime/src/parity_trace.c")
card = read("psxrecomp/runtime/src/card_data_writes.c")
debug_server = read("psxrecomp/runtime/src/debug_server.c")
precision = read("psxrecomp/runtime/include/gte_precision.h")
gte = read("psxrecomp/runtime/src/gte.cpp")
emitter = read("psxrecomp/recompiler/src/full_function_emitter.cpp")
icache_header = read("psxrecomp/runtime/include/psx_icache.h")
icache = read("psxrecomp/runtime/src/psx_icache.c")

require(
    "void psx_check_interrupts_at(CPUState* cpu, uint32_t resume_pc) {\n"
    "    uint32_t prev = s_compiled_interrupt_resume_pc;\n"
    "    s_compiled_interrupt_resume_pc = resume_pc;\n"
    "    s_resume_pc_given = 1;\n"
    "    psx_check_interrupts(cpu); \n"
    "    s_compiled_interrupt_resume_pc = prev;\n"
    "}\n"
    in interrupts
    and "    const int save_point = s_resume_pc_given || g_dirty_safe_resume_pc != 0u;\n    s_resume_pc_given = 0;\n"
    in interrupts,
    "the edge check stays out of line over the framework's longer check (mission 13 costs 2.09 ms of CPU a frame against "
    "the shipped build's 2.96), takes the resume PC and the save mark, and the check reads the mark once",
)
require(
    interrupts.count("if ((++s_fast_maintenance & 0x3FFFu) == 0 || g_psx_savestate_pending) {") == 2
    and interrupts.count("                if (save_point) savestate_poll(cpu, s_compiled_interrupt_resume_pc);\n") == 2,
    "both fast returns poll a save only at a save point, and their maintenance stays amortized",
)
for source, definition in (
    (interrupts, "void psx_check_interrupts(CPUState* cpu) {"),
    (interrupts, "void psx_check_interrupts_at(CPUState* cpu, uint32_t resume_pc) {"),
    (parity, "void parity_trace_note_write(uint32_t addr, uint32_t width, uint32_t writer_pc)"),
    (card, "int card_data_writes_check(uint32_t phys, uint32_t value, uint8_t width) {"),
):
    require(source.count(definition) == 1, f"one definition of {definition.split('(')[0]}, the one the pins read")
require(
    "s_callbacks.check_interrupts_at  = overlay_ci_at_wrapper;" in read("psxrecomp/runtime/src/overlay_loader.c"),
    "overlay DLLs reach the check through the host's callback table, not by the symbol's name",
)


require(
    "    psx_icache_reset();   \n" in read("psxrecomp/runtime/src/main.cpp")
    and read("psxrecomp/runtime/src/main.cpp").index("    psx_icache_reset();   \n")
    < read("psxrecomp/runtime/src/main.cpp").index("    cpu.read_word  = psx_read_word;\n")
    and "void psx_icache_reset(void) {\n    g_psx_icache_active = psx_icache_enabled();\n"
    "    for (int i = 0; i < 1024; i++) g_psx_icache_tv[i] = 0x1u;\n}\n"
    in icache
    and "    static int s = -1;\n    if (s < 0) {\n" in icache,
    "the runtime resolves the flag and empties the tags before the CPU runs, and the setting is read once",
)

require(
    "#define parity_trace_note_write(addr, width, writer_pc) \\\n"
    "    do { if (g_parity_trace_armed) (parity_trace_note_write)((addr), (width), (writer_pc)); } while (0)\n"
    in memory
    and "#define card_data_writes_check(phys, value, width) \\\n"
    "    do { if (g_card_data_writes_armed) (void)(card_data_writes_check)((phys), (value), (width)); } while (0)\n"
    in memory,
    "the store hooks are skipped only when their own armed flag is clear",
)
require(
    "uint32_t g_parity_trace_armed = 0;" in parity
    and "#define s_armed g_parity_trace_armed\n" in parity
    and "void parity_trace_note_write(uint32_t addr, uint32_t width, uint32_t writer_pc)\n{\n    if (!s_armed || s_frozen) return;\n"
    in parity,
    "the parity hook returns at once when its flag is clear",
)

require(
    len(re.findall(r"\bparity_trace_note_write\(phys, ", memory)) == 3
    and len(re.findall(r"\bcard_data_writes_check\(phys, (?!value, width\))", memory)) == 3,
    "the hooks are called by the three RAM store widths and nowhere else",
)
require(
    "volatile int g_card_data_writes_armed = 0;" in card
    and "#define s_armed g_card_data_writes_armed\n" in card
    and "int card_data_writes_check(uint32_t phys, uint32_t value, uint8_t width) {\n    if (!s_armed) return 0;\n"
    in card,
    "the card hook returns at once when its flag is clear",
)
require(
    "#ifdef PSX_NO_DEBUG_TOOLS\n#define debug_server_trace_write_check(phys, old_val, new_val, width) ((void)0)\n#endif\n"
    in memory
    and "                                    uint32_t new_val, uint8_t width)\n{\n#ifdef PSX_NO_DEBUG_TOOLS\n"
    "    (void)phys; (void)old_val; (void)new_val; (void)width;\n    return;\n#endif\n"
    in debug_server,
    "the write trace is compiled out only where its body returns at once",
)

require(
    "int fntrace_is_game_started(void) { return s_game_started; }" in read("psxrecomp/runtime/src/fntrace.c"),
    "the game-start test has no side effect, so its place in the chain is free",
)

require(
    "static inline void gte_precision_word_write_begin(void) {\n"
    "    g_gte_precision_word_commit.domain = 0u;\n"
    "    g_gte_precision_word_commit.pending = 0u;\n}\n"
    in precision
    and "    g_gte_precision_word_commit.physical = physical;\n"
    "    g_gte_precision_word_commit.domain = 1u;\n"
    "    g_gte_precision_word_commit.pending = 1u;\n"
    in precision
    and "#define s_precision_word_commit g_gte_precision_word_commit\n" in gte
    and "static_assert(PRECISION_WORD_DOMAIN_NONE == 0 && PRECISION_WORD_DOMAIN_MAIN_RAM == 1," in gte
    and 'extern "C" void gte_precision_word_write_begin' not in gte,
    "the inline token writers write gte.cpp's own token with its enum's values",
)
gpu = read("psxrecomp/runtime/src/gpu.c")
require(
    "void gpu_geometry_diagnostics_set_detail(int enabled) {\n"
    "    if (enabled && !s_geometry_diag_detail) {\n"
    "        geometry_diag_clear_vertex_detail(&s_geometry_diag_cumulative);\n"
    "        geometry_diag_clear_vertex_detail(&s_geometry_diag_live_current);\n"
    "        geometry_diag_clear_vertex_detail(&s_geometry_diag_live_latest);\n"
    "    }\n"
    in gpu
    and "    memset(stats->depth_vertices, 0,\n           offsetof(GpuGeometryCorrectionCounters, partial_polygon_rejections) -\n"
    "               offsetof(GpuGeometryCorrectionCounters, depth_vertices));\n"
    in gpu
    and "    for (int i = 0; s_geometry_diag_detail && i < count; ++i) {\n" in gpu,
    "the per-vertex fields count only under detail and start from zero when it is switched on",
)
indexes = re.findall(r"s_precision_(?:tag|projection)\[([^\]]+)\]", gte)
require(
    set(indexes) == {"PRECISION_STORE_SIZE", "slot", "precision_hash(physical)"}
    and indexes.count("PRECISION_STORE_SIZE") == 2,
    "the provenance tags and projections are only ever indexed by the address's hash",
)
slots = re.findall(r"^\s*const uint32_t slot = ([^;]+);$", gte, re.MULTILINE)
require(
    slots == ["precision_hash(physical)"] * 5 and gte.count("s_precision_projection[slot]") == 5,
    "every slot is the hash of the one address whose tag it reads, and its projection is the tag's own",
)


require(
    "        if (dispatch_keys[i] <= dispatch_keys[i - 1])\n            throw std::runtime_error(fmt::format(\n"
    '                "dispatch table key 0x{:08X} follows 0x{:08X}: the binary search and its key gap need ascending keys",'
    in emitter,
    "the generator refuses keys out of order",
)
print("hot paths contract: PASS")
