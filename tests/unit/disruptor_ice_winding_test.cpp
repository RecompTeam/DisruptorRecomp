#include "game/render/ice_winding.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

int check_repair(const disruptor::ice_winding::MeshRepair &repair) {
    using namespace disruptor::ice_winding;
    constexpr uint32_t part = 0x80100000u;
    std::vector<uint32_t> words(repair.part_size / 4u);
    words[0] = repair.part_size << 8 | 0xFFu;
    words[1] = repair.lights;
    for (const VertexWord &vertex : repair.vertices) words[vertex.offset / 4u] = vertex.value;
    const auto read = [&](uint32_t address) { return words.at((address - part) / 4u); };
    for (const Face &face : repair.faces) {
        const uint32_t corrected = face[1] ^ 0x200u;
        if (culling_flags(face[0], face[1], part, read) != corrected ||
            culling_flags(face[0], corrected, part, read) != corrected) {
            std::cerr << "FAIL: shipped face winding or repeated correction\n";
            return 1;
        }
    }
    const Face &face = repair.faces[0];
    for (uint32_t index : {0u, 1u}) {
        words[index] ^= 1u;
        if (culling_flags(face[0], face[1], part, read) != face[1]) {
            std::cerr << "FAIL: incompatible mesh layout\n";
            return 1;
        }
        words[index] ^= 1u;
    }
    for (const VertexWord &vertex : repair.vertices) {
        const uint32_t index = vertex.offset / 4u;
        words[index] ^= 1u;
        if (culling_flags(face[0], face[1], part, read) != face[1]) {
            std::cerr << "FAIL: a different mesh was changed\n";
            return 1;
        }
        words[index] ^= 1u;
    }
    if (culling_flags(face[0] ^ 1u, face[1], part, read) != face[1] ||
        culling_flags(face[0], face[1], 0u, read) != face[1] ||
        culling_flags(face[0], face[1], part + 1u, read) != face[1] ||
        culling_flags(face[0], face[1], 0x801FFFFCu, read) != face[1]) {
        std::cerr << "FAIL: unrelated face or invalid part\n";
        return 1;
    }
    return 0;
}

int main() {
    for (const auto &repair : disruptor::ice_winding::kRepairs)
        if (check_repair(repair)) return 1;
    std::cout << "ice_winding: PASS\n";
    return 0;
}
