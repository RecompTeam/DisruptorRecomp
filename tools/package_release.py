"""Package a release with Velopack: on Windows Setup.exe and a portable ZIP, on Linux an AppImage, each with the
packages the launcher updates from.

The program's files come from an explicit list and exclude game data. The packages carry the release's section of
CHANGELOG.md as their notes. The GitHub release's text is release/notes/<version>.md when it is written, else that
section, with explicit anchors for its links to its own headings. The previous release on GitHub, when there is one, gives the update a delta package. Nothing is
published unless --publish is given. The players' files are signed with Sigstore first: a browser opens to sign in as
the organisation's account, and each gets a bundle and a line of SLSA provenance. Windows comes first and makes
the release as a draft, Linux adds its files and publishes it, so players never see a release without them.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import re
import shutil
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "https://github.com/RecompTeam/DisruptorRecomp"
PACKAGE_ID = "DisruptorRecomp"
# The packer must match the Velopack C library the launcher links (src/launcher/CMakeLists.txt).
VPK = "vpk@1.2.161"


@dataclass(frozen=True)
class Platform:
    """What a release for one system is packed from and into."""

    name: str
    channel: str  # Velopack's, in the names of its files
    executable_suffix: str
    player_files: tuple[str, ...]
    install: str  # the release text's lines for this system


PLATFORMS = {
    "windows": Platform(
        "windows",
        "win",
        ".exe",
        (f"{PACKAGE_ID}-win-Setup.exe", f"{PACKAGE_ID}-win-Portable.zip"),
        "**Windows 10 or 11 x64**\n\n"
        f"- Download **{PACKAGE_ID}-win-Setup.exe** and run it\n"
        "- It installs the game into `%LocalAppData%\\DisruptorRecomp`, with shortcuts on the desktop and in the Start "
        "menu\n"
        "- Your files are kept in `%LocalAppData%\\DisruptorRecompData`\n"
        f"- Or, without installing, unpack **{PACKAGE_ID}-win-Portable.zip** into a folder of its own and run "
        "`Disruptor Recompiled.exe`\n"
        "- The portable copy needs the Microsoft Visual C++ x64 Redistributable\n"
        "- The portable copy keeps your files in its `data` folder\n",
    ),
    "linux": Platform(
        "linux",
        "linux",
        "",
        (f"{PACKAGE_ID}.AppImage",),
        "**Linux x64, glibc 2.35 or newer**\n\n"
        f"- Download **{PACKAGE_ID}.AppImage**\n"
        f"- Make it executable: `chmod +x {PACKAGE_ID}.AppImage`\n"
        "- Run it\n"
        "- Your files are kept in `~/.local/share/DisruptorRecompData`\n",
    ),
}
SIGSTORE = "sigstore@4.5.0"
SIGNER = "recomp.team@gmail.com"
SIGNER_ISSUER = "https://accounts.google.com"
SLSA_PROVENANCE = "https://slsa.dev/provenance/v1"
NUGET_CONFIG = """<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <packageSources>
    <clear />
    <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
  </packageSources>
</configuration>
"""


def release_notes(version: str, root: Path = ROOT) -> str:
    """The `## <version>` section of CHANGELOG.md, without its heading."""
    text = (root / "CHANGELOG.md").read_text(encoding="utf-8").replace("\r\n", "\n")
    match = re.search(rf"^## {re.escape(version)}\n(.*?)(?=^## |\Z)", text, re.MULTILINE | re.DOTALL)
    if not match or not match.group(1).strip():
        raise SystemExit(f"CHANGELOG.md has no section for {version}: add `## {version}` with its changes first")
    return match.group(1).strip() + "\n"


HEADING = re.compile(r"^ {0,3}#{1,6}[ \t]+(.+?)(?:[ \t]+#+)?[ \t]*$")
HEADING_LINK = re.compile(r"\]\(#(?:user-content-)?([^)\s]+)")
REFERENCE_LINK = re.compile(r"^ {0,3}\[[^\]]+\]:[ \t]*#")
ANCHOR = re.compile(r'^<a id="([^"]+)"></a>$')
FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})")
INSTALL_ANCHOR = "install"


def heading_slug(heading: str) -> str:
    """The id GitHub gives a heading elsewhere: its words in lower case without punctuation, joined by hyphens."""
    return re.sub(r"[^\w\- ]", "", heading.strip().lower()).replace(" ", "-")


def with_anchors(text: str) -> str:
    """`text` whose links to its own headings work on a release page, which gives headings no ids: each linked
    heading gets an `<a id>` on the line before it, which GitHub prefixes with `user-content-`, and the links name
    that. An anchor already in the text is kept, so a heading reworded after its anchor still has it. Fenced code is
    left alone, `#install` is the section the packager adds, and a link it cannot tie to a heading stops the
    release."""
    lines = text.split("\n")
    prose, fence = [], ""
    for line in lines:
        mark = FENCE.match(line)
        prose.append(not fence and not mark)
        if mark and (not fence or mark.group(1)[0] == fence[0] and len(mark.group(1)) >= len(fence)):
            fence = "" if fence else mark.group(1)
    shown = [line for line, words in zip(lines, prose, strict=True) if words]
    if any(REFERENCE_LINK.match(line) for line in shown):
        raise SystemExit("The release text links to a heading by reference: write it as [text](#heading)")
    wanted = {slug for line in shown for slug in HEADING_LINK.findall(line)}
    anchored = {INSTALL_ANCHOR} | {match.group(1) for line in shown if (match := ANCHOR.match(line))}
    out = []
    for line, words in zip(lines, prose, strict=True):
        heading = HEADING.match(line) if words else None
        if heading and (slug := heading_slug(heading.group(1))) in wanted - anchored:
            out.append(f'<a id="{slug}"></a>')
            anchored.add(slug)
        out.append(HEADING_LINK.sub(r"](#user-content-\1", line) if words else line)
    if missing := sorted(wanted - anchored):
        raise SystemExit(f"The release text links to headings it does not have: {', '.join(missing)}")
    return "\n".join(out)


def release_text(version: str, root: Path = ROOT) -> str:
    """The GitHub release's text above Install: release/notes/<version>.md when it is written, else the
    version's section of CHANGELOG.md, with anchors for its links to its own headings."""
    page = root / "release" / "notes" / f"{version}.md"
    if not page.is_file():
        return with_anchors(release_notes(version, root))
    text = page.read_text(encoding="utf-8").replace("\r\n", "\n").strip()
    if not text:
        raise SystemExit(f"{page.name} is empty: write the release's text or remove the file")
    if re.search(r"^ {0,3}#{1,2}[ \t]+Install(?:[ \t]+#*)?[ \t]*$", text, re.MULTILINE | re.IGNORECASE):
        raise SystemExit(f"{page.name} has an Install heading of its own: the packager writes that section")
    return with_anchors(text) + "\n"


def published_names(channel: str, players: list[str], packed: tuple[str, ...], signed: list[str]) -> set[str]:
    """The files a release carries: the players' files, the update feed, its packages and the players' signatures.
    GitHub shows each file's SHA-256 itself, so no digest files. Velopack's assets list and its legacy RELEASES file stay
    out: no updater reads them."""
    return {*players, f"releases.{channel}.json", *packed, *signed}


INSTALL = f'<a id="{INSTALL_ANCHOR}"></a>\n## Install\n\nYour own Disruptor (USA, SLUS-00224) disc image is needed\n'


def release_body(text: str) -> str:
    """What Windows publishes: the release's text, then how to install it on Windows."""
    return text + "\n" + INSTALL + "\n" + PLATFORMS["windows"].install


def with_install(body: str, platform: Platform) -> str:
    """`body` with `platform`'s install lines at the end of its Install section, the last one, and only once."""
    heading = "\n## Install\n"
    if heading not in body:
        raise SystemExit(f"The release text has no Install section for the {platform.name} lines")
    if any(line.startswith("## ") for line in body[body.index(heading) + len(heading) :].splitlines()):
        raise SystemExit(f"The release text has a section after Install, where the {platform.name} lines would land")
    return body if platform.install in body else body.rstrip("\n") + "\n\n" + platform.install


def create_command(version: str, repository: str, commit: str, body: Path, uploads: list[str]) -> list[str]:
    """The release Windows makes at `commit`: a draft, not a pre-release. GitHub tags the commit when it is published."""
    return [
        "gh",
        "release",
        "create",
        version,
        "--repo",
        repository,
        "--target",
        commit,
        "--draft",
        "--title",
        version.removeprefix("v"),
        "--notes-file",
        str(body),
        *uploads,
    ]


def finish_command(version: str, repository: str, body: Path, draft: bool) -> list[str]:
    """Linux's edit of the release text. A draft is published with it as a regular release and the latest, a release
    already published keeps its place."""
    publish = ["--draft=false", "--latest"] if draft else []
    return ["gh", "release", "edit", version, "--repo", repository, "--notes-file", str(body), *publish]


def now() -> str:
    """The time of now as RFC 3339 in UTC."""
    return datetime.datetime.now(datetime.UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def provenance(version: str, commit: str, started: str) -> dict:
    """An SLSA v1 provenance predicate for a build by this script from `commit` on the releasing machine."""
    builder = f"{REPOSITORY}/blob/{commit}/tools/package_release.py"
    return {
        "buildDefinition": {
            "buildType": builder,
            "externalParameters": {"repository": REPOSITORY, "version": version},
            "resolvedDependencies": [{"uri": f"git+{REPOSITORY}@refs/tags/{version}", "digest": {"gitCommit": commit}}],
        },
        "runDetails": {"builder": {"id": builder}, "metadata": {"startedOn": started, "finishedOn": now()}},
    }


def sign(work: Path, files: list[Path], version: str, commit: str, started: str, channel: str) -> list[Path]:
    """Attests `files` with Sigstore, checks each bundle names the organisation, and returns the bundles and the
    provenance lines that go with the release."""
    predicate = work / "provenance.json"
    predicate.write_text(json.dumps(provenance(version, commit, started), indent=2), encoding="utf-8")
    subprocess.run(
        ["uvx", SIGSTORE, "attest", "--overwrite", "--predicate", str(predicate), "--predicate-type", SLSA_PROVENANCE]
        + [str(file) for file in files],
        check=True,
    )
    bundles = [file.with_name(f"{file.name}.sigstore.json") for file in files]
    for file, bundle in zip(files, bundles, strict=True):
        if not bundle.is_file():
            raise SystemExit(f"Sigstore left no {bundle.name}")
        checked = subprocess.run(
            ["uvx", SIGSTORE, "verify", "identity", "--cert-identity", SIGNER, "--cert-oidc-issuer", SIGNER_ISSUER]
            + ["--bundle", str(bundle), str(file)],
            capture_output=True,
            text=True,
            check=False,
        )
        if checked.returncode != 0:
            raise SystemExit(f"{bundle.name} is not signed by {SIGNER}: sign in with that account\n{checked.stderr}")
    statements = work / "packages" / f"{PACKAGE_ID}-{channel}.intoto.jsonl"
    statements.write_text(
        "".join(
            json.dumps(json.loads(bundle.read_text(encoding="utf-8"))["dsseEnvelope"], separators=(",", ":")) + "\n"
            for bundle in bundles
        ),
        encoding="utf-8",
    )
    return [*bundles, statements]


def source_commit() -> str:
    """The commit the checkout stands on."""
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def check_build_commit(build: Path, platform: Platform, signing: bool) -> None:
    """Stops when the build may not be the checkout's commit: a signed release names that commit."""
    if signing:
        changed = subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=no"], cwd=ROOT, text=True
        ).strip()
        if changed:
            raise SystemExit("The checkout has uncommitted changes: commit them, build again, then sign")
    stamp = build / "SOURCE_COMMIT"
    if platform.name == "linux" and not stamp.is_file():
        raise SystemExit(f"{stamp} is missing: the Linux build must say which commit it was made from")
    if stamp.is_file() and stamp.read_text(encoding="utf-8").strip() != source_commit():
        raise SystemExit(f"{build} was built from {stamp.read_text(encoding='utf-8').strip()}, not {source_commit()}")


def check_executable(name: str, binary: bytes, platform: Platform) -> None:
    """Stops unless `binary` is an x64 program for `platform`."""
    if platform.name == "linux":
        if not binary.startswith(b"\x7fELF") or len(binary) < 20 or binary[4] != 2 or binary[18:20] != b"\x3e\x00":
            raise SystemExit(f"Expected an x64 Linux executable: {name}")
        return
    if len(binary) < 64 or not binary.startswith(b"MZ"):
        raise SystemExit(f"Expected a Windows executable: {name}")
    pe = struct.unpack_from("<I", binary, 0x3C)[0]
    if (
        pe + 6 > len(binary)
        or binary[pe : pe + 4] != b"PE\0\0"
        or struct.unpack_from("<H", binary, pe + 4)[0] != 0x8664
    ):
        raise SystemExit(f"Expected an x64 Windows executable: {name}")


def icon_png(size: int = 256) -> bytes:
    """The launcher icon's picture of `size` pixels, a PNG inside app.ico."""
    icon = (ROOT / "src/launcher/assets/app.ico").read_bytes()
    for entry in range(struct.unpack_from("<H", icon, 4)[0]):
        width, length, offset = icon[6 + 16 * entry] or 256, *struct.unpack_from("<II", icon, 6 + 16 * entry + 8)
        if width == size and icon[offset : offset + 4] == b"\x89PNG":
            return icon[offset : offset + length]
    raise SystemExit(f"app.ico has no {size} pixel PNG")


def payload_of(build: Path, version: str, zstd_license: Path, platform: Platform) -> dict[str, bytes]:
    """The program's files by their place in the installed folder."""
    game, launcher = (f"{name}{platform.executable_suffix}" for name in ("DisruptorRecompiled", "DisruptorLauncher"))
    files = {
        game: build / game,
        launcher: build / launcher,
        "game.toml": ROOT / "game.toml",
        "mouse-aim.ini": ROOT / "mouse-aim.ini",
        "keybinds.ini": ROOT / "config/keybinds-modern.ini",
        "GETTING_STARTED.md": ROOT / "docs/PLAYING.md",
        "CHANGELOG.md": ROOT / "CHANGELOG.md",
        "bios/openbios.bin": build / "bios/openbios.bin",
        "bios/OpenBIOS.LICENSE": build / "bios/OpenBIOS.LICENSE",
        "LICENSE.txt": ROOT / "LICENSE",
        "licenses/PSXRecomp-LICENSE.txt": ROOT / "licenses/PSXRecomp-LICENSE.txt",
        "licenses/Dear-ImGui-LICENSE.txt": ROOT / "licenses/Dear-ImGui-LICENSE.txt",
        "licenses/Velopack-LICENSE.txt": ROOT / "licenses/Velopack-LICENSE.txt",
        "licenses/Inter-LICENSE.txt": ROOT / "licenses/Inter-LICENSE.txt",
        "licenses/Lucide-LICENSE.txt": ROOT / "licenses/Lucide-LICENSE.txt",
        "licenses/FreeType-LICENSE.txt": ROOT / "licenses/FreeType-LICENSE.txt",
        "licenses/SDL-LICENSE.txt": build / "_deps/sdl3-src/LICENSE.txt",
        "licenses/SDL-hidapi-BSD-LICENSE.txt": build / "_deps/sdl3-src/src/hidapi/LICENSE-bsd.txt",
        "licenses/SDL-yuv2rgb-LICENSE.txt": build / "_deps/sdl3-src/src/video/yuv2rgb/LICENSE",
        "licenses/libchdr-LICENSE.txt": build / "_deps/psx_libchdr-src/LICENSE.txt",
        "licenses/LZMA-LICENSE.txt": build / "_deps/psx_libchdr-src/deps/lzma-25.01/LICENSE",
        "licenses/Zstd-LICENSE.txt": zstd_license,
    }
    payload = {name: path.read_bytes() for name, path in files.items()}
    if len(payload["bios/openbios.bin"]) != 524288:
        raise SystemExit("Expected the bundled 512 KiB OpenBIOS image")
    for name in (game, launcher):
        check_executable(name, payload[name], platform)

    chdr = build / "_deps/psx_libchdr-src"
    notices = set()
    for path in sorted(chdr.rglob("*")):
        if path.suffix not in {".c", ".h"}:
            continue
        for comment in re.findall(r"/\*.*?\*/", path.read_text(errors="replace"), re.DOTALL):
            if re.search(
                r"copyright|copyright-holders|permission is hereby|license:|Redistribution and use",
                comment,
                re.IGNORECASE,
            ):
                notices.add(comment)
    payload["licenses/libchdr-NOTICES.txt"] = (
        "Notices from libchdr and its bundled codecs:\n\n" + "\n\n".join(sorted(notices)) + "\n"
    ).encode("utf-8")
    payload["THIRD_PARTY_NOTICES.md"] = (
        b"# Third-party notices\n\n"
        b"PSXRecomp: copyright Matthew Stan; PolyForm Noncommercial 1.0.0.\n"
        b"Dear ImGui: copyright Omar Cornut and contributors; MIT.\n"
        b"Velopack: copyright Caelan Sayler and Velopack Ltd.; MIT.\n"
        b"Inter typeface: copyright The Inter Project Authors; SIL OFL 1.1.\n"
        b"Lucide icons: copyright Lucide Icons and Contributors; ISC.\n"
        b"FreeType: FreeType License. Portions of this software are copyright\n"
        b"(c) 2026 The FreeType Project (https://freetype.org). All rights reserved.\n"
        b"SDL: copyright Sam Lantinga and contributors; zlib, with bundled\n"
        b"hidapi and YUV conversion notices included.\n"
        b"libchdr and its codecs: BSD/MIT/public-domain notices included.\n"
        b"Zstandard: copyright Meta Platforms, Inc. and affiliates; BSD.\n"
        b"OpenBIOS/PCSX-Redux and uC-sdk: see bios/OpenBIOS.LICENSE.\n\n"
        b"Full license texts are in licenses/ and bios/. Game assets are not\n"
        b"included; supply your own supported Disruptor disc image.\n"
    )
    commit = source_commit()
    payload["VERSION.txt"] = f"{version}\nSource commit: {commit}\n".encode()
    payload["MANIFEST.json"] = (
        json.dumps(
            {
                "version": version,
                "source_commit": commit,
                "files": {name: hashlib.sha256(data).hexdigest() for name, data in sorted(payload.items())},
            },
            indent=2,
        ).encode()
        + b"\n"
    )
    return payload


def add_to_release(
    work: Path, repository: str, version: str, commit: str, platform: Platform, uploads: list[str]
) -> None:
    """Uploads `platform`'s files to the draft Windows made from the same commit, adds its install lines and
    publishes it."""
    shown = subprocess.run(
        ["gh", "release", "view", version, "--repo", repository, "--json", "body,targetCommitish,isDraft"],
        capture_output=True,
        text=True,
        check=False,
    )
    if shown.returncode != 0:
        raise SystemExit(f"{version} has no release yet: publish the Windows build first")
    release = json.loads(shown.stdout)
    if release["targetCommitish"] != commit:
        raise SystemExit(f"{version} was made from {release['targetCommitish']}, this build from {commit}")
    body = with_install(release["body"].replace("\r\n", "\n"), platform)
    (work / "body.md").write_text(body, encoding="utf-8")
    # A publish run again after a failure replaces what it uploaded and finishes the text.
    subprocess.run(["gh", "release", "upload", version, "--repo", repository, "--clobber", *uploads], check=True)
    subprocess.run(finish_command(version, repository, work / "body.md", release["isDraft"]), check=True)


def vpk(work: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    """Runs the pinned Velopack packer through the .NET SDK's tool runner."""
    command = [
        "dotnet",
        "dnx",
        VPK,
        "--yes",
        "--allow-roll-forward",
        "--configfile",
        str(work / "nuget.config"),
        "--",
        *arguments,
    ]
    return subprocess.run(command, cwd=work, capture_output=True, text=True, check=False)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "build/releases")
    parser.add_argument("--zstd-license", type=Path, required=True)
    parser.add_argument("--platform", choices=sorted(PLATFORMS), default="windows", help="the system the build is for")
    parser.add_argument(
        "--no-delta", action="store_true", help="do not fetch the previous release to build a delta package"
    )
    parser.add_argument("--publish", action="store_true", help="create the GitHub release and upload the packages")
    parser.add_argument("--sign", action="store_true", help="sign the packages without publishing them")
    args = parser.parse_args()
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?", args.version):
        parser.error("version must be a release tag such as v0.3.1")

    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    cache_entries = {
        line.split(":", 1)[0]: line.split("=", 1)[1]
        for line in cache.splitlines()
        if not line.startswith(("#", "//")) and ":" in line and "=" in line
    }
    for name, value in {
        "CMAKE_BUILD_TYPE": "Release",
        "PSX_DEBUG_TOOLS": "OFF",
        "PSX_GAME_VERSION": args.version[1:],
    }.items():
        if cache_entries.get(name) != value:
            raise SystemExit(f"Release build setting missing: {name}={value}")
    notes = release_notes(args.version)
    text = release_text(args.version)
    if shutil.which("dotnet") is None:
        raise SystemExit("Velopack packs with the .NET SDK: install it, then run again")
    platform = PLATFORMS[args.platform]
    check_build_commit(build, platform, args.publish or args.sign)

    started = now()
    work = (args.output / args.version / platform.name).resolve()
    shutil.rmtree(work, ignore_errors=True)
    stage, packages = work / "program", work / "packages"
    stage.mkdir(parents=True)
    packages.mkdir()
    payload = payload_of(build, args.version, args.zstd_license, platform)
    for name, data in payload.items():
        (stage / name).parent.mkdir(parents=True, exist_ok=True)
        (stage / name).write_bytes(data)
    (work / "nuget.config").write_text(NUGET_CONFIG, encoding="utf-8")
    (work / "notes.md").write_text(notes, encoding="utf-8")

    directive = ["[linux]"] if platform.name == "linux" else []
    if not args.no_delta:
        fetching = vpk(
            work,
            *directive,
            "download",
            "github",
            "--repoUrl",
            REPOSITORY,
            "--pre",
            "--channel",
            platform.channel,
            "--outputDir",
            str(packages),
        )
        if fetching.returncode != 0:
            raise SystemExit(f"Fetching the previous release failed, --no-delta skips it:\n{fetching.stderr[-2000:]}")
        fetched = any(packages.glob("*-full.nupkg"))
        print(
            "Previous release fetched for a delta package."
            if fetched
            else "No previous Velopack release: no delta package."
        )
    if platform.name == "linux":
        (work / "app.png").write_bytes(icon_png())
        system = ["--mainExe", "DisruptorLauncher", "--icon", str(work / "app.png"), "--runtime", "linux-x64"]
        system += ["--categories", "Game"]
    else:
        system = ["--mainExe", "DisruptorLauncher.exe", "--icon", str(ROOT / "src/launcher/assets/app.ico")]
        system += ["--framework", "vcredist143-x64"]
    packed = vpk(
        work,
        *directive,
        "pack",
        "--packId",
        PACKAGE_ID,
        "--packVersion",
        args.version[1:],
        "--packDir",
        str(stage),
        *system,
        "--packTitle",
        "Disruptor Recompiled",
        "--packAuthors",
        "RecompTeam",
        "--releaseNotes",
        str(work / "notes.md"),
        "--outputDir",
        str(packages),
    )
    if packed.returncode != 0:
        raise SystemExit(f"vpk pack failed:\n{packed.stdout[-3000:]}\n{packed.stderr[-3000:]}")
    players = [packages / name for name in platform.player_files]
    digests = {}
    for player_file in players:
        if not player_file.is_file():
            raise SystemExit(f"vpk pack left no {player_file.name}")
        digests[player_file.name] = hashlib.sha256(player_file.read_bytes()).hexdigest()
    print(f"Packaged {len(payload)} files into {packages}")
    for item in sorted(packages.iterdir()):
        print(f"{item.stat().st_size:>12,}  {item.name}")
    for name, digest in digests.items():
        print(f"{name} SHA-256: {digest}")

    channel_part = "" if platform.channel == "win" else f"-{platform.channel}"
    packed_names = tuple(f"{PACKAGE_ID}-{args.version[1:]}{channel_part}-{kind}.nupkg" for kind in ("full", "delta"))
    commit = json.loads(payload["MANIFEST.json"])["source_commit"]
    signed: list[Path] = []
    if args.publish or args.sign:
        signed = sign(work, players, args.version, commit, started, platform.channel)
        print(f"Signed {len(players)} files as {SIGNER}")

    if args.publish:
        ours = published_names(platform.channel, list(digests), packed_names, [item.name for item in signed])
        uploads = [str(item) for item in sorted(packages.iterdir()) if item.name in ours]
        repository = REPOSITORY.removeprefix("https://github.com/")
        if platform.name == "linux":
            add_to_release(work, repository, args.version, commit, platform, uploads)
            return
        (work / "body.md").write_text(release_body(text), encoding="utf-8")
        tagged = subprocess.run(
            ["git", "ls-remote", "--tags", REPOSITORY, f"refs/tags/{args.version}"],
            capture_output=True,
            text=True,
            check=True,
        )
        if tagged.stdout.strip():
            raise SystemExit(
                f"{args.version} is tagged on GitHub already: the release would not be this build's commit"
            )
        existing = subprocess.run(
            ["gh", "release", "view", args.version, "--repo", repository], capture_output=True, text=True, check=False
        )
        if existing.returncode == 0:
            raise SystemExit(
                f"{args.version} has a release or a draft already: delete a draft left by a failed run with "
                f"`gh release delete {args.version} --repo {repository}`, then run again"
            )
        subprocess.run(create_command(args.version, repository, commit, work / "body.md", uploads), check=True)


if __name__ == "__main__":
    main()
