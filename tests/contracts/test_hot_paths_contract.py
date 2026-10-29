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

require(
    "static inline void psx_check_interrupts_at(struct CPUState* cpu, uint32_t resume_pc) {\n"
    "#if !defined(PSX_COSIM)\n"
    "    const uint32_t outer = psx_irq_compiled_resume_pc;\n"
    "    psx_irq_compiled_resume_pc = resume_pc;\n"
    "    psx_irq_resume_pc_given = 0;\n"
    "    psx_cyc_batch_flush();\n"
    "    if (!psx_irq_in_exception && !psx_irq_defer_switch_pending && g_idle_skip_enabled == 0 && (i_stat & i_mask) == 0 &&\n"
    "        (cpu->cop0[13] & cpu->cop0[12] & 0x0300u) == 0 && !psx_irq_save_poll_owed &&\n"
    "        ((psx_irq_fast_maintenance + 1u) & 0x3FFFu) != 0u) {\n"
    "        ++psx_irq_fast_maintenance;\n"
    "        psx_irq_compiled_resume_pc = outer;\n"
    "        return;\n"
    "    }\n"
    "    psx_check_interrupts_flushed(cpu, 1);\n"
    "    psx_irq_compiled_resume_pc = outer;\n"
    "#else\n"
    "    psx_check_interrupts_at_slow(cpu, resume_pc);\n"
    "#endif\n"
    "}\n" in cpu_state,
    "the inline edge is the old wrapper step by step: resume PC and taken mark before the flush, the fast path's return, "
    "else the check from after its flush",
)
require(
    "void psx_check_interrupts_at_slow(CPUState* cpu, uint32_t resume_pc) {\n"
    "    uint32_t prev = s_compiled_interrupt_resume_pc;\n"
    "    s_compiled_interrupt_resume_pc = resume_pc;\n"
    "    s_resume_pc_given = 1;\n"
    "    psx_check_interrupts(cpu); \n"
    "    s_compiled_interrupt_resume_pc = prev;\n"
    "}\n"
    in interrupts
    and "void psx_check_interrupts(CPUState* cpu) {\n"
    "    const int save_point = s_resume_pc_given;\n"
    "    s_resume_pc_given = 0;\n"
    "    psx_cyc_batch_flush();\n"
    "    psx_check_interrupts_flushed(cpu, save_point);\n"
    "}\n"
    in interrupts,
    "the wrapper the inline edge unrolls, and the check that takes the mark and flushes before its rest",
)
flushed = interrupts[interrupts.index("void psx_check_interrupts_flushed(CPUState* cpu, int save_point) {") :]
flushed = flushed[: flushed.index("    if (in_exception && !(cpu->cop0[COP0_SR] & 0x01u)) return;")]
flushed_code = "\n".join(
    line.rstrip() for line in re.sub(r"/\*.*?\*/", "", flushed, flags=re.DOTALL).split("\n") if line.strip()
)
require(
    flushed_code
    == (
        "void psx_check_interrupts_flushed(CPUState* cpu, int save_point) {\n"
        "    extern int g_ls_suppress_record;\n"
        "#define PSX_CHECK_INTERRUPTS_RETURN() do { if (g_ls_suppress_record > 0) g_ls_suppress_record--; return; } while (0)\n"
        "#ifdef PSX_COSIM\n"
        "    extern uint32_t g_dirty_safe_resume_pc;\n"
        "#define COSIM_IRQ_TAKE_PC() (g_dirty_safe_resume_pc ? g_dirty_safe_resume_pc : s_compiled_interrupt_resume_pc)\n"
        "#define COSIM_IRQ_NOTE(kind_) cosim_irq_note(cpu, (kind_), COSIM_IRQ_TAKE_PC(), g_dirty_safe_resume_pc, "
        "s_compiled_interrupt_resume_pc, cpu->cop0[COP0_SR])\n"
        "#endif\n"
        "    if (!in_exception && !s_defer_switch_pending && (i_stat & i_mask) != 0) {\n"
        "        uint32_t sr = cpu->cop0[COP0_SR];\n"
        "        if (!(sr & 0x01u) || !(sr & (1u << 10))) {\n"
        "            const int periodic = (++s_fast_maintenance & 0x3FFFu) == 0;\n"
        "            if (psx_save_poll_due(periodic, save_point, &s_save_poll_owed))\n"
        "                savestate_poll(cpu, s_compiled_interrupt_resume_pc);\n"
        "            if (periodic) debug_server_poll();\n"
        "            return;\n"
        "        }\n"
        "    }\n"
        "    if (g_idle_skip_enabled < 0) (void)psx_idle_skip_is_enabled();\n"
        "    uint32_t sw_pending = cpu->cop0[COP0_CAUSE] & cpu->cop0[COP0_SR] & 0x0300u;\n"
        "    if (!in_exception && (i_stat & i_mask) == 0 && sw_pending == 0 &&\n"
        "        !s_defer_switch_pending) {\n"
        "        if (g_idle_skip_enabled > 0) {\n"
        "            uint32_t check_pc = g_dirty_safe_resume_pc ? g_dirty_safe_resume_pc\n"
        "                                                       : s_compiled_interrupt_resume_pc;\n"
        "            s_last_interrupt_check_pc = check_pc;\n"
        "            s_last_interrupt_check_cycle = psx_get_cycle_count();\n"
        "            psx_idle_note_check(cpu, check_pc);\n"
        "        }\n"
        "        if ((i_stat & i_mask) == 0 && sw_pending == 0) {\n"
        "            const int periodic = (++s_fast_maintenance & 0x3FFFu) == 0;\n"
        "            if (psx_save_poll_due(periodic, save_point, &s_save_poll_owed))\n"
        "                savestate_poll(cpu, s_compiled_interrupt_resume_pc);\n"
        "            if (periodic) debug_server_poll();\n"
        "            return;\n"
        "        }\n"
        "    }"
    )
    and "#define COP0_SR    12\n#define COP0_CAUSE 13\n" in interrupts,
    "the check's code up to the delivery is the code the inline edge copies, statement for statement: any other work "
    "there would be skipped by the inline return",
)
for source, definition in (
    (interrupts, "void psx_check_interrupts(CPUState* cpu) {"),
    (interrupts, "void psx_check_interrupts_flushed(CPUState* cpu, int save_point) {"),
    (interrupts, "void psx_check_interrupts_at_slow(CPUState* cpu, uint32_t resume_pc) {"),
    (cpu_state, "static inline void psx_check_interrupts_at(struct CPUState* cpu, uint32_t resume_pc) {"),
    (parity, "void parity_trace_note_write(uint32_t addr, uint32_t width, uint32_t writer_pc)"),
    (card, "int card_data_writes_check(uint32_t phys, uint32_t value, uint8_t width) {"),
):
    require(source.count(definition) == 1, f"one definition of {definition.split('(')[0]}, the one the pins read")
for name, exported in (
    ("in_exception", "psx_irq_in_exception"),
    ("s_save_poll_owed", "psx_irq_save_poll_owed"),
    ("s_resume_pc_given", "psx_irq_resume_pc_given"),
    ("s_defer_switch_pending", "psx_irq_defer_switch_pending"),
    ("s_fast_maintenance", "psx_irq_fast_maintenance"),
    ("s_compiled_interrupt_resume_pc", "psx_irq_compiled_resume_pc"),
):
    require(
        re.findall(rf"^#define {name} (\w+)$", interrupts, re.MULTILINE) == [exported]
        and re.search(rf"^(?:int|uint32_t) {exported}(?: = 0)?;", interrupts, re.MULTILINE) is not None
        and re.search(rf"\b{exported}\b", cpu_state) is not None,
        f"interrupts.c's {name} is {exported}, the state the inline edge reads",
    )
require(
    "static uint32_t s_fast_maintenance" not in interrupts
    and "static int in_exception;" not in interrupts
    and "static uint32_t s_compiled_interrupt_resume_pc" not in interrupts,
    "no second copy of the inline edge's state",
)
require(
    "s_callbacks.check_interrupts_at  = overlay_ci_at_wrapper;" in read("psxrecomp/runtime/src/overlay_loader.c"),
    "overlay DLLs reach the check through the host's callback table, not by the symbol's name",
)
icache_header = read("psxrecomp/runtime/include/psx_icache.h")
icache = read("psxrecomp/runtime/src/psx_icache.c")
require(
    "    if (g_psx_icache_tv[(addr & 0xFFCu) >> 2] == addr) return;\n    psx_icache_fetch_miss(cpu, addr);\n"
    in icache_header
    and "    if (g_psx_icache_active < 0) g_psx_icache_active = psx_icache_enabled();\n    if (!g_psx_icache_active) return;\n"
    "    uint32_t idx = (addr & 0xFFCu) >> 2;\n    if (g_psx_icache_tv[idx] == addr) return;"
    in icache
    and "    if (g_psx_icache_active < 0) {\n        psx_icache_fetch(cpu, addr);\n        return;\n    }\n"
    "    if (!g_psx_icache_active) return;\n"
    in icache_header,
    "the inline fetch returns on the miss path's own hit, and the flag a hit leaves unresolved is resolved by each reader",
)
flag_lines = {
    "psxrecomp/runtime/src/psx_icache.c": [
        "int g_psx_icache_active = -1;",
        "    g_psx_icache_active = psx_icache_enabled();",
        "    if (g_psx_icache_active < 0) g_psx_icache_active = psx_icache_enabled();",
        "    if (!g_psx_icache_active) return;",
    ],
    "psxrecomp/runtime/include/psx_icache.h": [
        "extern int g_psx_icache_active;",
        "    if (g_psx_icache_active < 0) {",
        "    if (!g_psx_icache_active) return;",
    ],
}
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".ipp")
named = {}
for folder in ("psxrecomp/runtime", "psxrecomp/generated", "src", "generated"):
    for path in (ROOT / folder).rglob("*"):
        if path.suffix in SOURCE_SUFFIXES and "/tests/" not in path.as_posix():
            relative = path.relative_to(ROOT).as_posix()
            lines = [line for line in read(relative).split("\n") if re.search(r"\bg_psx_icache_active\b", line)]
            if lines:
                named[relative] = lines
require(
    named == flag_lines,
    "the cache flag is set at its definition, by the reset and by the miss path's resolve, and read only by the miss "
    "path and the interpreter's fetch, each after resolving it: whether a hit resolved it is never seen",
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
    "#if defined(PSX_OVERLAY_DLL_BUILD)\nvoid psx_check_interrupts_at(struct CPUState* cpu, uint32_t resume_pc);\n#endif\n"
    in interrupts_header
    and interrupts.count("void psx_check_interrupts_at_slow(CPUState* cpu, uint32_t resume_pc) {\n") == 1,
    "overlay DLLs keep the call, every other build takes the inline edge",
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
for width, old, value, last in (
    (4, "read_ram_word(phys)", "val", "        ram[phys]     = (uint8_t)(val);\n"),
    (2, "(uint32_t)read_ram_half(phys)", "(uint32_t)val", "        ram[phys]     = (uint8_t)(val);\n"),
    (1, "(uint32_t)ram[phys]", "(uint32_t)val", "        ram[phys] = val;\n"),
):
    hooks = (
        f"        debug_server_trace_write_check(phys, {old}, {value if width != 4 else 'val'}, {width});\n"
        f"        parity_trace_note_write(phys, {width}, effective_store_pc());\n"
        f"        card_data_writes_check(phys, {value}, {width});\n"
        f"        dirty_ram_mark_kernel_write(phys);\n"
        f"        text_guard_note_write(phys, {value}, {width});\n"
        f"        overlay_watch_note_write(phys, {width});\n"
        "#ifdef PSX_COSIM\n"
        f"        {{ extern void cosim_note_ram_write(uint32_t,uint32_t); cosim_note_ram_write(phys, {width}); }}\n"
        "#endif\n" + last
    )
    require(memory.count(hooks) == 1, f"the {width}-byte RAM store calls every hook once, before its bytes land")
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
for guard in (
    "    if (phys < 0x10u && g_debug_last_store_pc == 0xBFC10A00u &&\n        fntrace_is_game_started()) return;\n",
    "    if (phys == 0u && fntrace_is_game_started()) {\n",
    "        if (g_debug_last_store_pc == 0xBFC117E4u &&\n            fntrace_is_game_started() &&\n",
):
    require(guard in memory, "the game-start test comes after the cheap compares of its chain: " + guard.strip())
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

game_dispatch = ROOT / "generated" / "SLUS_002.24.code_dispatch.c"
# A build without the game leaves an empty file here (build.sh).
if game_dispatch.is_file() and game_dispatch.stat().st_size:
    text = game_dispatch.read_text(encoding="utf-8")
    rows = [row for row in text.split("k_psx_game_dispatch[] = {", 1)[1].split("};", 1)[0].split("\n") if row.strip()]
    keys = [re.fullmatch(r"    \{0x([0-9A-F]{8})u, [^{}]*\},", row) for row in rows]
    require(all(keys), "every row of the game's dispatch table is a plain entry the checks below can read")
    entries = [int(key.group(1), 16) for key in keys]
    index_text = re.search(
        r"static const uint(?:16|32)_t k_psx_game_dispatch_index\[\] = \{\n(.*?)\n\};", text, re.DOTALL
    )
    lookup = re.search(
        r"    const uint32_t offset = addr - 0x([0-9A-F]{8})u;\n"
        r"    if \(\(addr & 3u\) \|\| offset > 0x([0-9A-F]+)u\) return 0;\n"
        r"    const uint32_t index = k_psx_game_dispatch_index\[offset >> 2\];\n"
        r"    return index \? &k_psx_game_dispatch\[index - 1u\] : 0;\n",
        text,
    )
    require(index_text is not None and lookup is not None, "the game's dispatch resolves through its word index")
    index = [int(value) for value in re.findall(r"(\d+)u,", index_text.group(1))]
    low, span = int(lookup.group(1), 16), int(lookup.group(2), 16)
    expected = [0] * (span // 4 + 1)
    for position, key in enumerate(entries):
        expected[(key - low) // 4] = position + 1
    require(
        all(key & 3 == 0 for key in entries)
        and entries == sorted(set(entries))
        and entries[0] == low
        and entries[-1] == low + span
        and index == expected,
        f"the word index names each of the {len(entries)} keys at its own word and nothing else",
    )
elif "--require-artifacts" in sys.argv:
    require(False, "the translated game's dispatch is not there")

require(
    "        if (dispatch_keys[i] <= dispatch_keys[i - 1])\n            throw std::runtime_error(fmt::format(\n"
    '                "dispatch table key 0x{:08X} follows 0x{:08X}: the binary search and its key gap need ascending keys",'
    in emitter,
    "the generator refuses keys out of order",
)
dispatch_path = ROOT / "psxrecomp" / "generated" / "OpenBIOS_dispatch.c"
if not dispatch_path.is_file():
    require("--require-artifacts" not in sys.argv, "the translated BIOS is not there")
    print("hot paths contract: PASS (sources only, no translated BIOS)")
    raise SystemExit(0)
dispatch = dispatch_path.read_text(encoding="utf-8")
table = re.search(r"static const DispatchEntry dispatch_table\[(\d+)\] = \{\n(.*?)\n\};", dispatch, re.DOTALL)
require(table is not None, "the generated BIOS has its dispatch table")
keys = [int(key, 16) for key in re.findall(r"^    \{ 0x([0-9A-F]{8})u, ", table.group(2), re.MULTILINE)]
require(len(keys) == int(table.group(1)) and keys == sorted(set(keys)), "the table's keys are whole and ascending")
gap = re.findall(
    r"if \(!found && !\(phys > 0x([0-9A-F]{8})u && phys < 0x([0-9A-F]{8})u\)\) \{\n        while \(lo <= hi\) \{",
    dispatch,
)
require(len(gap) == 1, "one search, guarded once")
low, high = (int(value, 16) for value in gap[0])
require(
    low in keys and high in keys and not any(low < key < high for key in keys),
    "no key of the table lies inside the gap the search skips",
)
require(low < 0x00010000 and high >= 0x00200000, "the gap covers the game's RAM, else the guard buys nothing")
print(f"hot paths contract: PASS (gap 0x{low:08X} to 0x{high:08X}, {len(keys)} keys)")
