# Sky extensions

Each `.idx` file contains 1280 columns of row-major, 8-bit indices into its panorama's original palette. The panorama hash, row count and overlap band are registered in `sky_art.cpp`.

The bottom `band` rows overlap the top of the original panorama. The renderer blends this overlap in as the player looks up. All rows below the overlap remain the original panorama. Horizontal edges wrap around a full turn.

CMake embeds the strips in the executable. Unknown panoramas and views beyond a strip's height use the existing procedural extension.

Mission 13 has a 1920 × 594 upper extension in `sky-green-upper.webp`. Its last 18 rows overlap 12 rows of the disc panorama. The original clouds and mountains are filtered at the game's original scale.

`sky-chemical-factory.webp` and `sky-triton-night.webp` hold the lossless sky textures for Chemical Factory and Triton's upper deck. The game embeds them alongside `sky-rooftops-clouds.webp`.
