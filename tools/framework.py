#!/usr/bin/env python3
"""Carry this project's changes to the pinned PSXRecomp checkout, in both directions.

The changes live under framework/. PIN names the framework revision. FILES.txt lists every file the project
changes there, one path a line, with "+" before a file the project adds. patches/<path>.patch is the diff of a
changed file against the pinned revision, and files/<path> is an added file whole.

    apply    write each changed file as its pinned content with its patch applied, copy the added files, verify
    refresh  rewrite patches/ and files/ from the checkout, after an edit made there
    check    verify that the checkout holds exactly what framework/ describes

Files are read and written as bytes and a patch is applied here, not by git, so the result does not depend on
a machine's line-ending settings. A patch is still a unified diff that `git apply` and `patch` read.
"""

import argparse
import difflib
import hashlib
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

PROJECT_ROOT = Path(__file__).resolve().parents[1]
INDEX_LINE = re.compile(rb"^index ([0-9a-f]{40})\.\.([0-9a-f]{40}) 100644\n", re.MULTILINE)
HUNK_LINE = re.compile(rb"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


class FrameworkError(Exception):
    """A state of the project's folder or of the checkout that the command cannot work with."""


def blob_hash(content: bytes) -> str:
    """Returns the hash git gives a file of this content."""
    return hashlib.sha1(b"blob %d\0" % len(content) + content, usedforsecurity=False).hexdigest()


def lines_of(content: bytes) -> list[bytes]:
    """Splits at line feeds, each line keeping its ending, a carriage return before it included.

    Raises:
        FrameworkError: The content does not end with a line feed.
    """
    if content and not content.endswith(b"\n"):
        raise FrameworkError("a file without a final line feed cannot be patched")
    return [line + b"\n" for line in content.split(b"\n")[:-1]]


def make_patch(path: PurePosixPath, before: bytes, after: bytes) -> bytes:
    """Returns the patch that turns `before` into `after`, empty when they are the same."""
    if before == after:
        return b""
    name = str(path).encode()
    hunks = difflib.diff_bytes(difflib.unified_diff, lines_of(before), lines_of(after), b"a/" + name, b"b/" + name)
    head = b"diff --git a/%s b/%s\nindex %s..%s 100644\n" % (
        name,
        name,
        blob_hash(before).encode(),
        blob_hash(after).encode(),
    )
    return head + b"".join(hunks)


def images(patch: bytes) -> tuple[str, str]:
    """Returns the hashes of the file before and after a patch, as its index line gives them.

    Raises:
        FrameworkError: The patch does not carry exactly one index line with full hashes.
    """
    found = INDEX_LINE.findall(patch)
    if len(found) != 1:
        raise FrameworkError("a patch must change one file and name its full hashes")
    return found[0][0].decode(), found[0][1].decode()


def apply_patch(before: bytes, patch: bytes) -> bytes:
    """Returns `before` with the patch applied, every line of every hunk matched exactly where the hunk says.

    Raises:
        FrameworkError: The patch is not for this content, or the result is not the one it names.
    """
    wanted_before, wanted_after = images(patch)
    if blob_hash(before) != wanted_before:
        raise FrameworkError("the patch was made against other content")
    source, rows = lines_of(before), lines_of(patch)
    result: list[bytes] = []
    taken = index = 0
    while index < len(rows):
        hunk = HUNK_LINE.match(rows[index])
        index += 1
        if hunk is None:
            continue
        old_left, new_left = int(hunk[2] or 1), int(hunk[4] or 1)
        first = int(hunk[1]) - (1 if old_left else 0)
        result += source[taken:first]
        taken = first
        while old_left or new_left:
            kind, text = rows[index][:1], rows[index][1:]
            index += 1
            if kind in b" -":
                if taken >= len(source) or source[taken] != text:
                    raise FrameworkError(f"a hunk does not match at line {taken + 1}")
                taken += 1
                old_left -= 1
            if kind in b" +":
                result.append(text)
                new_left -= 1
            if kind not in b" -+" or old_left < 0 or new_left < 0:
                raise FrameworkError("a hunk is malformed")
    after = b"".join(result + source[taken:])
    if blob_hash(after) != wanted_after:
        raise FrameworkError("the patch did not give the content it names")
    return after


@dataclass(frozen=True)
class Change:
    """One file of the framework that the project changes.

    Attributes:
        path: The file's path inside the framework.
        added: Whether the project adds the file, as opposed to changing one the framework has.
    """

    path: PurePosixPath
    added: bool


class Project:
    """The project's own record of its changes: the pin, the list, the patches and the added files."""

    def __init__(self, root: Path) -> None:
        self.folder = root / "framework"
        self.manifest = self.folder / "FILES.txt"

    def pin(self) -> str:
        """Returns the framework revision the patches are made against."""
        return (self.folder / "PIN").read_text(encoding="utf-8").strip()

    def changes(self) -> list[Change]:
        """Reads FILES.txt.

        Raises:
            FrameworkError: The list is empty, repeats a path or names one outside the framework.
        """
        changes = []
        for number, raw in enumerate(self.manifest.read_text(encoding="utf-8").splitlines(), start=1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            path = PurePosixPath(line.removeprefix("+").strip())
            if not path.parts or path.is_absolute() or ".." in path.parts or "\\" in line or ":" in line:
                raise FrameworkError(f"unsafe path in {self.manifest.name} at line {number}: {line}")
            changes.append(Change(path, line.startswith("+")))
        if not changes:
            raise FrameworkError(f"{self.manifest} lists no file")
        if len({change.path for change in changes}) != len(changes):
            raise FrameworkError(f"{self.manifest} lists a file twice")
        return changes

    def record(self, change: Change) -> Path:
        """Returns where the change is kept: the patch of a changed file, or an added file itself."""
        if change.added:
            return self.folder / "files" / change.path
        return self.folder / "patches" / f"{change.path}.patch"

    def strays(self, changes: list[Change]) -> list[str]:
        """Returns the patches and added files on disk that FILES.txt does not list."""
        listed = {self.record(change) for change in changes}
        kept = {path for name in ("patches", "files") for path in (self.folder / name).rglob("*") if path.is_file()}
        return sorted(path.relative_to(self.folder).as_posix() for path in kept - listed)


class Checkout:
    """A git checkout of the framework that has the pinned revision."""

    def __init__(self, folder: Path, pin: str) -> None:
        if not (folder / "runtime" / "runtime.cmake").is_file():
            raise FrameworkError(f"not a PSXRecomp checkout: {folder}")
        self.folder = folder
        self.pin = pin
        # No braces in an argument: an MSYS git started by a native Python expands them.
        if self._git("cat-file", "-e", f"{pin}^0").returncode != 0:
            raise FrameworkError(f"the checkout does not have the pinned revision {pin}")

    def _git(self, *arguments: str) -> subprocess.CompletedProcess[bytes]:
        return subprocess.run(["git", "-C", str(self.folder), *arguments], capture_output=True, check=False)

    def path(self, change: Change) -> Path:
        """Returns the file's place in the checkout."""
        return self.folder / change.path

    def pinned(self, change: Change) -> bytes | None:
        """Returns the file as the pinned revision has it, or None when the framework has no such file."""
        done = self._git("cat-file", "blob", f"{self.pin}:{change.path}")
        return done.stdout if done.returncode == 0 else None

    def present(self, change: Change) -> bytes | None:
        """Returns the file as it is in the checkout now, or None when it is not there."""
        path = self.path(change)
        return path.read_bytes() if path.is_file() else None

    def head(self) -> str:
        """Returns the revision the checkout is at."""
        return self._git("rev-parse", "HEAD").stdout.decode().strip()

    def changed(self) -> set[str]:
        """Returns the files of the framework that git sees as changed in the checkout."""
        done = self._git("-c", "core.quotepath=off", "status", "--porcelain", "--untracked-files=no", "--no-renames")
        return {line[3:] for line in done.stdout.decode().splitlines() if line}


class Framework:
    """The three commands, over a project and a checkout."""

    def __init__(self, project: Project, checkout: Checkout) -> None:
        self.project = project
        self.checkout = checkout
        self.changes = project.changes()
        self.pinned = {change: checkout.pinned(change) for change in self.changes}
        for change, pinned in self.pinned.items():
            if (pinned is None) != change.added:
                raise FrameworkError(
                    f"{change.path} is marked '+' but the framework has it"
                    if change.added
                    else f"{change.path} is not in the framework: mark it '+' if the project adds it"
                )

    def _described(self, change: Change) -> bytes:
        """Returns the file as framework/ says the checkout should hold it."""
        record = self.project.record(change)
        if not record.is_file():
            raise FrameworkError(f"framework/ has no record of {change.path}: run `python tools/framework.py refresh`")
        pinned = self.pinned[change]
        return record.read_bytes() if pinned is None else apply_patch(pinned, record.read_bytes())

    def _differing(self, pinned_too: bool) -> list[str]:
        """Lists the files of the checkout that are not what framework/ describes.

        Args:
            pinned_too: Whether a file still as the pinned revision has it, an added file being absent, also passes.
        """
        differing = []
        for change in self.changes:
            present = self.checkout.present(change)
            same = present == self._described(change) or (pinned_too and present == self.pinned[change])
            if not same:
                differing.append(str(change.path))
        return differing

    def _refuse_strays(self) -> None:
        strays = self.project.strays(self.changes)
        if strays:
            raise FrameworkError(f"not listed in {self.project.manifest.name}: {', '.join(strays)}")

    def check(self) -> str:
        """Verifies that the checkout is the pinned revision with exactly the changes framework/ describes.

        Raises:
            FrameworkError: The checkout is at another revision, a file differs, a framework file is changed
                without being listed, or framework/ keeps a file that FILES.txt does not list.
        """
        self._refuse_strays()
        if self.checkout.head() != self.checkout.pin:
            raise FrameworkError(f"the checkout is at {self.checkout.head()}, not at the pinned {self.checkout.pin}")
        unlisted = sorted(self.checkout.changed() - {str(change.path) for change in self.changes})
        if unlisted:
            raise FrameworkError(
                f"changed in the checkout and not in {self.project.manifest.name}: {', '.join(unlisted)}"
            )
        differing = self._differing(pinned_too=False)
        if differing:
            raise FrameworkError(
                f"the checkout differs from framework/ in {', '.join(differing)}: "
                "run `python tools/framework.py apply`, or `refresh` to keep an edit made there"
            )
        return f"The checkout holds the {len(self.changes)} framework changes."

    def apply(self, discard: bool) -> str:
        """Writes each changed file as its pinned content with its patch applied, and copies the added files.

        A file that already holds that content is left alone, so a build does not see it as changed.

        Args:
            discard: Whether to overwrite files of the checkout that hold an edit framework/ does not have.

        Raises:
            FrameworkError: The checkout holds such an edit and `discard` is off, or a patch does not apply.
        """
        self._refuse_strays()
        edited = self._differing(pinned_too=True)
        if edited and not discard:
            raise FrameworkError(
                f"the checkout holds edits that framework/ does not have, in {', '.join(edited)}: "
                "run `python tools/framework.py refresh` to keep them, or `apply --discard` to drop them"
            )
        described = {change: self._described(change) for change in self.changes}
        for change, content in described.items():
            destination = self.checkout.path(change)
            if not destination.parent.is_dir():
                raise FrameworkError(f"the framework has no folder for {change.path}")
            if self.checkout.present(change) != content:
                destination.write_bytes(content)
        self.check()
        return f"Applied and verified {len(self.changes)} framework changes."

    def refresh(self) -> str:
        """Rewrites patches/ and files/ from the checkout.

        Raises:
            FrameworkError: A listed file is missing from the checkout or is the same as the pinned one.
        """
        for change in self.changes:
            present, pinned = self.checkout.present(change), self.pinned[change]
            if present is None:
                raise FrameworkError(f"the checkout has no {change.path}")
            content = present if pinned is None else make_patch(change.path, pinned, present)
            if not content and pinned is not None:
                raise FrameworkError(f"{change.path} is the pinned file: take it out of {self.project.manifest.name}")
            record = self.project.record(change)
            record.parent.mkdir(parents=True, exist_ok=True)
            record.write_bytes(content)
        self._refuse_strays()
        return f"Saved {len(self.changes)} framework changes."


def main() -> int:
    """Runs one command and returns the process's exit status."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["apply", "refresh", "check"])
    parser.add_argument("--framework", type=Path, default=PROJECT_ROOT / "psxrecomp", help="the PSXRecomp checkout")
    parser.add_argument("--project", type=Path, default=PROJECT_ROOT, help="the folder that holds framework/")
    parser.add_argument("--discard", action="store_true", help="with apply: drop edits the checkout holds")
    arguments = parser.parse_args()
    try:
        project = Project(arguments.project.resolve())
        framework = Framework(project, Checkout(arguments.framework.resolve(), project.pin()))
        match arguments.command:
            case "apply":
                print(framework.apply(arguments.discard))
            case "refresh":
                print(framework.refresh())
            case _:
                print(framework.check())
    except (FrameworkError, OSError) as e:
        print(f"framework.py: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
