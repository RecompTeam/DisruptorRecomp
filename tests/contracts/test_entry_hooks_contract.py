"""Every function entry hook the game registers sits at an address the code generator calls hooks at.

The generator puts a hook call only into the functions `mod_function_entry_funcs` lists. A plugin registered at any other
address is taken without an error and never runs.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REGISTER = re.compile(
    r"psx_mod_register_function_entry_plugin\(\s*\"[^\"]+\"\s*,\s*([A-Za-z_:][A-Za-z0-9_:]*|0x[0-9A-Fa-f]+[uU]?)\s*,"
)
CALL = re.compile(r"\bpsx_mod_register_function_entry_plugin\s*\(")
CONSTANT = r"\b{}\s*=\s*(0x[0-9A-Fa-f]+)u?\b"
TABLE = r"\b{}\s*=\s*\{{([^}}]*)\}}"
LOOP = r"for\s*\(\s*const\s+uint32_t\s+{}\s*:\s*(\{{[^}}]*\}}|[A-Za-z_][A-Za-z0-9_]*)\s*\)"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def listed(config: str) -> set[int]:
    found = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", (ROOT / config).read_text("utf-8"))
    require(found is not None, f"{config} lists the functions hooks are called at")
    return {int(value, 16) for value in re.findall(r"0x[0-9A-Fa-f]+", found.group(1))}


class Sources:
    def __init__(self) -> None:
        self.paths = sorted([*(ROOT / "src").rglob("*.cpp"), *(ROOT / "src").rglob("*.h")])
        self.text = {path: path.read_text("utf-8") for path in self.paths}

    def near_first(self, source: Path) -> list[Path]:
        return [source] + [path for path in self.paths if path != source]

    def constant(self, name: str, source: Path) -> int:
        for path in self.near_first(source):
            values = set(re.findall(CONSTANT.format(re.escape(name)), self.text[path]))
            if values:
                require(len(values) == 1, f"{name} has one value in {path.relative_to(ROOT)}")
                return int(values.pop(), 16)
        raise AssertionError(f"{name} used in {source.relative_to(ROOT)} has no address to be found")

    def values(self, token: str, source: Path) -> list[int]:
        """The addresses a registration's address argument stands for: a literal, a constant, or a loop over a table."""
        if token.lower().startswith("0x"):
            return [int(token.rstrip("uU"), 16)]
        name = token.split("::")[-1]
        loop = re.search(LOOP.format(re.escape(name)), self.text[source])
        if not loop:
            return [self.constant(name, source)]
        items = loop.group(1)
        if not items.startswith("{"):
            table = re.search(TABLE.format(re.escape(items)), self.text[source])
            require(table is not None, f"the table {items} in {source.relative_to(ROOT)} is found")
            items = table.group(1)
        out = []
        for item in (part.strip() for part in items.strip("{}").split(",")):
            out.append(
                int(item.rstrip("uU"), 16)
                if item.lower().startswith("0x")
                else self.constant(item.split("::")[-1], source)
            )
        return out


def main() -> int:
    game, wide = listed("game.toml"), listed("game-widescreen.toml")
    require(game == wide, "both game configurations call hooks at the same functions")
    sources = Sources()
    registered: dict[int, set[str]] = {}
    for source in sources.paths:
        calls, read = len(CALL.findall(sources.text[source])), REGISTER.findall(sources.text[source])
        require(
            calls == len(read),
            f"every hook registration in {source.relative_to(ROOT)} names its address so it can be read",
        )
        for token in read:
            for address in sources.values(token, source):
                registered.setdefault(address, set()).add(str(source.relative_to(ROOT)))
    require(len(registered) >= 10, "the registrations are found")
    missing = sorted(address for address in registered if address not in game)
    require(
        not missing,
        "hooks registered where the generator calls none: "
        + ", ".join(f"0x{address:08X} ({', '.join(sorted(registered[address]))})" for address in missing),
    )
    print(f"entry hooks contract: {len(registered)} hooked functions, all called by the generated code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
