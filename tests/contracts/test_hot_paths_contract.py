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
    "    if (check_interrupts_quiet_edge(cpu, resume_pc)) return;\n"
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
quiet = interrupts[
    interrupts.index(
        "static inline int check_interrupts_quiet_edge(CPUState* cpu, uint32_t resume_pc) {"
    ) : interrupts.index("void psx_check_interrupts_at(CPUState* cpu, uint32_t resume_pc) {")
]
quiet_guards, quiet_writes = (
    quiet[: quiet.index("    ++s_fast_maintenance;\n")],
    quiet[quiet.index("    ++s_fast_maintenance;\n") :],
)
require(
    all(
        guard in quiet_guards
        for guard in (
            "(uint32_t)g_psx_guest_time_frozen",
            "g_psx_cyc_batch",
            "(uint32_t)in_exception",
            "(uint32_t)s_defer_switch_pending",
            "(uint32_t)g_psx_savestate_pending",
            "(uint32_t)g_ls_suppress_record",
            "g_dirty_safe_resume_pc",
            "(i_stat & i_mask)",
            "(cpu->cop0[COP0_CAUSE] & cpu->cop0[COP0_SR] & 0x0300u)) return 0;",
            "    if (g_psx_cyc_local_acc && *g_psx_cyc_local_acc) return 0;\n",
            "    if (g_idle_skip_enabled <= 0 || !g_psx_idle_edge_fresh || !resume_pc || resume_pc == s_idle_pc) return 0;\n",
            "(uint32_t)g_ls_replay_active",
            "(uint32_t)g_ls_mode",
            "(uint32_t)g_precise_mode",
            "(uint32_t)g_psx_call_bail",
            "(uint32_t)g_idle_note_suppress",
            "(uint32_t)g_idle_note_suppress | (uint32_t)((g_psx_idle_site_filter >> ((s_idle_pc >> 2) & 63u)) & 1u)) return 0;\n",
            "    if (((s_fast_maintenance + 1u) & 0x3FFFu) == 0) return 0;\n",
        )
    )
    and quiet_guards.count("return 0;") == 5
    and "return 1;" not in quiet_guards
    and quiet_writes.strip()
    == "++s_fast_maintenance;\n    ++s_irq_path_entry;\n    ++s_irq_path_fast_none;\n    s_last_interrupt_check_pc = resume_pc;\n"
    "    s_last_interrupt_check_cycle = psx_cycle_count;\n    s_idle_pc = resume_pc;\n    s_idle_last_cycle = psx_cycle_count;\n"
    "    s_idle_last_stores = g_guest_store_count;\n    s_idle_last_mmio = g_mmio_access_count;\n    return 1;\n}",
    "the edge's quiet return is taken only with every state the full check and the idle detector read at rest and the "
    "maintenance not due, and it writes what that check's return with nothing pending and the detector's restart write",
)
cycles = read("psxrecomp/runtime/src/psx_cycles.c")
full_begin = cycles.index("static void idle_note_check_full(CPUState *cpu, uint32_t check_pc) {")
full_end = cycles.index("static int idle_edge_fresh(void) {")
full, outside = cycles[full_begin:full_end], cycles[:full_begin] + cycles[full_end:]
restart = full[full.index("    if (check_pc != s_idle_pc) {\n") :]
restart = restart[: restart.index("    }\n")]
fresh = cycles[full_end : cycles.index("void psx_idle_note_check(CPUState *cpu, uint32_t check_pc) {")]
progress = (
    "s_idle_streak",
    "s_idle_quantum",
    "s_idle_have_snap",
    "s_idle_progress_reg",
    "s_idle_progress_delta",
    "s_idle_mem_progress",
    "s_idle_store_addr",
    "s_idle_edges",
)
written_outside = set(re.findall(r"(?m)^\s+(s_idle_\w+)\s*(?:=[^=]|\+\+|--|[-+]=)", outside))
reset = cycles[cycles.index("static void idle_reset_progress(void) {") :]
reset = reset[: reset.index("}\n")]
require(
    "void psx_idle_note_check(CPUState *cpu, uint32_t check_pc) {\n    idle_note_check_full(cpu, check_pc);\n"
    "    g_psx_idle_edge_fresh = idle_edge_fresh();\n}\n"
    in cycles
    and cycles.count("idle_note_check_full(") == 2
    and cycles.count("g_psx_idle_edge_fresh = idle_edge_fresh();") == 1
    and "static int idle_edge_fresh(void) {\n#if defined(PSX_COSIM) || defined(PSX_HAS_RECOMP_NET)\n    return 0;\n#else\n"
    in cycles
    and len(re.findall(r"g_psx_idle_edge_fresh = (?!0;|idle_edge_fresh\(\);)", cycles)) == 0
    and all(f"{name} == " in fresh or f"!{name} " in fresh for name in progress)
    and all(
        token in fresh
        for token in ("!s_idle_audited_only", "idle_skip_on() > 0", "!g_ls_replay_active", "!g_idle_note_suppress")
    )
    and "*s_track == (g_idle_skip_ext > 0 && s_idle_host_write != NULL && s_track != &s_track_dummy);" in fresh
    and {name for name in re.findall(r"\b(s_idle_\w+)\s*=", restart + reset)}
    == set(progress) | {"s_idle_pc", "s_idle_last_cycle", "s_idle_last_stores", "s_idle_last_mmio"}
    and all(
        f"{definition} {{\n    g_psx_idle_edge_fresh = 0;\n" in cycles
        for definition in (
            "void psx_idle_skip_set_store_tracker(int *gate, const uint32_t *addr, const uint32_t *val)",
            "void psx_idle_skip_set_audited_only(int enabled)",
        )
    )
    and "    if (n > IDLE_SITES_MAX) n = IDLE_SITES_MAX;\n    g_psx_idle_edge_fresh = 0;\n    g_psx_idle_site_filter = 0;\n"
    in cycles
    and "        g_psx_idle_site_filter |= 1ull << ((pcs[i] >> 2) & 63u);\n" in cycles
    and written_outside
    == {
        "s_idle_hi",
        "s_idle_lo",
        "s_idle_site_count",
        "s_idle_audited_only",
        "s_idle_pc",
        "s_idle_have_snap",
        "s_idle_edges",
        "s_idle_streak",
        "s_idle_quantum",
        "s_idle_progress_reg",
        "s_idle_progress_delta",
        "s_idle_mem_progress",
        "s_idle_last_cycle",
    }
    and "void psx_idle_skip_set_host_writer(void (*w)(uint32_t, uint32_t)) { g_psx_idle_edge_fresh = 0; s_idle_host_write = w; }"
    in cycles
    and cycles[cycles.index("void psx_cycles_resync_after_restore(") :].count("    g_psx_idle_edge_fresh = 0;\n") == 1
    and cycles.count("idle_skip_ext_on()") == 1,
    "the detector's restart writes the four words the edge writes and the progress fields the fresh mark is taken from, the "
    "mark is taken anew after every full note and dropped by every other writer, never set in a co-simulation or network build, "
    "and the site filter has a bit for every site",
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
