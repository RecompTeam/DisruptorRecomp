"""Make the CHD images the launcher's tests read, with MAME's chdman, from sectors of a fixed pattern.

None of them holds game data. The test's profile takes the SHA-256 this prints for the data track.
"""

import argparse
import hashlib
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW_SECTOR = 2352
COOKED_SECTOR = 2048


def pattern(length: int, seed: int) -> bytes:
    """Bytes no real sector starts with, the same on every run."""
    return bytes((at * 31 + seed) % 256 for at in range(length))


@dataclass(frozen=True)
class Track:
    """One track of a fixture: its CUE mode, its sectors and its file."""

    mode: str
    sectors: int
    seed: int

    def data(self) -> bytes:
        """The track's file, of sectors as its mode sizes them."""
        size = COOKED_SECTOR if self.mode.endswith("/2048") else RAW_SECTOR
        return pattern(size * self.sectors, self.seed)


FIXTURES = {
    "one_track": (Track("MODE2/2352", 10, 7),),
    "two_tracks": (Track("MODE2/2352", 10, 7), Track("AUDIO", 4, 11)),
    "cooked": (Track("MODE1/2048", 10, 7),),
}


class FixtureMaker:
    """Writes each fixture's CUE and files to a scratch folder and hands them to chdman."""

    def __init__(self, chdman: Path, output: Path) -> None:
        self.chdman = chdman
        self.output = output

    def make(self, name: str, tracks: tuple[Track, ...], scratch: Path) -> None:
        lines = []
        for number, track in enumerate(tracks, start=1):
            file = f"{name}-{number}.bin"
            (scratch / file).write_bytes(track.data())
            lines += [f'FILE "{file}" BINARY', f"  TRACK {number:02} {track.mode}", "    INDEX 01 00:00:00"]
        cue = scratch / f"{name}.cue"
        cue.write_text("\r\n".join(lines) + "\r\n", encoding="ascii")
        target = self.output / f"{name}.chd"
        subprocess.run([self.chdman, "createcd", "-i", cue, "-o", target, "-f"], check=True, capture_output=True)
        print(f"{target.name}: {target.stat().st_size} bytes")

    def make_all(self) -> None:
        self.output.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory() as scratch:
            for name, tracks in FIXTURES.items():
                self.make(name, tracks, Path(scratch))
        data = FIXTURES["one_track"][0].data()
        print(f"one_track data track: {len(data)} bytes, SHA-256 {hashlib.sha256(data).hexdigest()}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("chdman", type=Path, help="chdman of a MAME release")
    parser.add_argument("--output", type=Path, default=ROOT / "tests" / "fixtures" / "chd")
    arguments = parser.parse_args()
    FixtureMaker(arguments.chdman, arguments.output).make_all()


if __name__ == "__main__":
    main()
