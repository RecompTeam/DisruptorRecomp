#!/usr/bin/env python3
"""Source contract for the fast-forward key: B, asked of the game by the framework, and never while the name takes it."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig").replace("\r\n", "\n")


def faults(sources: dict[str, str]) -> list[str]:
    """What is wrong with the sources, empty when the key works as the contract says."""
    found = []
    main, host, entry = sources["main"], sources["host"], sources["entry"]
    if not (
        "#ifdef PSX_HAS_GAME_HOST\n        const int turbo_key = psx_game_fast_forward_key();\n#else\n"
        "        const int turbo_key = SDL_SCANCODE_RIGHTBRACKET;\n#endif\n"
        in main
        and "            turbo_key >= 0 && turbo_key < count && keys[turbo_key]) {" in main
        and "const Uint8* keys = SDL_GetKeyboardState(&count);" in main
    ):
        found.append("the framework must ask the game for the key and check it against the keyboard's size")
    if main.count("SDL_SCANCODE_RIGHTBRACKET") != 1:
        found.append("] must stay only as the key of a build without a game host")
    if "int psx_game_fast_forward_key(void);" not in sources["header"]:
        found.append("the game host header must declare the question")
    if (
        "int psx_game_fast_forward_key(void) {\n"
        "    if (disruptor_name_entry_taking_text() || disruptor_name_entry_withholds(SDL_SCANCODE_B)) return -1;\n"
        "    return SDL_SCANCODE_B;\n}\n"
    ) not in host:
        found.append("Disruptor must answer B, and none while the name rows take letters or hold B")
    if (
        'extern "C" int disruptor_name_entry_taking_text(void) { return taking_text() ? 1 : 0; }' not in entry
        or "    return scancode >= 0 && key < g_withheld.size() && g_withheld.test(key) ? 1 : 0;" not in entry
        or "    g_withheld.set(static_cast<std::size_t>(scancode));  /* the rows may close before the pad is next read */"
        not in entry
        or "        g_held.reset(static_cast<std::size_t>(scancode));\n        g_withheld.reset(static_cast<std::size_t>(scancode));\n"
        not in entry
    ):
        found.append("the name entry must say when it takes letters and which keys it still holds")
    if not (
        '{"B", "fast-forwards while held"},' in sources["keys"]
        and 'folded == "b") return true;' in sources["keys"]
        and '"]"' not in sources["keys"]
    ):
        found.append("the launcher must reserve B and list it, and leave ] to the player")
    if "- **B** fast-forwards while held, except on the save-name screen." not in sources["guide"]:
        found.append("the player's guide must name B")
    return found


sources = {
    "main": read("psxrecomp/runtime/src/main.cpp"),
    "header": read("psxrecomp/runtime/include/game_host.h"),
    "host": read("src/game/host.cpp"),
    "entry": read("src/game/input/name_entry.cpp"),
    "keys": read("src/launcher/key_bindings.cpp") + read("src/game/settings/key_layouts.h"),
    "guide": read("docs/PLAYING.md"),
}
said = faults(sources)
if said:
    raise AssertionError("; ".join(said))

seeded = {
    "a key the framework reads itself": ("main", "keys[turbo_key]) {", "keys[SDL_SCANCODE_B]) {"),
    "no bound on the scancode": ("main", "turbo_key >= 0 && turbo_key < count && ", "turbo_key >= 0 && "),
    "another key": ("host", "    return SDL_SCANCODE_B;\n", "    return SDL_SCANCODE_V;\n"),
    "B while the rows take letters": ("host", "disruptor_name_entry_taking_text() || ", ""),
    "B held over the rows' end": ("host", " || disruptor_name_entry_withholds(SDL_SCANCODE_B)", ""),
    "a held key past the bitset": ("entry", "key < g_withheld.size() && ", ""),
    "a typed key not held at once": (
        "entry",
        "    g_withheld.set(static_cast<std::size_t>(scancode));  /* the rows may close before the pad is next read */\n",
        "",
    ),
    "a key let go still held": (
        "entry",
        "        g_withheld.reset(static_cast<std::size_t>(scancode));\n        return 0;\n",
        "        return 0;\n",
    ),
    "B free to bind": ("keys", 'folded == "b") return true;', 'folded == "]") return true;'),
    "the old key in the guide": ("guide", "- **B** fast-forwards", "- **]** fast-forwards"),
}
for name, (where, old, new) in seeded.items():
    changed = dict(sources)
    if changed[where].count(old) != 1:
        raise AssertionError(f"the seed '{name}' no longer finds its text")
    changed[where] = changed[where].replace(old, new)
    if not faults(changed):
        raise AssertionError(f"the contract must refuse {name}")
print(f"Disruptor fast-forward key source contract: PASS ({len(seeded)} seeded changes refused)")
