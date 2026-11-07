#include "game/session/weapon_load.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

std::array<uint8_t, 2u * 1024u * 1024u> ram{};

void put_byte(uint32_t address, uint8_t value) { ram[address & 0x1FFFFFu] = value; }

void put_word(uint32_t address, uint32_t value) {
    for (uint32_t i = 0; i < 4u; ++i) put_byte(address + i, static_cast<uint8_t>(value >> (8u * i)));
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

/* Words read from the user's mission 6 capsules 1794019548 (start, unarmed) and 1794019605 (pistol picked up). */
void mission_six_start() {
    ram.fill(0u);
    put_word(0x80071714u, 0u);
    put_byte(0x80071324u, 0u);
    put_byte(0x80077627u, 0xFFu);
    put_byte(0x80071484u, 0xFFu);
}

void pistol_held() {
    mission_six_start();
    put_word(0x80071714u, 0x8006014Cu);
    put_byte(0x80077627u, 0u);
    put_byte(0x80071484u, 0u);
    put_word(0x800770FCu, 1u);
}

}  // namespace

uint8_t psx_mod_read_byte(uint32_t address) { return ram[address & 0x1FFFFFu]; }
uint32_t psx_mod_read_word(uint32_t address) {
    const uint32_t at = address & 0x1FFFFFu;
    return ram[at] | ram[at + 1u] << 8 | ram[at + 2u] << 16 | static_cast<uint32_t>(ram[at + 3u]) << 24;
}

int main() {
    mission_six_start();
    require(disruptor::unarmed(), "mission 6's start: no weapon owned, none held or wanted");
    require(!disruptor::weapon_switching(false), "and no switch either, so only the unarmed rule admits it");

    pistol_held();
    require(!disruptor::unarmed(), "a held pistol is not unarmed");

    for (uint32_t weapon = 0; weapon < 8u; ++weapon) {
        mission_six_start();
        put_word(0x800770FCu + weapon * 4u, 1u);
        require(!disruptor::unarmed(), "a weapon owned at any of the eight words is not unarmed");
    }

    mission_six_start();
    put_byte(0x80071324u, 1u);
    require(!disruptor::unarmed() && disruptor::weapon_switching(true), "a weapon's pictures loading is a switch");

    mission_six_start();
    put_byte(0x80077627u, 0u);
    require(!disruptor::unarmed(), "a weapon wanted is no longer unarmed");

    mission_six_start();
    put_byte(0x80071484u, 0u);
    require(!disruptor::unarmed(), "a weapon held is no longer unarmed");

    mission_six_start();
    put_word(0x80071714u, 0x8006014Cu);
    require(!disruptor::unarmed(), "a player record present is judged by its hands, not by this rule");

    std::puts("weapon_load: ok");
    return 0;
}
