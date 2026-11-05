"""The game's audited idle-skip sites: the same list in both configs, each a real interrupt-check point of the translated
code, and the framework skips a loop of several checks a pass without a counter in memory only at such a site with a
register counting down.

An unlisted loop is never skipped by the store-counter extension, and a listed PC the generated code never checks at
would audit nothing. The framework's refusal of multi-check loops without a counter (they moved R4's boot timing) must
stay in force everywhere else.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sites(config: str) -> list[int]:
    text = (ROOT / config).read_text("utf-8")
    require("idle_skip = true" in text and "idle_skip_store_counters = true" in text, f"{config} turns the skip on")
    found = re.search(r"^idle_skip_store_counter_sites = \[([^\]]*)\]$", text, re.MULTILINE)
    require(found is not None, f"{config} lists its audited sites")
    return [int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", found.group(1))]


def main() -> int:
    listed = sites("game.toml")
    require(listed == sites("game-widescreen.toml"), "both configs list the same audited sites")
    require(len(listed) == len(set(listed)) and len(listed) <= 16, "the sites are distinct and fit the framework's 16")
    translated = sorted((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
    require(
        translated or "--require-artifacts" not in sys.argv, "the translated code is there to check the sites against"
    )
    checks = set()
    for path in translated:
        checks |= {
            int(value, 16)
            for value in re.findall(
                r"psx_check_interrupts_at\(cpu, 0x([0-9A-F]{8})u\)", path.read_text("utf-8", errors="replace")
            )
        }
    require(not translated or checks, "the translated code's interrupt-check points were read")
    if translated:
        missing = [f"0x{pc:08X}" for pc in listed if pc not in checks]
        require(not missing, f"every audited site is an interrupt-check point of the translated code: {missing}")
    cycles = (ROOT / "psxrecomp/runtime/src/psx_cycles.c").read_text("utf-8")
    require(
        "    if (s_idle_iter_edges && !s_idle_mem_progress &&\n"
        "        !(s_idle_progress_reg > 0 && idle_site_listed(s_idle_pc))) return;" in cycles,
        "a loop of several checks a pass without a counter in memory is skipped only at a listed site counting a register",
    )
    told = "all checked at" if translated else "not checked: no translated code"
    print(f"idle skip contract: PASS ({len(listed)} sites, {told})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
