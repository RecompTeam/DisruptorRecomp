#!/usr/bin/env python3
"""Source contract for the key a save state carries: the generator's sources it is made of.

The key is pinned twice: over the files' bytes as the pin and the project's patches give them, and with CRLF read as
LF, which is how CMake reads a file on Windows before it hashes.
"""

import hashlib
import importlib.util
import re
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
KEY_OF_BYTES = "2d1942e0"
KEY_OF_TEXT = "2cece446"

specification = importlib.util.spec_from_file_location("framework_tool", ROOT / "tools" / "framework.py")
if specification is None or specification.loader is None:
    raise AssertionError("tools/framework.py cannot be loaded")
tool = importlib.util.module_from_spec(specification)
sys.modules[specification.name] = tool
specification.loader.exec_module(tool)


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def as_text(content: bytes) -> bytes:
    return content.replace(b"\r\n", b"\n")


def key(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()[:8]


listing = read("psxrecomp/runtime/codegen_hash_sources.cmake")
sources = re.findall(r"^\s*\$\{PSXRECOMP_CODEGEN_HASH_ROOT\}/(\S+?)\)?$", listing, re.MULTILINE)
require(len(sources) == 19, f"the key is made of 19 sources, the framework lists {len(sources)}")

project = tool.Project(ROOT)
checkout = tool.Checkout(ROOT / "psxrecomp", project.pin())
changed = {str(change.path): change for change in project.changes()}
stored, working = b"", b""
for source in sources:
    change = changed.get(source, tool.Change(PurePosixPath(source), False))
    pinned = checkout.pinned(change)
    if pinned is None:
        raise AssertionError(f"the pinned framework has no {source}")
    stored += tool.apply_patch(pinned, project.record(change).read_bytes()) if source in changed else pinned
    working += (ROOT / "psxrecomp" / source).read_bytes()
require(
    key(stored) == KEY_OF_BYTES and key(as_text(stored)) == KEY_OF_TEXT,
    f"the generator's sources give the keys {key(stored)} and {key(as_text(stored))}, not {KEY_OF_BYTES} and "
    f"{KEY_OF_TEXT}: every save state and capsule made so far would be refused. Change them only with the pin, "
    "and restate the keys here",
)
require(
    as_text(working) == as_text(stored),
    "the checkout's generator sources are not the pinned ones with the project's patches",
)
hashing = read("psxrecomp/runtime/hash_codegen.cmake")
require(
    'string(SHA256 _h "${_cat}")\nstring(SUBSTRING "${_h}" 0 8 _h8)\n' in hashing
    and "        h.codegen_hash  != (uint32_t)PSX_OVERLAY_CODEGEN_HASH ||\n"
    in read("psxrecomp/runtime/src/boot_state.c"),
    "the key is the first eight digits of the sources' hash, and a state of another key is refused",
)
require("    disruptor_contract(state_key)\n" in read("CMakeLists.txt"), "this contract must remain registered")

print("Disruptor save state key source contract: PASS")
