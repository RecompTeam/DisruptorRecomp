"""The precision word map and the interrupt check's short paths, which nothing on screen shows when they go wrong.

A store skips the provenance invalidation only for a word whose bit is clear, so every word that gets a tag must get its
bit, a bit may be cleared only where no current tag is left, and a new generation clears every bit a page bit names. A
stale tag would still be refused by its packed value at the lookup, so a broken map changes no picture. The idle
detector's restart at a new PC must leave the state its last branch leaves, and the interrupt check asks netplay a
second time only while a network game runs.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    header = (ROOT / "psxrecomp/runtime/include/gte_precision.h").read_text("utf-8")
    gte = (ROOT / "psxrecomp/runtime/src/gte.cpp").read_text("utf-8")
    cycles = (ROOT / "psxrecomp/runtime/src/psx_cycles.c").read_text("utf-8")
    interrupts = (ROOT / "psxrecomp/runtime/src/interrupts.c").read_text("utf-8")
    require(
        "    return (int)((g_gte_precision_ram_words[word >> 5] >> (word & 31u)) & 1u);\n" in header
        and "    return (int)((g_gte_precision_dma_words[word >> 5] >> (word & 31u)) & 1u);\n" in header
        and "ram_pages[" not in header.split("static inline int gte_precision_ram_live", 1)[1].split("}", 1)[0],
        "the store gates read the word maps",
    )
    store = gte.split("static void precision_store_projection(", 1)[1].split("\n}\n", 1)[0]
    require(
        "        g_gte_precision_ram_pages[physical >> 17] |= 1u << ((physical >> 12) & 31u);\n"
        "        g_gte_precision_ram_words[physical >> 7] |= 1u << ((physical >> 2) & 31u);\n"
        in store
        and "        g_gte_precision_dma_pages[offset >> 17] |= 1u << ((offset >> 12) & 31u);\n"
        "        g_gte_precision_dma_words[offset >> 7] |= 1u << ((offset >> 2) & 31u);\n"
        in store
        and gte.count("tag.generation = s_precision_generation;") == 1,
        "the one place a tag is made current sets its word's bit and its page's bit",
    )
    invalidate = gte.split('extern "C" void gte_precision_invalidate_word(uint32_t addr) {', 1)[1].split("\n}\n", 1)[0]
    require(
        invalidate.index("        tag.generation = 0;\n")
        < invalidate.index("g_gte_precision_ram_words[physical >> 7] &=")
        and "    if (s_speculative_depth != 0 || s_gte_replay_sandbox || !s_precision_tracking) return;\n"
        in invalidate,
        "a word's bit is cleared only by an invalidation that ran, after its tag",
    )
    require(
        "    precision_live_words_clear(g_gte_precision_ram_pages, 16u, g_gte_precision_ram_words);\n"
        "    precision_live_words_clear(g_gte_precision_dma_pages, 64u, g_gte_precision_dma_words);\n"
        in gte
        and "            if (bits & 1u) std::memset(words + (index * 32u + bit) * 32u, 0, 32u * sizeof(uint32_t));\n"
        in gte,
        "a new generation clears the word bits of every page a page bit names",
    )
    require(
        "    if (check_pc != s_idle_pc) {\n        s_idle_pc = check_pc;\n        idle_reset_progress();\n"
        "        s_idle_have_snap = 0;\n        s_idle_store_addr = 0xFFFFFFFFu;\n        s_idle_edges = 0;\n"
        "        s_idle_last_cycle = psx_cycle_count;\n        s_idle_last_stores = g_guest_store_count;\n"
        "        s_idle_last_mmio = g_mmio_access_count;\n        return;\n    }\n"
        in cycles
        and cycles.index("    if (check_pc != s_idle_pc) {\n        s_idle_pc = check_pc;")
        > cycles.index("        s_idle_edges++;\n        return;\n    }\n")
        and "        s_idle_pc = check_pc;\n        idle_reset_progress();\n        s_idle_have_snap = 0;\n"
        "        s_idle_store_addr = 0xFFFFFFFFu;\n    }\n"
        in cycles,
        "the idle detector restarts at a new PC as its last branch does, after the inner-edge case",
    )
    require(
        "    if (!in_exception && np_active && psx_netplay_active()) {" in interrupts
        and "    const int np_active = psx_netplay_active();\n" in interrupts,
        "the interrupt check asks netplay again only while a network game runs",
    )
    print("precision words contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
