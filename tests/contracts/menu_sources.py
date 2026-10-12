"""The in-game menu's sources as one text: the shared header, then each source in the order the build lists it."""

from pathlib import Path

FOLDER = "src/game/ui/menu"


def menu_sources(root: Path) -> list[str]:
    """Returns the menu's files as paths from the repository root."""
    build = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    listed = [line.strip().strip('")').rsplit("}/", 1)[1] for line in build.splitlines() if f"/{FOLDER}/" in line]
    return [f"{FOLDER}/menu.h", *listed]


def menu_text(root: Path) -> str:
    """Returns the text of all of them, joined."""
    return "".join((root / name).read_text(encoding="utf-8") for name in menu_sources(root))
