# Exporting environment textures

Run from the repository root with Python 3.11 or newer and Pillow installed:

```powershell
python tools/extract_environment_textures.py
```

The tool reads your original, verified USA BIN at `input/Disruptor (USA).bin`.
It exports to `output/environment-textures/`, which is excluded from Git.
Both the source and the game's behavior stay unchanged.

Open `output/environment-textures/index.html` to browse the exports, or open a
level folder's `contact-sheet.png`. Edit individual `tiles/tile_NNNN.png` files
in an image editor, or edit `atlas.png`, a gapless grid at the original
resolution. Each tile is 32 by 32 pixels, with 16 columns per atlas. Contact
sheets are labelled previews enlarged to twice the original size.

Folder numbers preserve the game's internal level slots, including gaps; they
are not mission names. Tile filenames preserve descriptor IDs, including the
gaps for null descriptors and references to VRAM outside the level's stored
uploads. Those entries are listed with their reasons in each manifest and
have empty atlas cells. Animated and palette variants remain separate.

To select slots or export another working copy:

```powershell
python tools/extract_environment_textures.py --slots 1 2 --output output/my-texture-copy
```

Choose an empty destination: the tool refuses to overwrite an existing export
so that rerunning it cannot erase artwork you have edited. Keep custom export
folders outside version control too.

Each level's `manifest.json` records its WAD table index, descriptor IDs,
atlas coordinates, original pixel indices, raw PSX palette words, and source
offsets within the level piece. Some descriptors share pixels or palettes.
PNG transparency represents PSX colour word zero; the PSX blending flag in
bit 15 is retained in the manifest because its meaning also depends on the
draw command. These PNGs contain original colour values, without world
lighting or distance fade applied.

This exports the environment tile table. Characters, weapons, interface art,
and separate backdrop assets have other tables. Loading edited PNGs in the
game or repacking them into the disc is not implemented by this tool.

The layout follows the retail loader at `0x80015398`: upload the level's
bytes at `0x800` to VRAM `(0,480,320,32)` and bytes at `0x5800` to
`(320,192,704,320)`, with widths measured in 16-bit words. Header word 70
counts the eight-byte world texture descriptors starting at level offset
`0x73800`. Descriptors identify the texture page, palette, and 32-pixel UV
span. Both four-bit and eight-bit indexed tiles are decoded.

Original game assets belong with your local disc data. Do not commit or
redistribute the exports.
