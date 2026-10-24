#!/usr/bin/env python3
"""Export SLUS-00224's environment tile tables as editable PNGs (requires Pillow).

The loader at 0x80015398 uploads two rectangles from each level's first
0x73800 bytes. Header word 70 counts the 8-byte world texture descriptors
at the start of the next block. Each descriptor names a 32x32 tile:
u, v, CLUT, last_u, v, texture_page. See docs/TEXTURES.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw


DISC_SHA256 = "3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4"
DISC_BYTES = 636350064
SECTOR = 2048
RAW_SECTOR = 2352
PICTURES_END = 0x73800
TILE_SIZE = 32
ATLAS_COLUMNS = 16
# (offset within level piece, VRAM x in 16-bit words, y, width in words, height)
UPLOADS = ((0x800, 0, 480, 320, 32), (0x5800, 320, 192, 704, 320))


class MissingVRAM(ValueError):
    """A descriptor references pixels not present in the level's disc data."""


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


class Disc:
    def __init__(self, path: Path):
        self.file = path.open("rb")
        self.size = path.stat().st_size

    def read(self, lba: int, size: int) -> bytes:
        if lba < 0 or size < 0 or (lba + (size + SECTOR - 1) // SECTOR) * RAW_SECTOR > self.size:
            raise ValueError("Read extends outside the disc image")
        out = bytearray()
        for sector in range((size + SECTOR - 1) // SECTOR):
            self.file.seek((lba + sector) * RAW_SECTOR + 24)
            block = self.file.read(SECTOR)
            if len(block) != SECTOR:
                raise ValueError("Incomplete disc sector")
            out.extend(block)
        return bytes(out[:size])

    def wad(self) -> tuple[int, int]:
        descriptor = self.read(16, SECTOR)
        if descriptor[:7] != b"\x01CD001\x01":
            raise ValueError("Expected a MODE2/2352 ISO9660 disc image")
        directory = self.read(u32(descriptor, 158), u32(descriptor, 166))
        at = 0
        while at < len(directory):
            length = directory[at]
            if length == 0:
                at = (at // SECTOR + 1) * SECTOR
                continue
            record = directory[at:at + length]
            if len(record) < 34 or 33 + record[32] > len(record):
                raise ValueError("Invalid ISO9660 directory record")
            name = record[33:33 + record[32]].split(b";")[0]
            if name == b"WAD.IN":
                return u32(record, 2), u32(record, 10)
            at += length
        raise ValueError("The disc has no WAD.IN")


def source_offset(x: int, y: int) -> int:
    """Map a VRAM word to its original byte offset within the level piece."""
    for offset, left, top, width, height in UPLOADS:
        if left <= x < left + width and top <= y < top + height:
            return offset + ((y - top) * width + x - left) * 2
    raise MissingVRAM(f"VRAM word ({x}, {y}) is outside the level's uploads")


def rgba(word: int) -> tuple[int, int, int, int]:
    def eight(five: int) -> int:
        return (five << 3) | (five >> 2)

    # PSX zero is transparent; bit 15 controls blending only when the draw enables it.
    return (eight(word & 31), eight((word >> 5) & 31),
            eight((word >> 10) & 31), 0 if word == 0 else 255)


def decode_tile(pictures: bytes, record: bytes) -> tuple[Image.Image, dict, dict]:
    u, v, clut, last_u, last_v, page = struct.unpack("<BBHBBH", record)
    if (last_u - u) % 256 != 31 or last_v != v:
        raise ValueError("Unexpected world texture bounds; expected a 32x32 descriptor")
    mode = (page >> 7) & 3
    if mode not in (0, 1):
        raise ValueError(f"Unsupported environment tile pixel mode {mode}")
    bits = 4 if mode == 0 else 8
    per_word = 16 // bits
    page_x, page_y = (page & 15) * 64, ((page >> 4) & 1) * 256
    palette_x, palette_y = (clut & 63) * 16, clut >> 6
    palette_offsets = [source_offset(palette_x + i, palette_y) for i in range(1 << bits)]
    colours = [struct.unpack_from("<H", pictures, offset)[0] for offset in palette_offsets]
    palette = [rgba(word) for word in colours]
    indices = bytearray()
    row_offsets = []
    for y in range(TILE_SIZE):
        row_offsets.append(source_offset(page_x + u // per_word, (page_y + v + y) & 511))
        for x in range(TILE_SIZE):
            tx = (u + x) & 255
            offset = source_offset((page_x + tx // per_word) & 1023, (page_y + v + y) & 511)
            word = struct.unpack_from("<H", pictures, offset)[0]
            indices.append((word >> ((tx % per_word) * bits)) & ((1 << bits) - 1))
    tile = Image.new("RGBA", (TILE_SIZE, TILE_SIZE))
    tile.putdata([palette[index] for index in indices])
    metadata = {
        "descriptor_hex": record.hex(), "bpp": bits, "uv": [u, v],
        "texture_page": page, "clut": clut,
        "vram_origin_words": [page_x + u // per_word, page_y + v],
        "pixel_row_offsets_in_level": row_offsets,
        "first_pixel_bit_shift": (u % per_word) * bits,
        "original_indices_hex": indices.hex(),
        "original_rgba_sha256": hashlib.sha256(tile.tobytes()).hexdigest(),
    }
    palette_metadata = {
        "vram_origin_words": [palette_x, palette_y],
        "offset_in_level": palette_offsets[0], "words": colours,
    }
    return tile, metadata, palette_metadata


def checkerboard(width: int, height: int) -> Image.Image:
    image = Image.new("RGBA", (width, height), "#30343c")
    draw = ImageDraw.Draw(image)
    for y in range(0, height, 8):
        for x in range(0, width, 8):
            if (x // 8 + y // 8) % 2:
                draw.rectangle((x, y, x + 7, y + 7), fill="#424650")
    return image


def export_level(disc: Disc, wad_lba: int, wad_offset: int, slot: int, destination: Path) -> dict:
    pictures = disc.read(wad_lba + wad_offset // SECTOR, PICTURES_END)
    count = u32(pictures, 70 * 4)
    second_size = u32(pictures, 4)
    if not 0 < count <= 4096 or second_size % SECTOR or count * 8 > second_size:
        raise ValueError(f"Invalid texture table for level slot {slot}")
    table = disc.read(wad_lba + (wad_offset + PICTURES_END) // SECTOR, count * 8)
    name = f"level-slot-{slot:02d}"
    folder = destination / name
    (folder / "tiles").mkdir(parents=True)
    rows = (count + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS
    atlas = Image.new("RGBA", (ATLAS_COLUMNS * TILE_SIZE, rows * TILE_SIZE))
    contact = Image.new("RGB", (ATLAS_COLUMNS * 76, rows * 88), "#252932")
    draw = ImageDraw.Draw(contact)
    manifest = {
        "format_version": 1, "level_slot": slot, "wad_table_index": 162 + slot,
        "wad_piece_offset": wad_offset, "tile_size": [32, 32],
        "atlas_columns": ATLAS_COLUMNS, "descriptor_count": count,
        "descriptor_table_offset_in_level": PICTURES_END,
        "uploads": [list(upload) for upload in UPLOADS],
        "palettes": {}, "tiles": [], "skipped": [],
    }
    for index in range(count):
        record = table[index * 8:(index + 1) * 8]
        # Slot 1 has one null descriptor pointing into the framebuffer, with no palette.
        if record == bytes.fromhex("000000001f000000"):
            manifest["skipped"].append({"id": index, "reason": "null descriptor", "descriptor_hex": record.hex()})
            continue
        try:
            tile, metadata, palette = decode_tile(pictures, record)
        except MissingVRAM as error:
            manifest["skipped"].append({"id": index, "reason": str(error), "descriptor_hex": record.hex()})
            continue
        filename = f"tiles/tile_{index:04d}.png"
        tile.save(folder / filename)
        metadata.update({"id": index, "file": filename,
                         "atlas_xy": [(index % ATLAS_COLUMNS) * TILE_SIZE, (index // ATLAS_COLUMNS) * TILE_SIZE],
                         "descriptor_offset_in_level": PICTURES_END + index * 8})
        manifest["tiles"].append(metadata)
        manifest["palettes"][f"{metadata['clut']:04x}-{metadata['bpp']}"] = palette
        atlas.paste(tile, tuple(metadata["atlas_xy"]))
        x, y = (index % ATLAS_COLUMNS) * 76 + 6, (index // ATLAS_COLUMNS) * 88 + 4
        preview = checkerboard(64, 64)
        preview.alpha_composite(tile.resize((64, 64), Image.Resampling.NEAREST))
        contact.paste(preview.convert("RGB"), (x, y))
        draw.text((x, y + 66), f"{index:04d}", fill="white")
    atlas.save(folder / "atlas.png")
    contact.save(folder / "contact-sheet.png")
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    result = {"folder": name, "level_slot": slot, "tiles": len(manifest["tiles"]),
              "skipped": manifest["skipped"], "palettes": len(manifest["palettes"])}
    print(f"{name}: {result['tiles']} tiles, {result['palettes']} palettes, {len(result['skipped'])} skipped descriptors", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, default=Path("input/Disruptor (USA).bin"), help="Supported original USA MODE2/2352 BIN")
    parser.add_argument("--output", type=Path, default=Path("output/environment-textures"), help="New or empty destination; existing edits are never overwritten")
    parser.add_argument("--slots", type=int, nargs="+", choices=range(1, 16), help="Internal game level slots (default: all populated slots)")
    args = parser.parse_args()
    disc = None
    try:
        if args.output.exists() and (not args.output.is_dir() or any(args.output.iterdir())):
            raise ValueError(f"Destination must be empty; choose a new --output folder: {args.output}")
        if args.disc.stat().st_size != DISC_BYTES:
            raise ValueError("Unsupported disc size; this tool expects the original USA BIN")
        with args.disc.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        if digest != DISC_SHA256:
            raise ValueError(f"Unsupported disc SHA-256: {digest}")
        disc = Disc(args.disc)
        wad_lba, wad_size = disc.wad()
        offsets = struct.unpack("<405I", disc.read(wad_lba, 405 * 4))
        selected = sorted(set(args.slots or range(1, 16)))
        levels = []
        empty = []
        for slot in selected:
            offset = offsets[162 + slot]
            if offset == 0:
                empty.append(slot)
                continue
            if offset % SECTOR or offset < 7680 * 4 or offset + PICTURES_END > wad_size:
                raise ValueError(f"Invalid level offset in slot {slot}")
            levels.append(export_level(disc, wad_lba, offset, slot, args.output))
        summary = {"source_disc": str(args.disc.resolve()), "source_sha256": digest,
                   "wad_lba": wad_lba, "tile_size": [32, 32],
                   "total_tiles": sum(level["tiles"] for level in levels),
                   "empty_level_slots": empty, "levels": levels}
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "manifest.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        readme = ["Environment textures extracted from your Disruptor USA disc", "",
                  f"{summary['total_tiles']} individual 32x32 PNGs across {len(levels)} populated level slots.", "",
                  "Open a level folder's contact-sheet.png to browse the numbered tiles.",
                  "Edit tiles/tile_NNNN.png, or atlas.png (16 columns, no gaps, original resolution).",
                  "The contact sheets are labelled 2x previews; edit the tiles or atlas instead.", "",
                  "Level-slot numbers preserve the game's internal slots; they are not mission names.",
                  "Empty internal slots: " + ", ".join(map(str, empty)), "",
                  "manifest.json preserves source positions, pixel indices and original PSX palettes.",
                  "Some tiles share pixels or palettes. Keep manifests with your edits.",
                  "Skipped descriptors reference VRAM outside the level uploads, or are null.",
                  "Their IDs and reasons are recorded in each manifest; their atlas cells are empty.",
                  "PNG alpha represents PSX transparent zero. PSX blending bit 15 is preserved",
                  "in each palette's raw words, and cannot be represented by ordinary PNG alpha.", "",
                  "These exports are original artwork. In-game loading of edited PNGs is not implemented.",
                  "Extraction reads the disc without changing it. Do not redistribute the original assets.", ""]
        (args.output / "README.txt").write_text("\n".join(readme), encoding="utf-8")
        links = "\n".join(
            f'<section><h2>Level slot {level["level_slot"]:02d} &middot; {level["tiles"]} tiles</h2>'
            f'<p><a href="{level["folder"]}/atlas.png">Original-size atlas</a></p>'
            f'<a href="{level["folder"]}/contact-sheet.png"><img loading="lazy" '
            f'src="{level["folder"]}/contact-sheet.png" alt="Level slot {level["level_slot"]:02d} tiles"></a></section>'
            for level in levels)
        html = ('<!doctype html><html lang="en"><meta charset="utf-8"><title>Disruptor environment tiles</title>'
                '<style>body{background:#191c22;color:#e8edf4;font:16px system-ui;margin:32px auto;max-width:1240px;padding:0 20px}'
                'a{color:#8cc7ff}img{max-width:100%;image-rendering:pixelated}section{margin:48px 0}</style>'
                f'<h1>Disruptor environment tiles</h1><p>{summary["total_tiles"]} editable 32&times;32 PNGs from '
                f'{len(levels)} level slots. Previews show tile IDs; files live in each level folder\'s tiles directory.</p>'
                '<p>Level numbers are internal slots. See <a href="README.txt">README.txt</a> for editing notes.</p>' + links + '</html>')
        (args.output / "index.html").write_text(html, encoding="utf-8")
        print(f"Exported {summary['total_tiles']} tiles to {args.output.resolve()}")
        return 0
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        if disc:
            disc.file.close()


if __name__ == "__main__":
    raise SystemExit(main())
