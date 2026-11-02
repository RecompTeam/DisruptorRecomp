#!/usr/bin/env python3
"""Source contract: a state is saved only at an interrupt check that was given its own resume address.

A check made without one (the RAM interpreter's are) sees the latch an earlier check or a restore left, and a state
saved there kept that address with the registers of other code: one save of 315 in the measured runs.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def between(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    return source[start : source.index(closing, start)]


cmake = read("CMakeLists.txt")
interrupts = read("psxrecomp/runtime/src/interrupts.c")
require("    disruptor_contract(save_point)\n" in cmake, "this contract is registered")
require("runtime/src/interrupts.c" in read("framework/FILES.txt").split(), "the rule is a patch on the framework's check")
check = between(interrupts, "void psx_check_interrupts(CPUState* cpu) {", "\nvoid psx_check_interrupts_at(")
require(
    check.startswith(
        "void psx_check_interrupts(CPUState* cpu) {\n"
        "    const int save_point = s_resume_pc_given || g_dirty_safe_resume_pc != 0u;\n"
        "    s_resume_pc_given = 0;\n"
    ),
    "a check takes the mark before anything else, so a check that leaves early or by a long jump cannot pass it on",
)
at = between(interrupts, "void psx_check_interrupts_at(CPUState* cpu, uint32_t resume_pc) {", "\n}\n")
require(
    "    s_compiled_interrupt_resume_pc = resume_pc;\n    s_resume_pc_given = 1;\n    psx_check_interrupts(cpu);" in at
    and len(re.findall(r"\bs_resume_pc_given\b", interrupts)) == 4,
    "the mark is set with the address, right before the one call it is for, and read nowhere else",
)
polls = re.findall(r"^.*\bsavestate_poll\(cpu,.*$", interrupts, re.MULTILINE)
require(
    len(polls) == 5
    and sum("if (save_point) savestate_poll(cpu, " in poll for poll in polls) == 4
    and "    if (save_point && !in_exception && g_psx_savestate_pending) {\n" in interrupts
    and "    poll_staged_user_snapshot(cpu, save_point);\n" in check,
    "every save poll of the check waits for a check with an address of its own",
)

print("Disruptor save point source contract: PASS")
