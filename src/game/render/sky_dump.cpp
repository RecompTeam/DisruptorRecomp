/*
 * Developer builds only: PSX_SKY_DUMP=<folder> writes each sky panorama the game sets up, as the sky pass reads it, so that
 * its extension can be looked at and made from it. Per panorama, named by the hash of its texel indices: <hash>.ppm, the
 * picture through the base palette, and <hash>.dump, the indices, the base palette and the cell table.
 */

#include "game/render/sky_grower.h"

#include "gpu.h"
#include "mod_plugins.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace sky = disruptor::sky_panorama;

uint64_t g_last = 0;

}  // namespace

extern "C" void mod_register_frame_hook(void (*hook)(void));

namespace {

void put32(std::ofstream &out, uint32_t value) {
    for (int at = 0; at < 4; ++at) out.put(static_cast<char>(value >> (8 * at)));
}

}  // namespace

void disruptor::sky_grower::dump(const sky::Cells &cells, uint32_t clut_x, uint32_t clut_y) {
    static const char *folder_name = std::getenv("PSX_SKY_DUMP");
    if (!folder_name || !*folder_name || cells.rows == 0 || clut_x > 1024u - 256u || clut_y > 511u) return;
    const uint16_t *vram = gpu_get_vram();
    if (!vram) return;
    const sky::Picture panorama = sky::picture(cells, vram);
    if (panorama.hash == g_last) return;
    g_last = panorama.hash;
    std::array<uint16_t, 256> palette{};
    for (size_t index = 0; index < palette.size(); ++index) palette[index] = vram[clut_y * 1024u + clut_x + index];

    char name[32];
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(panorama.hash));
    const std::filesystem::path folder(folder_name);
    std::error_code failed;
    std::filesystem::create_directories(folder, failed);
    std::ofstream picture(folder / (std::string(name) + ".ppm"), std::ios::binary);
    picture << "P6\n" << panorama.width << ' ' << panorama.height << "\n255\n";
    for (const uint8_t index : panorama.indices) {
        const uint16_t colour = palette[index];
        for (const int shift : {0, 5, 10}) {
            const int five = (colour >> shift) & 31;
            picture.put(static_cast<char>((five << 3) | (five >> 2)));
        }
    }
    std::ofstream raw(folder / (std::string(name) + ".dump"), std::ios::binary);
    raw.write("DSKYDMP1", 8);
    put32(raw, static_cast<uint32_t>(panorama.width));
    put32(raw, static_cast<uint32_t>(panorama.height));
    put32(raw, static_cast<uint32_t>(cells.rows));
    put32(raw, static_cast<uint32_t>(cells.columns));
    put32(raw, cells.irregular ? 1u : 0u);
    for (int cell = 0; cell < cells.rows * cells.columns; ++cell) {
        put32(raw, static_cast<uint32_t>(cells.xy[static_cast<size_t>(2 * cell)]));
        put32(raw, static_cast<uint32_t>(cells.xy[static_cast<size_t>(2 * cell + 1)]));
    }
    for (const uint16_t colour : palette) {
        raw.put(static_cast<char>(colour & 0xFFu));
        raw.put(static_cast<char>(colour >> 8));
    }
    raw.write(reinterpret_cast<const char *>(panorama.indices.data()), static_cast<std::streamsize>(panorama.indices.size()));
    std::fprintf(stderr, "disruptor: sky dump %s %dx%d%s\n", name, panorama.width, panorama.height, cells.irregular ? " irregular" : "");
}

namespace {

/* A level's table is set when it loads: indoors, where no sky is drawn, it is read once a VBlank as well. */
void dump_each_frame() {
    const uint32_t descriptor = psx_mod_read_word(sky::kDescriptorPointer);
    const sky::Cells cells = sky::read([](uint32_t address) { return psx_mod_read_word(address); }, descriptor);
    if (cells.rows > 0) {
        disruptor::sky_grower::dump(cells, psx_mod_read_word(descriptor + 1280u), psx_mod_read_word(descriptor + 1284u));
        return;
    }
    static uint32_t refused = 0;
    if (!sky::in_ram(descriptor) || descriptor == refused) return;
    refused = descriptor;
    std::fprintf(stderr, "disruptor: sky table at %08X refused: rows %u, columns %u, row tops %u %u, column starts %u %u\n", descriptor,
                 psx_mod_read_word(descriptor), psx_mod_read_word(descriptor + 4u), psx_mod_read_word(descriptor + 8u),
                 psx_mod_read_word(descriptor + 12u), psx_mod_read_word(descriptor + 32u), psx_mod_read_word(descriptor + 36u));
    const uint32_t rows = std::min(psx_mod_read_word(descriptor), 4u), columns = std::min(psx_mod_read_word(descriptor + 4u), 24u);
    for (uint32_t row = 0; row < rows; ++row) {
        std::fprintf(stderr, "disruptor: sky table row %u:", row);
        for (uint32_t column = 0; column < columns; ++column)
            std::fprintf(stderr, " %u,%u", psx_mod_read_word(descriptor + 128u + 96u * row + 4u * column),
                         psx_mod_read_word(descriptor + 704u + 96u * row + 4u * column));
        std::fprintf(stderr, "\n");
    }
}

struct Registration {
    Registration() {
        const char *folder = std::getenv("PSX_SKY_DUMP");
        if (folder && *folder) mod_register_frame_hook(dump_each_frame);
    }
};

Registration g_registration;

}  // namespace
