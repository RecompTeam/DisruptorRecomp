"""The recording backend closes its open group of records before anything else takes the render queue.

While a group is open the ring holds it as a pending record: another record, a frame marker or a sync point written then
would land on it. So every direct `rt_cmd_begin`, `rt_frame_end` and `rt_acquire` call of the backend, except the one
that opens a group, comes right after `rth_group_close()`, and the queue stops only after a sync point.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BACKEND = ROOT / "psxrecomp/runtime/src/gpu_gl_render_thread.c.inc"
CALL = re.compile(r"\brt_(cmd_begin|frame_end|acquire|drain|stop)\s*\(")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    lines = BACKEND.read_text("utf-8").splitlines()
    calls = [(index, CALL.search(line).group(1)) for index, line in enumerate(lines) if CALL.search(line)]
    require(len(calls) >= 5, "the backend's queue calls were found")
    for index, name in calls:
        line = lines[index]
        if name == "cmd_begin" and "RTH_GROUP" in line:
            continue
        if name == "stop":
            require(
                any('gl_rth_acquire("stop")' in lines[at] for at in range(max(0, index - 2), index)),
                "the queue stops only after a sync point, which closes the group",
            )
            continue
        before = " ".join(lines[max(0, index - 3) : index])
        require("rth_group_close();" in before, f"line {index + 1} ({name}) comes right after rth_group_close()")
    opened = [index for index, name in calls if name == "cmd_begin" and "RTH_GROUP" in lines[index]]
    require(len(opened) == 1, "one place opens a group")
    text = "\n".join(lines)
    require(
        "case RTH_GROUP:" in text and "gl_rth_exec(user, sub, sub + 1);" in text,
        "the render thread replays a group's records one by one through the same dispatch",
    )
    print("render groups contract: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
