#!/usr/bin/env python3
"""Source contract for the step fusion: the generator joins per-instruction cycle steps that only register-only lines
separate, and the code audit reads every joined run back from the output.

The audit's check is shown able to refuse: each kind of line the fusion must never step over is put into a copy of the
real output between a run's steps, and the check must name it.
"""

import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERATED = ROOT / "generated"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"step fusion contract: FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


codegen = read("psxrecomp/recompiler/src/code_generator.cpp")
cyc = read("psxrecomp/runtime/include/psx_cyc.h")
cycles = read("psxrecomp/runtime/src/psx_cycles.c")
cmake = read("CMakeLists.txt")

require(
    "    if (!cycle_per_insn) return ss.str();\n    const std::string block_text = ss.str();\n"
    "    std::string fused = fuse_cycle_steps(block_text, config_.indent);\n"
    "    if (restore_cycle_steps(fused, config_.indent) != block_text) {\n"
    in codegen
    and "        throw std::runtime_error(refusal);\n" in codegen,
    "every block of the per-instruction cycle model is fused, and the generator refuses one whose steps do not come back",
)
require(
    "static const std::set<std::string_view> kCastTypes = {\n"
    '    "uint32_t", "int32_t", "uint16_t", "int16_t", "uint8_t", "int8_t", "uint64_t", "int64_t"};'
    in codegen
    and '    static constexpr std::string_view kAllowedMarks = " ()+-<>|^~!=?:&_@";' in codegen
    and 'CAST_TYPES = {"uint32_t", "int32_t", "uint16_t", "int16_t", "uint8_t", "int8_t", "uint64_t", "int64_t"}'
    in read("tools/audit_codegen.py")
    and 'REGISTER_MARKS = set(" ()+-<>|^~!=?:&_@")' in read("tools/audit_codegen.py"),
    "the generator's register-only words and marks are the audit's",
)
for count in (2, 3, 4):
    require(
        f"    psx_cyc_run_inline(cpu, masks, {count}u);\n" in cycles,
        f"psx_cyc_step{count} is the shared run of {count} steps",
    )
require(
    "    disruptor_unit_test(psx_cyc_run tests/unit/psx_cyc_run_test.cpp RUNTIME_HEADERS)\n" in cmake,
    "the run's equality with the steps one by one stays tested",
)
require(
    cyc.count("PSX_CYC_HOT void psx_cyc_run_inline(") == 1 and cyc.count("PSX_CYC_HOT void psx_cyc_charge_ones(") == 1,
    "one run and one charge of ones",
)

shards = sorted(GENERATED.glob("SLUS_002.24.code_full_*.c"))
if not shards:
    require("--require-artifacts" not in sys.argv, "the translated game code is not there")
    print("step fusion contract: PASS (sources only, no translated code)")
    raise SystemExit(0)

specification = importlib.util.spec_from_file_location("audit_codegen", ROOT / "tools" / "audit_codegen.py")
require(specification is not None and specification.loader is not None, "tools/audit_codegen.py cannot be loaded")
audit = importlib.util.module_from_spec(specification)
specification.loader.exec_module(audit)

text = "\n".join(path.read_text(encoding="utf-8") for path in shards)
heads, violations = audit.fused_step_violations(text)
require(
    heads == audit.EXPECTED_FUSED_STEPS and not violations,
    f"the output holds {heads} runs and {len(violations)} violations: {violations[:3]}",
)

lines = text.split("\n")
head = next(index for index, line in enumerate(lines) if line.startswith("    psx_cyc_step4(cpu, "))
first_member = lines.index("#if defined(PSX_ENABLE_BLOCK_CYCLES) && defined(PSX_COSIM)", head)


def refused(changed: list[str], what: str) -> None:
    _, found = audit.fused_step_violations("\n".join(changed))
    require(bool(found), f"the audit's check passes a run with {what}")


for line, what in (
    ("    psx_icache_fetch(cpu, 0x80010000u);", "a fetch between its steps"),
    ("    cpu->gpr[2] = psx_read_word(cpu->gpr[3]);", "a call between its steps"),
    ("    cpu->hi = cpu->gpr[2];", "a write of another field between its steps"),
    ("    cpu->gpr[2] = cpu->lo;", "a read of another field between its steps"),
    ("    cpu->gpr[2] = g_psx_cyc_batch;", "a read of a global between its steps"),
    ("block_80010000:;", "a label between its steps"),
    ("    /* half a comment", "an unclosed comment between its steps"),
    ("    psx_cyc_bb_defer_flush();", "a block edge between its steps"),
    ("    cpu->gpr[2] = *(uint32_t *)cpu->gpr[3];", "a memory read through a pointer between its steps"),
    ("    cpu->gpr[2] = cpu->gpr[3][1];", "an index past a register between its steps"),
    ("    cpu->gpr[2] = (uint32_t)&cpu->gpr[3];", "an address taken between its steps"),
    ("    cpu->gpr[2] = cpu->gpr[3] / cpu->gpr[4];", "a division between its steps"),
    ("    cpu->gpr[2] = cpu->gpr[x];", "a register chosen at run time between its steps"),
    ("    cpu->gpr[2] = cpu->gpr[3], cpu->hi = 0;", "a comma between its steps"),
    ("    cpu->gpr[2] = r;", "a name standing for a register between its steps"),
    ("    cpu->gpr[2] = π;", "a name outside ASCII between its steps"),
    (
        "    cpu->gpr[2] = cpu->gpr[3]; /* note */ g_psx_cyc_batch = 0;",
        "a statement behind a comment between its steps",
    ),
    ("    cpu->gpr[2] = (r)();", "a call through parentheses between its steps"),
    ("    cpu->gpr[2] = (cpu->gpr[3])(cpu->gpr[4]);", "a register called between its steps"),
    ("    cpu->gpr[2] = @;", "the substitute mark itself between its steps"),
    ("    cpu->gpr[2] = cpu->gpr[32];", "a read past the registers between its steps"),
    ("    cpu->gpr[32] = cpu->gpr[2];", "a write past the registers between its steps"),
    ("    cpu->gpr[2] = cpu->gpr[07];", "a register written with a leading zero between its steps"),
    ("    cpu->gpr[2] = (uint32_t)(cpu->gpr[3] + 1u)(0);", "a call after a cast group between its steps"),
):
    refused(lines[: head + 4] + [line] + lines[head + 4 :], what)
    refused(lines[:first_member] + [line] + lines[first_member:], what)

changed = list(lines)
changed[first_member + 1] = changed[first_member + 1].replace("psx_cyc_step(cpu, 0x", "psx_cyc_step(cpu, 0x1")
refused(changed, "a step whose mask is not the fused one")
refused(lines[:first_member] + lines[first_member + 3 :], "a step missing")
refused(lines[: first_member + 3] + lines[first_member:], "a step twice")
changed = list(lines)
changed[head] = changed[head].replace("psx_cyc_step4(", "psx_cyc_step3(")
refused(changed, "an arity that is not its mask count")
changed = list(lines)
changed[head + 2] = "    psx_cyc_step(cpu, 0x0u);"
refused(changed, "a co-simulation first step that is not the first mask")

print(f"step fusion contract: PASS ({heads} runs read back)")
