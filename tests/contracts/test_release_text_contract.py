#!/usr/bin/env python3
"""Test of the packager's GitHub release: its text, the written page or the changelog section, and its files."""

import ast
import importlib
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHANGELOG = (
    "# Changelog\n\n## v0.2.0-alpha.1\n\n### Changes\n\n- One\n\n"
    "## v0.1.0-alpha.5\n\nReleased on 2026-10-13.\n\n- Two\n"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def refused(call) -> bool:
    try:
        call()
    except SystemExit:
        return True
    return False


sys.path.insert(0, str(ROOT / "tools"))
package_release = importlib.import_module("package_release")

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    (root / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
    newest = "v0.2.0-alpha.1"
    require(
        package_release.release_text(newest, root) == "### Changes\n\n- One\n",
        "without a written page the text is the version's changelog section",
    )
    notes = root / "release" / "notes"
    notes.mkdir(parents=True)
    page = notes / f"{newest}.md"
    page.write_bytes(b"Banner\r\n\r\n## In this release\r\n\r\n- One\r\n\r\n")
    require(
        package_release.release_text(newest, root) == "Banner\n\n## In this release\n\n- One\n",
        "a written page is the text, with plain line ends",
    )
    require(
        package_release.release_notes(newest, root) == "### Changes\n\n- One\n",
        "the packages keep the changelog section beside a written page",
    )
    require(
        package_release.release_text("v0.1.0-alpha.5", root) == "Released on 2026-10-13.\n\n- Two\n",
        "a version without a page of its own still takes its section",
    )
    page.write_text(
        "| [Slowdowns](#slowdowns) | Less |\n| [Draw distance 1.25x](#draw-distance-125x) | Further |\n\n"
        "## Slowdowns\n\nOne\n\n## Draw distance 1.25x ##\n\nTwo\n\n## Settings\n\nSee [Install](#install)\n",
        encoding="utf-8",
    )
    require(
        package_release.release_text(newest, root)
        == "| [Slowdowns](#user-content-slowdowns) | Less |\n"
        "| [Draw distance 1.25x](#user-content-draw-distance-125x) | Further |\n\n"
        '<a id="slowdowns"></a>\n## Slowdowns\n\nOne\n\n'
        '<a id="draw-distance-125x"></a>\n## Draw distance 1.25x ##\n\nTwo\n\n'
        "## Settings\n\nSee [Install](#user-content-install)\n"
        and '<a id="install"></a>\n## Install\n' in package_release.release_body("Banner\n"),
        "each linked heading gets its anchor on the line before it, the links name it as GitHub prefixes it, "
        "a heading nothing links to gets none, and the Install section the packager adds has its own",
    )
    reworded = '| [Draw](#user-content-draw) | Far |\n\n<a id="draw"></a>\n## Draw, fixed in 0.4.0\n\nOne\n'
    page.write_text(reworded, encoding="utf-8")
    require(
        package_release.release_text(newest, root) == reworded,
        "an anchored text is kept as it is, and a heading reworded after its anchor gets no second one",
    )
    fenced = "[Slowdowns](#slowdowns)\n\n```markdown\n## Slowdowns\n[Here](#here)\n```\n\n## Slowdowns\n"
    page.write_text(fenced, encoding="utf-8")
    require(
        package_release.release_text(newest, root)
        == "[Slowdowns](#user-content-slowdowns)\n\n```markdown\n## Slowdowns\n[Here](#here)\n```\n\n"
        '<a id="slowdowns"></a>\n## Slowdowns\n',
        "a code block is left as written, and the anchor goes to the heading outside it",
    )
    for unknown in ("| [Gone](#gone) | Away |\n\n## Here\n", "[Here][here]\n\n[here]: #here\n\n## Here\n"):
        page.write_text(unknown, encoding="utf-8")
        require(
            refused(lambda: package_release.release_text(newest, root)),
            "a link to a heading the text lacks, or one by reference, stops the release",
        )
    for heading in ("## Install", "## Install  ", "## Install ##", "   ## Install", "# install"):
        page.write_text(f"Banner\n\n{heading}\n\nMine\n", encoding="utf-8")
        require(refused(lambda: package_release.release_text(newest, root)), f"a page with '{heading}' is refused")
    kept = "See the Install section below.\n\n### Install notes\n\n## Verify\n\n    ## Install\n"
    page.write_text(kept, encoding="utf-8")
    require(
        package_release.release_text(newest, root) == kept,
        "a mention of Install, a smaller heading, a Verify heading and an indented code line are no section of the packager's",
    )
    page.write_text(" \n\n", encoding="utf-8")
    require(refused(lambda: package_release.release_text(newest, root)), "an empty page is refused")
    page.unlink()
    (root / "CHANGELOG.md").write_text("# Changelog\n", encoding="utf-8")
    require(refused(lambda: package_release.release_text(newest, root)), "no page and no section is refused")

players = ["DisruptorRecomp.AppImage"]
packed = ("DisruptorRecomp-0.2.0-alpha.1-linux-full.nupkg", "DisruptorRecomp-0.2.0-alpha.1-linux-delta.nupkg")
signed = ["DisruptorRecomp.AppImage.sigstore.json", "DisruptorRecomp-linux.intoto.jsonl"]
names = package_release.published_names("linux", players, packed, signed)
require(
    names == {*players, "releases.linux.json", *packed, *signed},
    "a release carries the players' files, the update feed, its packages and the players' signatures, no digest files",
)
require(
    not names & {"assets.linux.json", "RELEASES-linux", "releases.win.json"},
    "Velopack's assets list, its legacy RELEASES file and another channel's feed stay out",
)

body = Path("body.md")
created = package_release.create_command("v0.3.0-alpha.1", "RecompTeam/DisruptorRecomp", "abc123", body, ["a.exe"])
require(
    "--draft" in created
    and "--latest" not in created
    and "--prerelease" not in created
    and created[created.index("--target") + 1] == "abc123"
    and created[created.index("--title") + 1] == "0.3.0-alpha.1"
    and created[-1] == "a.exe",
    "Windows makes a draft, not a pre-release, at the build's commit, titled by the version alone, with its files",
)
finished = package_release.finish_command("v0.3.0-alpha.1", "RecompTeam/DisruptorRecomp", body, draft=True)
kept = package_release.finish_command("v0.3.0-alpha.1", "RecompTeam/DisruptorRecomp", body, draft=False)
require(
    finished[:4] == ["gh", "release", "edit", "v0.3.0-alpha.1"]
    and finished[finished.index("--notes-file") + 1] == str(body)
    and "--draft=false" in finished
    and "--latest" in finished
    and "--prerelease" not in finished,
    "Linux publishes the draft with its text, a regular release and the latest",
)
require(
    kept == finished[: finished.index("--draft=false")] and "--latest" not in kept,
    "a release already published only gets its text, it is not made the latest again",
)
body = package_release.release_body("Banner\n")
windows, linux = package_release.PLATFORMS["windows"], package_release.PLATFORMS["linux"]
require(
    body.startswith('Banner\n\n<a id="install"></a>\n## Install\n')
    and body.endswith(windows.install)
    and "Verify" not in body,
    "Windows publishes the release's text, then the Install section with its own lines, and no Verify section",
)
both = package_release.with_install(body, linux)
require(
    both == body + "\n" + linux.install and package_release.with_install(both, linux) == both,
    "Linux adds its lines once, at the end of the Install section",
)
require(
    refused(lambda: package_release.with_install("Banner\n", linux)), "a text without an Install section is refused"
)
require(
    refused(lambda: package_release.with_install(body + "\n## Verify\n\nSigned.\n", linux))
    and refused(lambda: package_release.with_install("## Install\n\nOne\n\n## Notes\n", linux))
    and refused(lambda: package_release.with_install("Banner\n## Install\n## Notes\n", linux)),
    "so is one with a section after Install, where Linux's lines would land in the wrong section",
)


def payload_unpacks_bundled_mods() -> bool:
    """Whether payload_of's file table unpacks bundled_mods(build) into itself, read from the code and not its text."""
    tree = ast.parse((ROOT / "tools/package_release.py").read_text(encoding="utf-8"))
    payload = next(node for node in ast.walk(tree) if isinstance(node, ast.FunctionDef) and node.name == "payload_of")
    tables = [
        node.value
        for node in ast.walk(payload)
        if isinstance(node, ast.Assign)
        and [getattr(target, "id", None) for target in node.targets] == ["files"]
        and isinstance(node.value, ast.Dict)
    ]
    return len(tables) == 1 and any(
        key is None
        and isinstance(value, ast.Call)
        and getattr(value.func, "id", None) == "bundled_mods"
        and [getattr(arg, "id", None) for arg in value.args] == ["build"]
        for key, value in zip(tables[0].keys, tables[0].values, strict=True)
    )


with tempfile.TemporaryDirectory() as folder:
    build = Path(folder)
    rewind = build / "mods/bundled/psx.feature.rewind/1.0.0/manifest.toml"
    rewind.parent.mkdir(parents=True)
    rewind.write_text("id = 1\n", encoding="utf-8")
    without_game = refused(lambda: package_release.bundled_mods(build))
    game = build / "mods/bundled/disruptor.game/1.0.0/manifest.toml"
    game.parent.mkdir(parents=True)
    game.write_text("id = 2\n", encoding="utf-8")
    (build / "mods/state.toml").write_text("format_version = 2\n", encoding="utf-8")
    frame = build / "mods/bundled/psx.presentation.bezel/1.0.0/assets/frame.png"
    frame.parent.mkdir(parents=True)
    frame.write_bytes(b"\x89PNG")
    require(
        without_game
        and package_release.bundled_mods(build)
        == {
            "mods/bundled/disruptor.game/1.0.0/manifest.toml": game,
            "mods/bundled/psx.feature.rewind/1.0.0/manifest.toml": rewind,
            "mods/bundled/psx.presentation.bezel/1.0.0/assets/frame.png": frame,
        }
        and payload_unpacks_bundled_mods(),
        "a package carries every mod package the framework staged, the player's mod state not among them, and refuses "
        "a build without the game's own: 0.10.0 shipped without it, and none of the game's enhancements and fixes ran",
    )

release_smoke = importlib.import_module("release_smoke")
windows, linux = package_release.PLATFORMS["windows"], package_release.PLATFORMS["linux"]
MANIFEST = b'{"files": {}}\n'
ZIP_DIGEST = "a" * 64
PULSES = (
    "disruptor: first frame 1.4 s after the start\n"
    "disruptor: +11.7s pulse: 1263 frames in 10.0 s, 7.9 ms a frame, longest 27.4 ms, game 0 frames in 0 vblanks with "
    "0 late, flat screen, not focused, shown 197 pictures, 0 in between, longest gap 2002.0 ms\n"
    "disruptor: +21.8s pulse: 600 frames in 10.0 s, 16.7 ms a frame, longest 24.3 ms, game 0 frames in 0 vblanks with "
    "0 late, flat screen, not focused, shown 300 pictures, 0 in between, longest gap 37.8 ms\n"
)
STALLED = PULSES.replace("shown 300 pictures", "shown 0 pictures")
LATER_STALL = PULSES + (
    "disruptor: +31.8s pulse: 600 frames in 10.0 s, 16.7 ms a frame, longest 20.7 ms, game 0 frames in 0 vblanks with "
    "0 late, flat screen, not focused, shown 0 pictures, 0 in between, longest gap 10000.0 ms\n"
)
FIRST_LOADING = PULSES.replace("shown 197 pictures", "shown 0 pictures") + LATER_STALL[len(PULSES) :].replace(
    "shown 0 pictures", "shown 300 pictures"
)


def smoke_result(**changes) -> dict:
    checks = [{"name": "files", "passed": True}, {"name": "launcher", "passed": True}]
    checks += [{"name": "start", "passed": True, "seconds": 30.0}, {"name": "capsule", "passed": True}]
    return {
        "version": "v0.10.2",
        "platform": "windows",
        "package_sha256": ZIP_DIGEST,
        "manifest_sha256": package_release.hashlib.sha256(MANIFEST).hexdigest(),
        "passed": True,
        "checks": checks,
    } | changes


with tempfile.TemporaryDirectory() as folder:
    stamp = package_release.smoke_stamp(Path(folder), "v0.10.2", windows)

    def smoke_accepted(written: dict | None, platform=windows, digest: str = ZIP_DIGEST) -> bool:
        stamp.unlink(missing_ok=True)
        if written is not None:
            stamp.parent.mkdir(parents=True, exist_ok=True)
            stamp.write_text(package_release.json.dumps(written), encoding="utf-8")
        return not refused(lambda: package_release.check_smoke(stamp, "v0.10.2", platform, MANIFEST, digest))

    linux_checks = [{"name": "files", "passed": True}, {"name": "launcher", "passed": True}]
    linux_checks.append({"name": "start", "passed": True, "seconds": 30.0})
    CAPSULE_PASS = {"name": "capsule", "passed": True}
    require(
        stamp == Path(folder) / "v0.10.2/windows.smoke.json"
        and not smoke_accepted(None)
        and smoke_accepted(smoke_result())
        and not smoke_accepted(smoke_result(), digest="b" * 64)
        and not smoke_accepted(smoke_result(manifest_sha256="0" * 64))
        and not smoke_accepted(smoke_result(version="v0.10.1"))
        and not smoke_accepted(smoke_result(platform="linux"))
        and not smoke_accepted(smoke_result(passed=False))
        and not smoke_accepted(smoke_result(checks=linux_checks))
        and not smoke_accepted(smoke_result(checks=[*linux_checks, {"name": "capsule", "passed": False}]))
        and not smoke_accepted(
            smoke_result(checks=[*linux_checks[:2], {"name": "start", "passed": True, "seconds": 0.0}, CAPSULE_PASS])
        )
        and not smoke_accepted(
            smoke_result(checks=[*linux_checks[:2], {"name": "start", "passed": True}, CAPSULE_PASS])
        )
        and not smoke_accepted(smoke_result(checks=[linux_checks[0], linux_checks[2], CAPSULE_PASS]))
        and smoke_accepted(smoke_result(platform="linux", checks=linux_checks), platform=linux),
        "a publish needs the smoke test's pass on this very player's file and payload, version and system, every check "
        "that system asks for and a start of 25 s or more: the capsule on Windows, files and start on both",
    )
require(
    package_release.ci_passed(
        [
            {"status": "completed", "conclusion": "failure", "databaseId": 1, "attempt": 1},
            {"status": "completed", "conclusion": "success", "databaseId": 1, "attempt": 2},
        ]
    )
    and not package_release.ci_passed(
        [
            {"status": "completed", "conclusion": "success", "databaseId": 1, "attempt": 1},
            {"status": "completed", "conclusion": "failure", "databaseId": 2, "attempt": 1},
        ]
    )
    and not package_release.ci_passed([])
    and not package_release.ci_passed([{"status": "in_progress", "conclusion": "", "databaseId": 3, "attempt": 1}]),
    "a publish needs the newest build run on its commit, reruns counted, to be green: 0.10.0 to 0.10.2 went out while "
    "CI was red",
)
require(
    release_smoke.judge_start(None, 30.0, PULSES)[0]
    and not release_smoke.judge_start(None, 30.0, "disruptor: first frame 1.4 s after the start\n")[0]
    and not release_smoke.judge_start(None, 30.0, STALLED)[0]
    and not release_smoke.judge_start(None, 30.0, PULSES.replace("first frame", "no frame"))[0]
    and not release_smoke.judge_start(1, 0.8, PULSES)[0]
    and not release_smoke.judge_start(None, 40.0, LATER_STALL)[0]
    and release_smoke.judge_start(None, 40.0, FIRST_LOADING)[0]
    and not release_smoke.judge_start(None, 30.0, PULSES + "Disruptor: The game's own package is missing\n")[0],
    "a start passes only alive at the end, with its first frame and new pictures in every pulse after the first and "
    "in two at least: a game stalled after its first frame, or after two good pulses, fails",
)
started = release_smoke.Check("start", True, "ran", 30.0)
entered = [release_smoke.Check("files", True, ""), release_smoke.Check("launcher", True, "")]
require(
    release_smoke.verdict_of("v0.10.2", "windows", Path(folder), ZIP_DIGEST, MANIFEST, [*entered, started])["passed"]
    is False
    and release_smoke.verdict_of("v0.10.2", "linux", Path(folder), ZIP_DIGEST, MANIFEST, [entered[0], started])[
        "passed"
    ]
    is False
    and release_smoke.verdict_of("v0.10.2", "linux", Path(folder), ZIP_DIGEST, MANIFEST, [*entered, started])["passed"]
    is True
    and release_smoke.verdict_of(
        "v0.10.2",
        "linux",
        Path(folder),
        ZIP_DIGEST,
        MANIFEST,
        [*entered, started, release_smoke.Check("error", False, "OSError")],
    )["passed"]
    is False,
    "the smoke test passes only when every check its system needs ran and passed and nothing broke",
)
with tempfile.TemporaryDirectory() as folder:
    original = Path(folder) / "DisruptorRecomp-win-Portable.zip"
    with zipfile.ZipFile(original, "w") as archive:
        archive.writestr("current/MANIFEST.json", MANIFEST)
    tested = package_release.hashlib.sha256(original.read_bytes()).hexdigest()
    (Path(folder) / "work").mkdir()
    snapshot = release_smoke.Smoke(original, "windows", Path(folder), Path(folder) / "work")
    snapshot.unpack()
    written = Path(folder) / "written.json"
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/release_smoke.py"),
            *("--version", "v0.10.2", "--platform", "windows", "--package", str(original), "--disc", folder),
            *("--stamp", str(written), "--capsule", folder),
        ],
        capture_output=True,
        check=False,
    )
    result = package_release.json.loads(written.read_text(encoding="utf-8"))
    original.write_bytes(b"replaced while the test ran")
    stale = Path(folder) / "stamp.json"
    stale.write_text('{"passed": true}', encoding="utf-8")
    rejected = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/release_smoke.py"),
            *("--version", "v0.10.2", "--platform", "windows", "--package", str(original), "--disc", folder),
            *("--stamp", str(stale), "--seconds", "0"),
        ],
        capture_output=True,
        check=False,
    )
    require(
        snapshot.digest == tested
        and result["package_sha256"] == tested
        and result["passed"] is False
        and snapshot.manifest() == MANIFEST
        and rejected.returncode == 2
        and not stale.exists(),
        "the smoke test names the copy it tested, not a package replaced while it ran, and a rejected rerun leaves no "
        "earlier pass behind",
    )


class Recorder:
    """Stands for subprocess in publish(): records each command and answers git and gh as a test asks."""

    def __init__(self, runs: list[dict]) -> None:
        self.runs, self.commands = runs, []

    def run(self, command, **_):
        self.commands.append(command)
        answer = package_release.json.dumps(self.runs) if command[:3] == ["gh", "run", "list"] else ""
        code = 1 if command[:3] == ["gh", "release", "view"] else 0
        return subprocess.CompletedProcess(command, code, stdout=answer, stderr="")


def publish_attempt(
    record_changes: dict, smoke: dict, runs: list[dict], changed_file: bool = False, added_file: bool = False
) -> list:
    """What publish() did before it stopped or finished: the signing and every command, in order."""
    with tempfile.TemporaryDirectory() as root:
        output = Path(root)
        work = output / "v0.10.2/windows"
        (work / "program").mkdir(parents=True)
        (work / "packages").mkdir()
        (work / "program/MANIFEST.json").write_bytes(MANIFEST)
        for name in windows.player_files:
            (work / "packages" / name).write_bytes(name.encode())
        digests = package_release.package_digests(work / "packages")
        smoke = smoke | {"package_sha256": digests["DisruptorRecomp-win-Portable.zip"]}
        record = {"version": "v0.10.2", "platform": "windows", "commit": "c0ffee", "clean": True}
        record |= {"started": "2026-11-03T00:00:00Z", "files": digests} | record_changes
        if record_changes.get("files") != "absent":
            (work / package_release.PACKED).write_text(package_release.json.dumps(record), encoding="utf-8")
        if changed_file:
            (work / "packages/DisruptorRecomp-win-Setup.exe").write_bytes(b"other")
        if added_file:
            (work / "packages/DisruptorRecomp-0.10.2-delta.nupkg").write_bytes(b"not packed")
        stamp = package_release.smoke_stamp(output, "v0.10.2", windows)
        stamp.write_text(package_release.json.dumps(smoke), encoding="utf-8")
        recorder = Recorder(runs)
        kept = (
            package_release.subprocess,
            package_release.sign,
            package_release.source_commit,
            package_release.checkout_changed,
        )
        package_release.subprocess = recorder
        package_release.sign = lambda *_: recorder.commands.append(["sign"]) or []
        package_release.source_commit = lambda: "c0ffee"
        package_release.checkout_changed = lambda: False
        try:
            stopped = refused(lambda: package_release.publish(work, "v0.10.2", windows, stamp, "Text\n"))
        finally:
            (
                package_release.subprocess,
                package_release.sign,
                package_release.source_commit,
                package_release.checkout_changed,
            ) = kept
        added = ["uploaded an unrecorded file"] if any("delta.nupkg" in " ".join(c) for c in recorder.commands) else []
        return [*(["stopped"] if stopped else []), *(" ".join(command[:3]) for command in recorder.commands), *added]


GREEN = [{"status": "completed", "conclusion": "success", "databaseId": 9, "attempt": 1}]
RED = [{"status": "completed", "conclusion": "failure", "databaseId": 9, "attempt": 1}]
whole = publish_attempt({}, smoke_result(), GREEN)
require(
    publish_attempt({"files": "absent"}, smoke_result(), GREEN) == ["stopped"]
    and publish_attempt({"clean": False}, smoke_result(), GREEN) == ["stopped"]
    and publish_attempt({"commit": "other"}, smoke_result(), GREEN) == ["stopped"]
    and publish_attempt({}, smoke_result(), GREEN, changed_file=True) == ["stopped"]
    and publish_attempt({}, smoke_result(passed=False), GREEN) == ["stopped"]
    and publish_attempt({}, smoke_result(), RED) == ["stopped", "gh run list"]
    and whole[0] == "gh run list"
    and whole[1] == "sign"
    and whole[-1] == "gh release create"
    and publish_attempt({}, smoke_result(), GREEN, added_file=True)[-1] == "gh release create",
    "publish() signs and uploads only a clean, unchanged packing of the commit that passed the smoke test, after a "
    f"green CI run, and none of the files added after packing: every refusal stops before signing, got {whole}",
)

print("Disruptor release text and files: PASS")
