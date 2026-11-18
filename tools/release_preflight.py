"""Check the player's guide and a linked profile against the checkout before packing a release."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

if sys.platform == "win32":
    import _winapi

ROOT = Path(__file__).resolve().parents[1]


def check_release_inputs() -> None:
    """Build the two small checks from current sources. Use MinGW on Windows to exercise its filesystem implementation."""
    compiler = shutil.which(os.environ.get("DISRUPTOR_PREFLIGHT_CXX", "g++"))
    if not compiler:
        raise SystemExit("Set DISRUPTOR_PREFLIGHT_CXX to g++ (MinGW-w64 on Windows) for the release checks")
    env = os.environ.copy()
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env["PATH"]
    if sys.platform == "win32":
        machine = subprocess.check_output([compiler, "-dumpmachine"], env=env, text=True).strip()
        if not machine.startswith("x86_64-") or "mingw" not in machine:
            raise SystemExit(f"Expected an x64 MinGW-w64 compiler for the release checks, got {machine}")
    command = [compiler, "-std=c++20", "-Isrc"]
    with tempfile.TemporaryDirectory(prefix="disruptor-preflight-") as temp:
        folder = Path(temp).resolve()
        guide = folder / ("guide.exe" if sys.platform == "win32" else "guide")
        subprocess.run(
            [
                *command,
                "-Isrc/launcher",
                '-DDISRUPTOR_SOURCE_DIR="."',
                "tests/unit/launcher_guide_test.cpp",
                "src/launcher/guide.cpp",
                "-o",
                str(guide),
            ],
            cwd=ROOT,
            env=env,
            check=True,
        )
        subprocess.run([guide], cwd=ROOT, env=env, check=True)
        profile = folder / ("profile.exe" if sys.platform == "win32" else "profile")
        subprocess.run(
            [
                *command,
                "-Ipsxrecomp/runtime/include",
                "tests/unit/user_folder_test.cpp",
                "src/game/settings/user_folder.cpp",
                *(["-lshell32", "-lole32", "-luuid"] if sys.platform == "win32" else []),
                "-o",
                str(profile),
            ],
            cwd=ROOT,
            env=env,
            check=True,
        )
        target = folder / "versions" / "v2"
        target.mkdir(parents=True)
        link = folder / "library" / "current"
        link.parent.mkdir()
        if sys.platform == "win32":
            _winapi.CreateJunction(str(target), str(link))
        else:
            link.symlink_to(target, target_is_directory=True)
        env["PSX_USER_DIR"] = str(link / ".." / "profile")
        subprocess.run([profile, "--prepare-profile"], cwd=folder, env=env, check=True)
        if not (target.parent / "profile").is_dir() or (link.parent / "profile").exists():
            raise SystemExit("The launcher profile was not created in the linked directory's physical parent")
    print("Release guide and linked profile: PASS")


if __name__ == "__main__":
    check_release_inputs()
