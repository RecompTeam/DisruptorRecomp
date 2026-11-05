#include "game/session/card_choice.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

using namespace disruptor::card_choice;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

std::array<uint8_t, kFrameBytes> frame(uint8_t state, std::string_view name) {
    std::array<uint8_t, kFrameBytes> bytes{};
    bytes[0] = state;
    std::memcpy(bytes.data() + 10, name.data(), name.size());
    return bytes;
}

}  // namespace

int main() {
    expect(opens_game_file(frame(0x51u, kFile).data()), "the first block of the game's file opens it");
    expect(!opens_game_file(frame(0xA1u, kFile).data()), "a deleted file does not");
    expect(!opens_game_file(frame(0x52u, kFile).data()), "a middle block does not");
    expect(!opens_game_file(frame(0x51u, "BASLUS-00224DISRPTO").data()), "a shorter name is another file");
    expect(!opens_game_file(frame(0x51u, "BASLUS-00224DISRPTOR1").data()), "a longer name is another file");
    expect(!opens_game_file(frame(0x51u, "BESLES-00224DISRPTOR").data()), "another game's file does not");

    expect(chosen(0, false, true, true) == 1, "the right card alone holding the saves is taken");
    expect(chosen(0, true, true, true) == 0, "with saves on both the left one stays, as the game starts");
    expect(chosen(0, true, false, true) == 0, "the left card with the saves stays");
    expect(chosen(0, false, false, true) == 0, "no saves anywhere leaves the game's left card");
    expect(chosen(0, false, true, false) == 0, "a right card the game does not find is not taken");
    expect(chosen(-1, false, true, true) == -1 && chosen(1, true, false, true) == 1, "only the start's left card is changed");
    if (g_failures) return 1;
    std::cout << "card choice: ok\n";
    return 0;
}
