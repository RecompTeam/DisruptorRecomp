"""The launcher's keyboard and gamepad paths, driven through its hidden picture mode on an empty data folder.

Run by ctest with the launcher's path. Exit 77, a skip, where the launcher finds no display to draw on.
"""

import itertools
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

if sys.platform == "win32":
    import _winapi

SKIP = 77
NO_DISPLAY = 5


@dataclass(frozen=True)
class Step:
    """Where the cursor was after one step of --input, as the launcher's --nav-log has it."""

    name: str
    window: str
    place: tuple[int, int, int, int]
    shown: bool
    popup: str
    view: str

    @property
    def in_sidebar(self) -> bool:
        return "/sidebar_" in self.window

    @property
    def in_rows(self) -> bool:
        return "/rows_" in self.window

    @property
    def scroll(self) -> int:
        found = re.search(r" scroll (\d+)", self.view)
        return int(found.group(1)) if found else -1


class Skipped(Exception):
    """The launcher could not make a window here."""


def drive(
    launcher: Path,
    folder: Path,
    name: str,
    steps: str,
    page: int = 0,
    screen: str = "settings",
    tab: int = 0,
    pad: bool = False,
    data: str = "data",
) -> list[Step]:
    log = folder / f"{name}.txt"
    arguments = [
        str(launcher),
        "--screenshot",
        str(folder / f"{name}.bmp"),
        "--screen",
        screen,
        "--page",
        str(page),
        "--tab",
        str(tab),
        "--size",
        "1280x720",
        "--nav-log",
        str(log),
        "--input",
        steps,
    ]
    if pad:
        arguments.append("--virtual-pad")
    environment = dict(os.environ, PSX_USER_DIR=str(folder / data))
    done = subprocess.run(arguments, env=environment, capture_output=True, timeout=120, check=False)
    if done.returncode == NO_DISPLAY:
        raise Skipped(done.stderr.decode(errors="replace"))
    if done.returncode != 0:
        raise AssertionError(
            f"{name}: the launcher ended with {done.returncode}: {done.stderr.decode(errors='replace')}"
        )
    found = []
    for line in log.read_text(encoding="utf-8").splitlines():
        step, window, place, cursor, popup, view = line.split("\t")
        x, y, width, height = (int(number) for number in place.split())
        found.append(Step(step, window, (x, y, width, height), cursor == "cursor", popup, view))
    return found


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def check(launcher: Path, folder: Path) -> None:
    (folder / "data").mkdir()
    walk = drive(launcher, folder, "tab", "Tab Tab Escape Down Space", page=1)
    require(
        walk[0].in_rows and walk[1].in_rows and walk[0].place != walk[1].place,
        "Tab reaches the page's own controls, one after another",
    )
    require(
        walk[2].in_sidebar and "page 1" in walk[2].view, "Escape takes the cursor from the page back to the side bar"
    )
    require(walk[3].in_sidebar and "page 1" in walk[3].view, "Down moves along the side bar without opening a page")
    require(
        "page 2" in walk[4].view and not walk[4].in_sidebar, "Space on a side bar entry opens its page and goes into it"
    )

    # From Enhancements down the side bar: Controls, Audio, Experimental, Troubleshooting, Saves folder, Bring saves, Help.
    right = drive(
        launcher, folder, "right", "Tab Escape Down Down Down Down Down Down Down Right Escape Down Right", page=1
    )
    require(right[9].view.startswith("help") and not right[9].in_sidebar, "Right on Help opens it and goes into it")
    require(right[12].view.startswith("about") and not right[12].in_sidebar, "Right on About opens it and goes into it")

    # The settings column stands in the middle of the page: the wheel over its margin scrolls the card as the card does.
    wheel = drive(launcher, folder, "wheel", "Wheel:255,400,-1 Wheel:700,400,-1", page=1)
    require(
        wheel[0].scroll > 0 and wheel[1].scroll == 2 * wheel[0].scroll,
        "the wheel over the page's margin scrolls the card a step as long as the wheel over the card",
    )

    keys = drive(launcher, folder, "keys", "Tab Tab Tab Tab Tab Tab Tab Space K Tab", page=2, tab=3)
    require(" key up" in keys[7].view, "Space on the Move forward button waits for a key")
    require(" key " not in keys[8].view, "the next key is taken by the button that waited")
    require(
        keys[9].shown and keys[9].in_rows and keys[9].place != keys[8].place and keys[9].place[1] == keys[8].place[1],
        "Tab after a key was taken moves to the next key of the row: the button kept its place in the order",
    )
    written = (folder / "data" / "keybinds.ini").read_text(encoding="utf-8")
    require(re.search(r"^up\s*=\s*K$", written, re.MULTILINE) is not None, "the key taken is written for Move forward")

    # Audio's stops: the volume slider, Bass's three parts, two switches, then the Reset link.
    reset = drive(launcher, folder, "reset", "Tab Tab Tab Tab Tab Tab Tab Space Escape", page=3)
    require(reset[7].popup != "-", "Space on the page's Reset link opens its question")
    require(reset[8].popup == "-" and not reset[8].in_sidebar, "Escape closes the question without an answer")

    # With settings.toml unreadable Troubleshooting keeps its report: Start over, the file's folder, then Report a problem.
    (folder / "broken").mkdir()
    (folder / "broken" / "settings.toml").write_text("[video\nsupersampling = = 8\n", encoding="utf-8")
    broken = drive(launcher, folder, "broken", "Tab Tab Tab Space", page=5, data="broken")
    require(
        broken[3].view.endswith(" held report"),
        "Report a problem is reached and pressed while settings.toml cannot be read",
    )

    # Down on a tab of Controls goes into its card, also where no control stands right under the tab.
    for tab in range(4):
        down = drive(
            launcher, folder, f"down{tab}", " ".join(["Tab"] * (tab + 1)) + " Pad:Down", page=2, tab=tab, pad=True
        )
        require(
            down[tab + 1].in_rows and not down[tab + 1].in_sidebar,
            f"Down on the Controls page's tab {tab} goes into its card, not to the side bar",
        )

    # From Enhancements: a release's Experimental page has no controls to go into.
    pad = drive(launcher, folder, "pad", "Pad:Down Pad:B Pad:Down Pad:A Pad:RB", page=1, pad=True)
    require(pad[0].in_rows, "the D-pad reaches the page's controls")
    require(pad[1].in_sidebar, "B takes the cursor back to the side bar")
    require("page 2" in pad[3].view and not pad[3].in_sidebar, "A on a side bar entry opens its page and goes into it")
    require("page 3" in pad[4].view and pad[4].in_sidebar, "the right bumper steps to the next page")

    steps_to_fire = 13
    steps = " ".join(["Tab"] * steps_to_fire) + " Pad:A Pad:A Pad:A Pad:B Pad:A Pad:Down Pad:B Pad:Down"
    capture = drive(launcher, folder, "capture", steps, page=2, tab=1, pad=True)[steps_to_fire - 1 :]
    first = capture[0].place, capture[0].scroll
    require(" button " in capture[1].view, "A on a gamepad button waits for the new button")
    require(
        " button " not in capture[2].view and (capture[2].place, capture[2].scroll) == first,
        "A taken for the button starts no capture again and leaves the cursor where it was",
    )
    require(" button " in capture[3].view, "a new press of A waits again")
    require(
        " button " not in capture[4].view
        and (capture[4].place, capture[4].scroll) == first
        and not capture[4].in_sidebar,
        "B taken for the button does not also take the cursor back to the side bar",
    )
    require(
        " button " in capture[6].view and (capture[6].place, capture[6].scroll) == first,
        "the D-pad is no button to take and moves nothing",
    )
    require(
        " button " not in capture[7].view and (capture[8].place, capture[8].scroll) != first,
        "after B is taken and let go the D-pad moves the cursor again",
    )


def check_rows(launcher: Path, folder: Path) -> None:
    """--rows-log names each setting row of the picture with its place, as the release notes' marked pictures need."""
    log = folder / "rows.txt"
    arguments = [str(launcher), "--screenshot", str(folder / "rows.bmp"), "--screen", "settings", "--page", "0"]
    arguments += ["--size", "1280x720", "--rows-log", str(log)]
    done = subprocess.run(
        arguments,
        env=dict(os.environ, PSX_USER_DIR=str(folder / "data")),
        capture_output=True,
        timeout=120,
        check=False,
    )
    require(done.returncode == 0, f"rows: the launcher ended with {done.returncode}")
    places: dict[str, tuple[int, ...]] = {}
    whole: set[str] = set()
    for line in log.read_text(encoding="utf-8").splitlines():
        key, place, shown = line.split("\t")
        places[key] = tuple(int(number) for number in place.split())
        if shown == "whole":
            whole.add(key)
    require("disruptor.resolution" in whole, "Resolution's row is in the picture whole")
    ordered = sorted(places.values(), key=lambda place: place[1])
    require(
        all(0 <= left < right <= 1280 and 0 <= top < bottom <= 720 for left, top, right, bottom in ordered),
        "every row lies inside the picture",
    )
    require(
        all(above[3] <= below[1] + 1 for above, below in itertools.pairwise(ordered)),
        "rows follow one another without overlapping",
    )


def check_profile_parent(launcher: Path, folder: Path) -> None:
    target = folder / "versions" / "v2"
    target.mkdir(parents=True)
    alias = folder / "library" / "current"
    alias.parent.mkdir()
    if sys.platform == "win32":
        _winapi.CreateJunction(str(target), str(alias))
    else:
        alias.symlink_to(target, target_is_directory=True)
    for mode in ("cli", "env"):
        chosen = alias / ".." / mode
        args = [str(launcher), "--check"]
        env = os.environ.copy()
        if mode == "cli":
            args += ["--data-dir", str(chosen)]
        else:
            env["PSX_USER_DIR"] = str(chosen)
        done = subprocess.run(args, env=env, capture_output=True, timeout=30, check=False)
        require(
            done.returncode == 1 and b"No supported disc image" in done.stderr,
            f"expected an empty profile via {mode}, got exit {done.returncode}: {done.stderr.decode(errors='replace')}",
        )
        require(
            (target.parent / mode).is_dir() and not (alias.parent / mode).exists(), "use the linked directory's parent"
        )


def main() -> int:
    launcher = Path(sys.argv[1])
    with tempfile.TemporaryDirectory() as folder:
        check_profile_parent(launcher, Path(folder))
        try:
            check(launcher, Path(folder))
            check_rows(launcher, Path(folder))
        except Skipped as reason:
            print(f"launcher navigation: skipped, no display ({reason})")
            return SKIP
    print("launcher navigation: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
