#include "game/render/quad_visibility.h"

#include <array>
#include <cstdint>
#include <iostream>

uint32_t at(int32_t x, int32_t y) {
    return static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16 | static_cast<uint16_t>(x);
}

int main() {
    struct Case {
        const char *name;
        std::array<uint32_t, 4> screen;
        uint32_t flags;
        uint32_t expected;
    };
    const Case cases[] = {
        {"left Triton rock", {at(91, 140), at(92, 173), at(101, 227), at(78, 187)}, 0x202u, 0x2u},
        {"right Triton rock", {at(205, 165), at(202, 202), at(179, 236), at(205, 182)}, 0x2u, 0x202u},
        {"alternate culling triangle", {at(223, 160), at(235, 197), at(218, 235), at(223, 177)}, 0x300u, 0x100u},
        {"both culling triangles", {at(223, 160), at(235, 197), at(218, 235), at(223, 177)}, 0x600u, 0x600u},
        {"both tests miss a visible half", {at(0, 0), at(10, 0), at(0, 10), at(-2, -2)}, 0x400u, 0x600u},
        {"two-sided face", {at(91, 140), at(92, 173), at(101, 227), at(78, 187)}, 0xA00u, 0xA00u},
        {"opposite winding", {at(-91, 140), at(-92, 173), at(-101, 227), at(-78, 187)}, 0x2u, 0x202u},
        {"front-facing rectangle", {at(10, 10), at(30, 10), at(30, 30), at(10, 30)}, 0x202u, 0x202u},
        {"back-facing rectangle", {at(10, 10), at(30, 10), at(30, 30), at(10, 30)}, 0x2u, 0x2u},
        {"collapsed face", {at(10, 10), at(20, 10), at(30, 10), at(10, 10)}, 0x202u, 0x202u},
        {"triangle", {at(10, 10), at(30, 10), at(30, 30), at(10, 10)}, 0x2u, 0x2u},
    };
    int failures = 0;
    for (const Case &example : cases) {
        if (disruptor::quad_visibility::culling_flags(example.screen, example.flags) == example.expected) continue;
        std::cerr << "FAIL: " << example.name << '\n';
        ++failures;
    }
    if (failures) return 1;
    std::cout << "quad_visibility: PASS\n";
    return 0;
}
