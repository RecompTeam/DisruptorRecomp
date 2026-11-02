#!/usr/bin/env python3
"""Start a packed release the way the launcher does and write whether it ran, for package_release.py --publish.

The package is copied first and only the copy is tested and named by its digest. It is unpacked into a folder of its
own and the player's files go into another, as an installed or portable copy keeps them. Every file its MANIFEST.json
names must be there unchanged, the game's own package among them. The package's own entry, the portable stub or the
AppImage's AppRun, must start the launcher and have it report this version. The game then starts muted and hidden
from power-on with the disc and detailed logging: it must draw its first frame, show new pictures from the game in
every pulse after the first and in two at least, and still run at the end. Without its package it stops with a
message. On Windows the game also replays a capsule recorded on this build and must stay in step, which
the capsule's states check against the memory the game's own package takes. The result is written as JSON with the
package's digest and its MANIFEST.json's, and a failed or broken run replaces an earlier pass.
"""

import argparse
import datetime
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib
import zipfile
from dataclasses import asdict, dataclass, field
from pathlib import Path

GAME_PACKAGE = "mods/bundled/disruptor.game"
MISSING_PACKAGE = "The game's own package is missing"
REQUIRED_CHECKS = {"windows": ("files", "launcher", "start", "capsule"), "linux": ("files", "launcher", "start")}
LAUNCHER_SECONDS = 60
MIN_SECONDS = 25.0
PULSES_NEEDED = 2
REPLAY_SECONDS = 600
PULSE = re.compile(r"pulse: (\d+) frames in ")
SHOWN = re.compile(r", shown (\d+) pictures")
START_SETTINGS = "[audio]\nmuted = true\n\n[disruptor]\ndetailed_logging = true\n"


@dataclass
class Check:
    """One check's name, whether it passed and what it saw."""

    name: str
    passed: bool
    detail: str
    seconds: float | None = None


def judge_start(code: int | None, ran: float, said: str) -> tuple[bool, str]:
    """Whether a start ran: alive at the end, its first frame drawn, and new pictures from the game in every pulse
    after the first, which may hold the disc's load, and in two at least."""
    pulses = [
        (int(frames.group(1)), int(shown.group(1)) if (shown := SHOWN.search(line)) else 0)
        for line in said.splitlines()
        if (frames := PULSE.search(line))
    ]
    moving = [pulse for pulse in pulses if all(count > 0 for count in pulse)]
    problems = [
        *([f"ended with {code}"] if code is not None else []),
        *(["it said its own package is missing"] if MISSING_PACKAGE in said else []),
        *(["no first frame"] if "disruptor: first frame" not in said else []),
        *([f"{len(moving)} of {PULSES_NEEDED} pulses with new pictures"] if len(moving) < PULSES_NEEDED else []),
        *(["a pulse after the first without new pictures"] if any(pulse not in moving for pulse in pulses[1:]) else []),
    ]
    tail = " | ".join(said.strip().splitlines()[-3:])[-500:]
    return not problems, f"{', '.join(problems) or 'ran'} after {ran:.1f} s, pulses (frames, shown) {pulses}: {tail}"


@dataclass
class Smoke:
    """A packed release, unpacked into a work folder, and the checks run on it."""

    package: Path
    platform: str
    disc: Path
    work: Path
    checks: list[Check] = field(default_factory=list)
    program: Path = field(init=False)
    snapshot: Path = field(init=False)
    digest: str = field(init=False)

    @property
    def executable(self) -> Path:
        return self.program / ("DisruptorRecompiled.exe" if self.platform == "windows" else "DisruptorRecompiled")

    def unpack(self) -> None:
        """Copies the package, then puts the copy's program where an installed copy keeps it: Velopack's current
        folder, or the AppImage's usr/bin."""
        self.snapshot = self.work / self.package.name
        shutil.copy2(self.package, self.snapshot)
        self.digest = hashlib.sha256(self.snapshot.read_bytes()).hexdigest()
        if self.platform == "windows":
            with zipfile.ZipFile(self.snapshot) as archive:
                archive.extractall(self.work / "portable")
            self.program = self.work / "portable/current"
            return
        self.snapshot.chmod(0o755)
        subprocess.run([str(self.snapshot), "--appimage-extract"], cwd=self.work, capture_output=True, check=True)
        self.program = self.work / "squashfs-root/usr/bin"

    def manifest(self) -> bytes:
        return (self.program / "MANIFEST.json").read_bytes()

    def check_files(self) -> None:
        listed = json.loads(self.manifest())["files"]
        wrong = [
            name
            for name, digest in listed.items()
            if not (self.program / name).is_file()
            or hashlib.sha256((self.program / name).read_bytes()).hexdigest() != digest
        ]
        own = [name for name in listed if name.startswith(f"{GAME_PACKAGE}/") and name.endswith("/manifest.toml")]
        self.checks.append(
            Check(
                "files",
                not wrong and bool(own),
                f"{len(listed)} files, the game's package {own or 'absent'}, {len(wrong)} missing or changed {wrong[:5]}",
            )
        )

    def check_launcher(self, version: str) -> None:
        """Starts the package's own entry as a player does and asks the launcher for its version, without a window."""
        status = self.work / "launcher-status.txt"
        data = self.work / "launcher-data"
        environment = dict(os.environ, PSX_USER_DIR=str(data), XDG_DATA_HOME=str(data), APPIMAGE_EXTRACT_AND_RUN="1")
        entry = self.work / "portable/Disruptor Recompiled.exe" if self.platform == "windows" else self.snapshot
        process = subprocess.Popen(
            [str(entry), "--status", str(status)],
            cwd=self.work,
            env=environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        deadline = time.monotonic() + LAUNCHER_SECONDS
        while time.monotonic() < deadline and "installed=" not in (
            status.read_text(errors="replace") if status.is_file() else ""
        ):
            time.sleep(0.25)
        try:
            process.wait(max(0.0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        said = status.read_text(errors="replace") if status.is_file() else ""
        installed = next((line.split("=", 1)[1] for line in said.splitlines() if line.startswith("installed=")), None)
        self.checks.append(
            Check(
                "launcher",
                installed == version.removeprefix("v"),
                f"{entry.name} --status: installed {installed}, exit {process.returncode}: {' | '.join(said.splitlines())}",
            )
        )

    def data_folder(self, name: str, settings: str) -> Path:
        """A player's folder of its own with `settings`, which must keep the game muted."""
        if tomllib.loads(settings).get("audio", {}).get("muted") is not True:
            raise SystemExit("the settings to run with are not muted")
        data = self.work / name
        data.mkdir()
        (data / "settings.toml").write_text(settings, encoding="utf-8", newline="\n")
        return data

    def start(self, data: Path, extra: dict[str, str]) -> tuple[subprocess.Popen[bytes], Path]:
        """Starts the game as the launcher does: from the program's folder, the player's folder in PSX_USER_DIR."""
        log = data / "startup.log"
        environment = dict(os.environ, PSX_USER_DIR=str(data), PSX_HIDDEN_WINDOW="1", **extra)
        for name in ("PSX_LOAD_SLOT", "PSX_DISRUPTOR_CAPSULE_RECORD", "PSX_DISRUPTOR_CAPSULE_REPLAY"):
            if name not in extra:
                environment.pop(name, None)
        command = [str(self.executable), "--no-launcher", "--game", str(self.program / "game.toml")]
        command += ["--disc", str(self.disc)]
        with log.open("wb") as output:
            process = subprocess.Popen(command, cwd=self.program, env=environment, stdout=output, stderr=output)
        return process, log

    def check_start(self, seconds: float) -> None:
        process, log = self.start(self.data_folder("data", START_SETTINGS), {"SDL_AUDIODRIVER": "dummy"})
        started = time.monotonic()
        while process.poll() is None and time.monotonic() - started < seconds:
            time.sleep(0.25)
        code = process.poll()
        ran = time.monotonic() - started
        if code is None:
            process.kill()
            process.wait()
        passed, detail = judge_start(code, ran, log.read_text(encoding="utf-8", errors="replace"))
        self.checks.append(Check("start", passed, detail, round(ran, 1)))

    def check_capsule(self, capsule: Path) -> None:
        copy = self.work / "capsule" / capsule.name
        shutil.copytree(capsule, copy)
        settings = (copy / "settings.toml").read_text(encoding="utf-8")
        lines = [line for line in settings.splitlines() if line.split("=")[0].strip() != "muted"]
        at = lines.index("[audio]") if "[audio]" in lines else None
        lines = (
            lines + ["[audio]", "muted = true"] if at is None else lines[: at + 1] + ["muted = true"] + lines[at + 1 :]
        )
        data = self.data_folder("replay", "\n".join(lines) + "\n")
        process, log = self.start(data, {"PSX_DISRUPTOR_CAPSULE_REPLAY": str(copy), "PSX_LOW_LATENCY_INPUT": "0"})
        try:
            code = process.wait(REPLAY_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = None
        told = [line for line in log.read_text(encoding="utf-8", errors="replace").splitlines() if "capsule:" in line]
        said = told[-1].split("capsule:", 1)[1].strip() if told else "nothing said of the capsule"
        passed = code == 0 and "stayed in step" in said
        self.checks.append(Check("capsule", passed, f"{capsule.name}: exit {code}: {said[:300]}"))


def verdict_of(
    version: str, platform: str, package: Path, digest: str | None, manifest: bytes | None, checks: list[Check]
) -> dict:
    """The result: a pass only when every check the system needs ran and passed."""
    ran = {check.name for check in checks if check.passed}
    return {
        "version": version,
        "platform": platform,
        "package": package.name,
        "package_sha256": digest,
        "manifest_sha256": hashlib.sha256(manifest).hexdigest() if manifest is not None else None,
        "passed": all(name in ran for name in REQUIRED_CHECKS[platform]) and all(check.passed for check in checks),
        "checks": [asdict(check) for check in checks],
        "time": datetime.datetime.now(datetime.UTC).replace(microsecond=0).isoformat(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="the release tag, such as v0.10.2")
    parser.add_argument("--platform", choices=sorted(REQUIRED_CHECKS), required=True)
    parser.add_argument("--package", type=Path, required=True, help="the Portable ZIP or the AppImage")
    parser.add_argument("--disc", type=Path, required=True, help="the CUE of a supported disc")
    parser.add_argument("--capsule", type=Path, help="a capsule recorded on this build, needed on Windows")
    parser.add_argument(
        "--seconds", type=float, default=30.0, help=f"how long the start runs, {MIN_SECONDS:.0f} at least"
    )
    parser.add_argument("--stamp", type=Path, required=True, help="where the JSON result goes")
    args = parser.parse_args()
    args.stamp.unlink(missing_ok=True)
    if not math.isfinite(args.seconds) or args.seconds < MIN_SECONDS:
        parser.error(f"--seconds must be {MIN_SECONDS:.0f} or more")
    if "capsule" in REQUIRED_CHECKS[args.platform] and args.capsule is None:
        parser.error(f"--capsule is needed on {args.platform}")
    package = args.package.resolve()
    checks: list[Check] = []
    manifest = digest = None
    with tempfile.TemporaryDirectory(prefix="disruptor-smoke-") as folder:
        smoke = Smoke(package, args.platform, args.disc.resolve(), Path(folder), checks)
        try:
            smoke.unpack()
            digest = smoke.digest
            manifest = smoke.manifest()
            smoke.check_files()
            smoke.check_launcher(args.version)
            smoke.check_start(args.seconds)
            if args.capsule:
                smoke.check_capsule(args.capsule.resolve())
        except (OSError, ValueError, KeyError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
            checks.append(Check("error", False, f"{type(error).__name__}: {error}"))
    verdict = verdict_of(args.version, args.platform, package, digest, manifest, checks)
    args.stamp.parent.mkdir(parents=True, exist_ok=True)
    args.stamp.write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    for check in verdict["checks"]:
        print(f"{'PASS' if check['passed'] else 'FAIL'} {check['name']}: {check['detail']}")
    print(f"{'passed' if verdict['passed'] else 'FAILED'}, written to {args.stamp}")
    return 0 if verdict["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
