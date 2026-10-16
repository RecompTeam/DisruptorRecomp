#!/usr/bin/env python3
"""Test of the packager's GitHub release: its text, the written page or the changelog section, and its files."""

import importlib
import sys
import tempfile
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
    for heading in ("## Install", "## Verify", "## Verify  ", "## Verify ##", "   ## Install", "# install"):
        page.write_text(f"Banner\n\n{heading}\n\nMine\n", encoding="utf-8")
        require(refused(lambda: package_release.release_text(newest, root)), f"a page with '{heading}' is refused")
    kept = "See the Verify section below.\n\n### Install notes\n\n### Verify\n\n    ## Install\n"
    page.write_text(kept, encoding="utf-8")
    require(
        package_release.release_text(newest, root) == kept,
        "a mention of Verify, a smaller heading and an indented code line are no section of the packager's",
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
    names == {*players, "DisruptorRecomp.AppImage.sha256", "releases.linux.json", *packed, *signed},
    "a release carries the players' files with their digests, the update feed, its packages and the signatures",
)
require(
    not names & {"assets.linux.json", "RELEASES-linux", "releases.win.json"},
    "Velopack's assets list, its legacy RELEASES file and another channel's feed stay out",
)

print("Disruptor release text and files: PASS")
