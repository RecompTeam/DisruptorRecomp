# Sky extensions

Each `.idx` file contains 1280 columns of row-major, 8-bit indices into its panorama's original palette. The panorama hash, row count and overlap band are registered in `sky_art.cpp`.

The bottom `band` rows overlap the top of the original panorama. The renderer blends this overlap in as the player looks up. All rows below the overlap remain the original panorama. Horizontal edges wrap around a full turn.

CMake embeds the strips in the executable. Unknown panoramas and views beyond a strip's height use the existing procedural extension.
