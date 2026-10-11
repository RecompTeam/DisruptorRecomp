"""Package the Windows runtime from an explicit file list, excluding game data."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "build/releases")
    parser.add_argument("--zstd-license", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?", args.version):
        parser.error("version must be a release tag such as v0.1.0-alpha.1")

    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    cache_entries = {line.split(":", 1)[0]: line.split("=", 1)[1]
                     for line in cache.splitlines()
                     if not line.startswith(("#", "//")) and ":" in line and "=" in line}
    for name, value in {"CMAKE_BUILD_TYPE": "Release", "PSX_DEBUG_TOOLS": "OFF",
                        "PSX_GAME_VERSION": args.version[1:]}.items():
        if cache_entries.get(name) != value:
            raise SystemExit(f"Release build setting missing: {name}={value}")
    files = {
        "DisruptorRecompiled.exe": build / "DisruptorRecompiled.exe",
        "DisruptorLauncher.exe": build / "DisruptorLauncher.exe",
        "game.toml": ROOT / "game.toml",
        "mouse-aim.ini": ROOT / "mouse-aim.ini",
        "keybinds.ini": ROOT / "config/keybinds-modern.ini",
        "settings.toml": ROOT / "release/windows/settings.toml",
        "Play Disruptor.cmd": ROOT / "release/windows/Play Disruptor.cmd",
        "GETTING_STARTED.md": ROOT / "docs/PLAYING.md",
        "bios/openbios.bin": build / "bios/openbios.bin",
        "bios/OpenBIOS.LICENSE": build / "bios/OpenBIOS.LICENSE",
        "licenses/PSXRecomp-LICENSE.txt": ROOT / "licenses/PSXRecomp-LICENSE.txt",
        "licenses/Dear-ImGui-LICENSE.txt": ROOT / "licenses/Dear-ImGui-LICENSE.txt",
        "licenses/SDL-LICENSE.txt": build / "_deps/sdl3-src/LICENSE.txt",
        "licenses/SDL-hidapi-BSD-LICENSE.txt": build / "_deps/sdl3-src/src/hidapi/LICENSE-bsd.txt",
        "licenses/SDL-yuv2rgb-LICENSE.txt": build / "_deps/sdl3-src/src/video/yuv2rgb/LICENSE",
        "licenses/libchdr-LICENSE.txt": build / "_deps/psx_libchdr-src/LICENSE.txt",
        "licenses/LZMA-LICENSE.txt": build / "_deps/psx_libchdr-src/deps/lzma-25.01/LICENSE",
        "licenses/Zstd-LICENSE.txt": args.zstd_license,
    }
    payload = {name: path.read_bytes() for name, path in files.items()}
    if len(payload["bios/openbios.bin"]) != 524288:
        raise SystemExit("Expected the bundled 512 KiB OpenBIOS image")
    for name in ("DisruptorRecompiled.exe", "DisruptorLauncher.exe"):
        binary = payload[name]
        if len(binary) < 64 or not binary.startswith(b"MZ"):
            raise SystemExit(f"Expected a Windows executable: {name}")
        pe = struct.unpack_from("<I", binary, 0x3C)[0]
        if (pe + 6 > len(binary) or binary[pe:pe+4] != b"PE\0\0" or
                struct.unpack_from("<H", binary, pe+4)[0] != 0x8664):
            raise SystemExit(f"Expected an x64 Windows executable: {name}")

    chdr = build / "_deps/psx_libchdr-src"
    notices = set()
    for path in sorted(chdr.rglob("*")):
        if path.suffix not in {".c", ".h"}:
            continue
        for comment in re.findall(r"/\*.*?\*/", path.read_text(errors="replace"), re.S):
            if re.search(r"copyright|copyright-holders|permission is hereby|license:|Redistribution and use", comment, re.I):
                notices.add(comment)
    payload["licenses/libchdr-NOTICES.txt"] = (
        "Notices from libchdr and its bundled codecs:\n\n" +
        "\n\n".join(sorted(notices)) + "\n"
    ).encode("utf-8")
    payload["THIRD_PARTY_NOTICES.md"] = (
        "# Third-party notices\n\n"
        "PSXRecomp: copyright Matthew Stan; PolyForm Noncommercial 1.0.0.\n"
        "Dear ImGui: copyright Omar Cornut and contributors; MIT.\n"
        "SDL: copyright Sam Lantinga and contributors; zlib, with bundled\n"
        "hidapi and YUV conversion notices included.\n"
        "libchdr and its codecs: BSD/MIT/public-domain notices included.\n"
        "Zstandard: copyright Meta Platforms, Inc. and affiliates; BSD.\n"
        "OpenBIOS/PCSX-Redux and uC-sdk: see bios/OpenBIOS.LICENSE.\n\n"
        "Full license texts are in licenses/ and bios/. Game assets are not\n"
        "included; supply your own supported Disruptor disc image.\n"
    ).encode("utf-8")
    payload["input/PUT_YOUR_DISC_HERE.txt"] = (
        "Run DisruptorLauncher.exe and browse to your own USA disc image.\r\n"
        "The launcher copies and verifies it here automatically.\r\n"
        "See GETTING_STARTED.md one folder above.\r\n"
    ).encode("utf-8")
    source_commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    payload["VERSION.txt"] = f"{args.version}\nSource commit: {source_commit}\n".encode()
    payload["MANIFEST.json"] = json.dumps({
        "version": args.version,
        "source_commit": source_commit,
        "files": {name: hashlib.sha256(data).hexdigest()
                  for name, data in sorted(payload.items())},
    }, indent=2).encode() + b"\n"

    args.output.mkdir(parents=True, exist_ok=True)
    stem = f"DisruptorRecomp-{args.version}-win64"
    archive = args.output / f"{stem}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in sorted(payload.items()):
            z.writestr(f"{stem}/{name}", data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum = archive.with_suffix(".zip.sha256")
    checksum.write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(f"Packaged {len(payload)} files: {archive} ({archive.stat().st_size:,} bytes)")
    print(f"SHA-256: {digest}")


if __name__ == "__main__":
    main()
