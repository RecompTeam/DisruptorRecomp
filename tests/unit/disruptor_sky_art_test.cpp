#include "game/render/sky_art.h"
#include "game_host.h"

#include <array>
#include <iostream>

int main() {
    disruptor::sky_panorama::Picture picture;
    picture.width = 1280;
    picture.height = 240;
    constexpr std::array<uint64_t, 8> skies = {
        0xC9D53ACFE66EB10Bull, 0xA6A30DE71D65100Dull, 0xD45C4422C609AD02ull, 0xCF2A5D8F606B29ACull,
        0x30776B002F9D32F5ull, 0x705B8BFA188C1BE0ull, 0x4CAEF66DF7CE8E2Dull, 0x5229C04BF1EB164Dull,
    };
    for (const uint64_t hash : skies) {
        picture.hash = hash;
        const auto art = disruptor::sky_art::find(picture, 140);
        if (!art || art->width != 1280 || art->rows > 512 || art->band < 0 || art->band >= art->rows ||
            art->indices.size() != 1280u * static_cast<size_t>(art->rows) ||
            !disruptor::sky_art::find(picture, art->rows - art->band) ||
            disruptor::sky_art::find(picture, art->rows - art->band + 1)) {
            std::cerr << "A prepared sky must cover the full look-up range and contain every palette index it uploads.\n";
            return 1;
        }
        size_t bytes = 0;
        const auto *overlay = psx_game_sky_overlay(art->width, art->rows, art->band, art->indices.data(), &bytes);
        if ((overlay != nullptr) != (hash == 0x4CAEF66DF7CE8E2Dull) || (bytes != 0) != (overlay != nullptr)) return 1;
        if (overlay) {
            auto altered = art->indices;
            altered.back() ^= 1;
            if (psx_game_sky_overlay(art->width, art->rows, art->band, altered.data(), &bytes) || bytes) return 1;
        }
        if (hash != 0xC9D53ACFE66EB10Bull) {
            for (int32_t row = 0; row < art->rows - art->band; ++row) {
                const size_t first = static_cast<size_t>(row) * 1280u;
                if (art->indices[first] != art->indices[first + 1279u]) {
                    std::cerr << "A cloud panorama must join at the horizontal wrap.\n";
                    return 1;
                }
            }
        }
    }
    if (disruptor::sky_art::find(picture, -1)) return 1;
    picture.width = 640;
    if (disruptor::sky_art::find(picture, 128)) return 1;
    picture.width = 1280;
    picture.hash = 0;
    if (disruptor::sky_art::find(picture, 128)) return 1;
    return 0;
}
