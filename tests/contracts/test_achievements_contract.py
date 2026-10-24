#!/usr/bin/env python3
"""Source contract: the RetroAchievements sign-in token stays on the machine.

The launcher's problem report carries a fixed list of files, settings.toml among them, so the token lives in its own
file that the list never names, and no settings writer ever writes it.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def gathered(report: str) -> list[str]:
    """The file names of the report's list."""
    found = re.search(r"constexpr std::array<const char\*, \d+> gathered = \{(.*?)\};", report, re.DOTALL)
    require(found is not None, "the report keeps its files in one list")
    return re.findall(r'"([^"]+)"', found.group(1))


def account_file(header: str) -> str:
    found = re.search(r'inline constexpr const char\* kAccountFile = "([^"]+)";', header)
    require(found is not None, "the account file has one name")
    return found.group(1)


report = read("src/launcher/report.cpp")
account = account_file(read("src/game/achievements/account.h"))
require(account == "retroachievements.toml", "the account is kept in retroachievements.toml")
require(account not in gathered(report) and "settings.toml" in gathered(report), "a problem report never takes it")
require(
    account in gathered(report.replace('"settings.toml",', f'"settings.toml", "{account}",')),
    "and the check would see it if the list named it",
)
for writer in ("src/game/settings/saved.cpp", "src/launcher/launcher_settings.cpp"):
    require("token" not in read(writer), f"{writer} writes no token into settings.toml")
writes = read("src/game/achievements/account.cpp")
require(
    'const fs::path target = folder / kAccountFile;' in writes and writes.count("std::ofstream") == 1,
    "the token is written to the account file alone",
)
print("Disruptor achievements source contract: PASS")
