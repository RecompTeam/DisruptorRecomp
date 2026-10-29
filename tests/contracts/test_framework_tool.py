#!/usr/bin/env python3
"""Test of tools/framework.py on a small framework made here: apply, refresh, check and what each refuses."""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "framework.py"
PLAIN = b"int a;\nint b;\nint c;\nint d;\nint e;\n"
MIXED = b"one\r\ntwo\r\nthree\nfo\rur\r\nfive\r\n"
PLAIN_EDITED = b"int a;\nint b2;\nint c;\nint d;\nint e;\nint f;\n"
MIXED_EDITED = b"one\r\ntwo\r\nours\nfo\rur\r\nfive\r\n"
ADDED = b"#pragma once\n"
LISTED = "runtime/src/plain.c\nruntime/src/mixed.c\n+runtime/include/added.h\n"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def git(folder: Path, *arguments: str) -> str:
    identity = ["-c", "user.name=test", "-c", "user.email=test@example.invalid", "-c", "core.autocrlf=false"]
    done = subprocess.run(["git", "-C", str(folder), *identity, *arguments], capture_output=True, text=True, check=True)
    return done.stdout.strip()


def tool(project: Path, checkout: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, "-B", str(TOOL), *arguments, "--project", str(project), "--framework", str(checkout)]
    return subprocess.run(command, capture_output=True, text=True, check=False)


def main() -> int:
    with tempfile.TemporaryDirectory() as folder:
        checkout, project = Path(folder) / "checkout", Path(folder) / "project"
        (checkout / "runtime" / "src").mkdir(parents=True)
        (checkout / "runtime" / "include").mkdir()
        (checkout / "runtime" / "runtime.cmake").write_bytes(b"# fixture\n")
        plain, mixed = checkout / "runtime" / "src" / "plain.c", checkout / "runtime" / "src" / "mixed.c"
        added = checkout / "runtime" / "include" / "added.h"
        plain.write_bytes(PLAIN)
        mixed.write_bytes(MIXED)
        git(checkout, "init", "--quiet")
        git(checkout, "add", "--all")
        git(checkout, "commit", "--quiet", "--message", "pinned")
        (project / "framework").mkdir(parents=True)
        pinned = git(checkout, "rev-parse", "HEAD") + "\n"
        (project / "framework" / "PIN").write_text(pinned, encoding="utf-8")
        manifest = project / "framework" / "FILES.txt"
        manifest.write_text("# a comment\n" + LISTED, encoding="utf-8")

        plain.write_bytes(PLAIN_EDITED)
        mixed.write_bytes(MIXED_EDITED)
        added.write_bytes(ADDED)
        saved = tool(project, checkout, "refresh")
        patch = project / "framework" / "patches" / "runtime" / "src" / "mixed.c.patch"
        require(
            saved.returncode == 0
            and (project / "framework" / "files" / "runtime" / "include" / "added.h").read_bytes() == ADDED
            and b"\n two\r\n-three\n+ours\n fo\rur\r\n five\r\n" in patch.read_bytes()
            and tool(project, checkout, "check").returncode == 0,
            "an edit made in the checkout is saved as a patch that keeps each line's own ending",
        )

        plain.write_bytes(PLAIN)
        mixed.write_bytes(MIXED)
        require(
            git(checkout, "apply", "--check", str(patch)) == ""
            and git(checkout, "apply", "--check", str(patch.with_name("plain.c.patch"))) == "",
            "a saved patch is one that git applies to the pinned file",
        )
        added.unlink()
        require(tool(project, checkout, "check").returncode != 0, "a checkout with pinned files is not in step")
        applied = tool(project, checkout, "apply")
        require(
            applied.returncode == 0
            and plain.read_bytes() == PLAIN_EDITED
            and mixed.read_bytes() == MIXED_EDITED
            and added.read_bytes() == ADDED
            and tool(project, checkout, "check").returncode == 0,
            "apply gives the saved state back byte for byte",
        )
        written = [path.stat().st_mtime_ns for path in (plain, mixed, added)]
        require(
            tool(project, checkout, "apply").returncode == 0
            and written == [path.stat().st_mtime_ns for path in (plain, mixed, added)],
            "apply leaves alone a file that already holds what it should, so a build does not compile it again",
        )

        plain.write_bytes(PLAIN_EDITED + b"int unsaved;\n")
        refused = tool(project, checkout, "apply")
        require(
            refused.returncode != 0
            and "runtime/src/plain.c" in refused.stderr
            and plain.read_bytes().endswith(b"int unsaved;\n")
            and tool(project, checkout, "check").returncode != 0,
            "apply does not overwrite an edit that refresh has not saved, and check reports it",
        )
        require(
            tool(project, checkout, "apply", "--discard").returncode == 0 and plain.read_bytes() == PLAIN_EDITED,
            "apply --discard drops such an edit",
        )

        stray = project / "framework" / "patches" / "runtime" / "src" / "stray.c.patch"
        stray.write_bytes(patch.read_bytes())
        require(
            tool(project, checkout, "check").returncode != 0 and tool(project, checkout, "apply").returncode != 0,
            "a patch that FILES.txt does not list is refused",
        )
        stray.unlink()

        whole = patch.read_bytes()
        patch.write_bytes(whole.replace(b"+ours\n", b"+theirs\n"))
        mixed.write_bytes(MIXED)
        altered = tool(project, checkout, "apply")
        require(
            altered.returncode != 0
            and "did not give the content it names" in altered.stderr
            and mixed.read_bytes() == MIXED,
            "a patch whose text was altered is not applied",
        )
        patch.write_bytes(whole.replace(b" two\r\n", b" two\n"))
        unmatched = tool(project, checkout, "apply")
        require(
            unmatched.returncode != 0 and "does not match at line 2" in unmatched.stderr,
            "a hunk that does not match the pinned file is refused, and the line is named",
        )
        patch.write_bytes(whole)
        require(tool(project, checkout, "apply").returncode == 0, "the fixture is whole again")

        marker = checkout / "runtime" / "runtime.cmake"
        marker.write_bytes(b"# changed\n")
        unlisted = tool(project, checkout, "check")
        require(
            unlisted.returncode != 0 and "not in FILES.txt: runtime/runtime.cmake" in unlisted.stderr,
            "a framework file changed in the checkout and not listed is reported",
        )
        marker.write_bytes(b"# fixture\n")
        require(tool(project, checkout, "check").returncode == 0, "the fixture is in step again")

        for lines, said in (
            (LISTED.replace("+runtime/include/added.h", "runtime/include/added.h"), "is not in the framework"),
            (LISTED.replace("runtime/src/plain.c", "+runtime/src/plain.c"), "is marked '+' but the framework has it"),
            (LISTED + "../outside.c\n", "unsafe path"),
            (LISTED + "+..\\outside.h\n", "unsafe path"),
            (LISTED + "+C:/outside.h\n", "unsafe path"),
            (LISTED + "runtime/src/plain.c\n", "lists a file twice"),
            ("# nothing\n", "lists no file"),
        ):
            manifest.write_text(lines, encoding="utf-8")
            refused = tool(project, checkout, "check")
            require(refused.returncode != 0 and said in refused.stderr, f"a list is refused when it {said}")
        manifest.write_text(LISTED, encoding="utf-8")

        plain.write_bytes(PLAIN)
        require(
            tool(project, checkout, "refresh").returncode != 0,
            "refresh refuses a listed file that is the pinned one again",
        )

        plain.write_bytes(b"int other;\n")
        git(checkout, "commit", "--quiet", "--all", "--message", "moved")
        (project / "framework" / "PIN").write_text(git(checkout, "rev-parse", "HEAD") + "\n", encoding="utf-8")
        moved = tool(project, checkout, "check")
        require(
            moved.returncode != 0 and "made against other content" in moved.stderr,
            "a patch is not applied to a pinned file it was not made against",
        )
        (project / "framework" / "PIN").write_text(pinned, encoding="utf-8")
        elsewhere = tool(project, checkout, "check")
        require(
            elsewhere.returncode != 0 and "not at the pinned" in elsewhere.stderr,
            "a checkout that is at another revision than the pinned one is reported",
        )

    listed = [line for line in (ROOT / "framework" / "FILES.txt").read_text(encoding="utf-8").splitlines()]
    entries = [line.strip() for line in listed if line.strip() and not line.startswith("#")]
    patches = {
        path.relative_to(ROOT / "framework" / "patches").as_posix()
        for path in (ROOT / "framework" / "patches").rglob("*")
        if path.is_file()
    }
    files = {
        path.relative_to(ROOT / "framework" / "files").as_posix()
        for path in (ROOT / "framework" / "files").rglob("*")
        if path.is_file()
    }
    require(
        len(entries) == 90
        and patches == {f"{line}.patch" for line in entries if not line.startswith("+")}
        and files == {line[1:] for line in entries if line.startswith("+")}
        and len(patches) == 56
        and len(files) == 34
        and "framework/patches/** -text\n" in (ROOT / ".gitattributes").read_text(encoding="utf-8"),
        "the project lists 90 changed files, with a patch for each of 56 and 34 added whole, and stores patches byte for byte",
    )
    for script, call in (
        ("build.ps1", "python (Join-Path $Root 'tools/framework.py') apply --framework $Framework"),
        ("build.sh", 'python3 "$ROOT/tools/framework.py" apply --framework "$FRAMEWORK"'),
        ("build.ps1", 'python -c "import sys; sys.exit(sys.version_info < (3, 11))"'),
        ("build.sh", "python3 -c 'import sys; sys.exit(sys.version_info < (3, 11))' || {"),
        ("CMakeLists.txt", "    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)\n"),
    ):
        require(
            call in (ROOT / script).read_text(encoding="utf-8"),
            f"{script} applies the framework changes with a Python the tool runs on",
        )
    source = TOOL.read_text(encoding="utf-8")
    require(
        '        if self._git("cat-file", "-e", f"{pin}^0").returncode != 0:\n' in source and "^{" not in source,
        "the tool asks git for the pinned commit without braces, which an MSYS git expands when a native Python starts it",
    )

    print("framework tool test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
