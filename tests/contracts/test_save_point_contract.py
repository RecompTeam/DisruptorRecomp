#!/usr/bin/env python3
"""Source contract: a state is saved or loaded only at an interrupt check that was given its own resume address.

A check made without one (the RAM interpreter's are) left the compiled latch as an earlier call had it, and a state
saved there kept that address with the registers of other code: one save of 315 in the measured runs.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
POLL = "savestate_poll(cpu, s_compiled_interrupt_resume_pc);"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def between(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    return source[start : source.index(closing, start)]


def polls_are_the_four(source: str) -> bool:
    """The name is in the file six times: its declaration, the four polls and one comment of the framework's."""
    return len(re.findall(r"\bsavestate_poll\b", source)) == 6 and source.count(POLL) == 4


cmake = read("CMakeLists.txt")
manifest = read("PSXRECOMP_OVERLAY_FILES.txt").splitlines()
interrupts = read("psxrecomp-overlay/runtime/src/interrupts.c")
rule = read("psxrecomp-overlay/runtime/include/save_point.h")

require(
    "    add_test(NAME disruptor_save_point_contract\n" in cmake
    and "    add_test(NAME disruptor_save_point\n        COMMAND disruptor-save-point-test)\n" in cmake,
    "this contract and the rule's unit test are registered",
)
require(
    "runtime/src/interrupts.c" in manifest and "+runtime/include/save_point.h" in manifest,
    "the overlay replaces the framework's interrupt check and adds the rule's header",
)

require(
    "int savestate_pending(void);\n" in rule
    and "static inline int psx_save_poll_due(int periodic, int save_point, int *owed) {\n"
    "    if (!save_point) {\n"
    "        if (periodic) *owed = savestate_pending();\n"
    "        return 0;\n"
    "    }\n"
    "    if (!periodic && !*owed) return 0;\n"
    "    *owed = 0;\n"
    "    return 1;\n"
    "}\n"
    in rule,
    "a check without the mark never polls and owes a periodic poll that met a request, a marked one polls when "
    "periodic or owed",
)

require(
    '#include "save_point.h"\n' in interrupts
    and "static uint32_t s_compiled_interrupt_resume_pc = 0;\n" in interrupts
    and "static int s_resume_pc_given = 0;\n" in interrupts
    and len(re.findall(r"\bs_resume_pc_given\b", interrupts)) == 4
    and interrupts.count("s_resume_pc_given = 1;") == 1
    and interrupts.count("s_resume_pc_given = 0;") == 2,
    "the mark is declared off, set in one place, taken in one place and read nowhere else",
)

require(
    "void psx_check_interrupts(CPUState* cpu) {\n"
    "    const int save_point = s_resume_pc_given;\n"
    "    s_resume_pc_given = 0;\n" in interrupts,
    "a check takes the mark before anything else, so a check that leaves by a long jump cannot pass it on",
)

given = between(interrupts, "void psx_check_interrupts_at(CPUState* cpu, uint32_t resume_pc) {", "\n}\n")
require(
    "    uint32_t prev = s_compiled_interrupt_resume_pc;\n"
    "    s_compiled_interrupt_resume_pc = resume_pc;\n"
    "    s_resume_pc_given = 1;\n"
    "    psx_check_interrupts(cpu);"
    in given
    and given.rstrip().endswith("s_compiled_interrupt_resume_pc = prev;"),
    "the mark is set with the address, right before the one call it is for",
)

check = between(interrupts, "void psx_check_interrupts(CPUState* cpu) {", "\nvoid psx_check_interrupts_at(")
fast = (
    "            const int periodic = (++s_fast_maintenance & 0x3FFFu) == 0;\n"
    "            if (psx_save_poll_due(periodic, save_point, &s_save_poll_owed))\n"
    f"                {POLL}\n"
    "            if (periodic) debug_server_poll();\n"
    "            return;\n"
)
middle = (
    "        const int periodic = (total_checks & 0x3FFFu) == 0;\n"
    "        if (psx_save_poll_due(periodic, save_point, &s_save_poll_owed))\n"
    f"            {POLL}\n"
    "        if (periodic) debug_server_poll();\n"
    "        goto irq_deliver_eval;\n"
)
last = f"    if (!in_exception && psx_save_poll_due(1, save_point, &s_save_poll_owed))\n        {POLL}\n"
require(
    check.count(fast) == 2 and check.count(middle) == 1 and check.count(last) == 1,
    "the two fast paths and the middle path poll periodically, the end of the check at every check outside an exception",
)
require(
    polls_are_the_four(interrupts)
    and check.count(POLL) == 4
    and len(re.findall(r"\bpsx_save_poll_due\b", interrupts)) == 4
    and len(re.findall(r"\bsave_point\b", check)) == 5
    and len(re.findall(r"\bs_save_poll_owed\b", interrupts)) == 5
    and "static int s_save_poll_owed = 0;\n" in interrupts
    and "savestate_pending" not in interrupts,
    "every poll goes through the rule with this check's mark and passes the address this check was given",
)
flushed = "    s_resume_pc_given = 0;\n    psx_cyc_batch_flush();\n"
require(
    interrupts.count(flushed) == 1
    and not polls_are_the_four(
        interrupts.replace(flushed, flushed + "    savestate_poll (cpu, s_compiled_interrupt_resume_pc);\n")
    )
    and not polls_are_the_four(interrupts.replace(flushed, flushed + "    savestate_poll(cpu, check_pc);\n")),
    "a fifth poll is seen however it is spelled",
)

print("Disruptor save point source contract: PASS")
