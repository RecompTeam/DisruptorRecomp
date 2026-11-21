#include "game/render/sky_art.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>

extern "C" const unsigned char disruptor_sky_earth[];
extern "C" const std::size_t disruptor_sky_earth_size;
extern "C" const unsigned char disruptor_sky_city[];
extern "C" const std::size_t disruptor_sky_city_size;
extern "C" const unsigned char disruptor_sky_rooftop[];
extern "C" const std::size_t disruptor_sky_rooftop_size;
extern "C" const unsigned char disruptor_sky_mars[];
extern "C" const std::size_t disruptor_sky_mars_size;
extern "C" const unsigned char disruptor_sky_triton[];
extern "C" const std::size_t disruptor_sky_triton_size;
extern "C" const unsigned char disruptor_sky_dusk[];
extern "C" const std::size_t disruptor_sky_dusk_size;
extern "C" const unsigned char disruptor_sky_green[];
extern "C" const std::size_t disruptor_sky_green_size;
extern "C" const unsigned char disruptor_sky_green_upper[];
extern "C" const std::size_t disruptor_sky_green_upper_size;
extern "C" const unsigned char disruptor_sky_fire[];
extern "C" const std::size_t disruptor_sky_fire_size;

namespace {

struct Asset {
    uint64_t hash;
    int32_t rows;
    int32_t band;
    std::span<const unsigned char> indices;
};

const Asset kGreen{0x4CAEF66DF7CE8E2Dull, 396, 12, {disruptor_sky_green, disruptor_sky_green_size}};
const std::array<Asset, 8> kAssets = {{
    {0xC9D53ACFE66EB10Bull, 448, 64, {disruptor_sky_earth, disruptor_sky_earth_size}},
    {0xA6A30DE71D65100Dull, 416, 32, {disruptor_sky_city, disruptor_sky_city_size}},
    {0xD45C4422C609AD02ull, 408, 30, {disruptor_sky_rooftop, disruptor_sky_rooftop_size}},
    {0xCF2A5D8F606B29ACull, 384, 0, {disruptor_sky_mars, disruptor_sky_mars_size}},
    {0x30776B002F9D32F5ull, 408, 24, {disruptor_sky_triton, disruptor_sky_triton_size}},
    {0x705B8BFA188C1BE0ull, 192, 32, {disruptor_sky_dusk, disruptor_sky_dusk_size}},
    kGreen,
    {0x5229C04BF1EB164Dull, 400, 16, {disruptor_sky_fire, disruptor_sky_fire_size}},
}};

}

extern "C" const unsigned char *psx_game_sky_overlay(int width, int rows, int band, const unsigned char *indices, size_t *bytes) {
    *bytes = 0;
    if (width != 1280 || rows != kGreen.rows || band != kGreen.band || !indices ||
        std::memcmp(indices, kGreen.indices.data(), kGreen.indices.size()) != 0) return nullptr;
    *bytes = disruptor_sky_green_upper_size;
    return disruptor_sky_green_upper;
}

std::optional<disruptor::sky_extension::Extension> disruptor::sky_art::find(
    const sky_panorama::Picture &picture, int32_t minimum_rows) {
    constexpr int32_t width = 1280;
    if (picture.width != width || minimum_rows < 0) return std::nullopt;
    for (const Asset &asset : kAssets) {
        if (picture.hash != asset.hash) continue;
        if (minimum_rows > asset.rows - asset.band) return std::nullopt;
        sky_extension::Extension result;
        result.width = width;
        result.rows = asset.rows;
        result.band = asset.band;
        result.indices.assign(asset.indices.begin(), asset.indices.end());
        return result;
    }
    return std::nullopt;
}
