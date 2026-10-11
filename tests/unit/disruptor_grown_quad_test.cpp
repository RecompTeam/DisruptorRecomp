#include <array>
#include <cstdint>
#include <iostream>
#include <map>

namespace {

std::map<std::uint32_t, std::uint32_t> g_memory;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" std::uint32_t psx_mod_read_word(std::uint32_t address) {
    const auto found = g_memory.find(address);
    return found == g_memory.end() ? 0xDEADBEEFu : found->second;
}

#include "../../src/disruptor_grown_quad.cpp"

namespace {

using Positions = std::array<std::uint32_t, 4>;

struct Pair {
    Positions first, second;
};

/* Position words of a first packet and of the game's own second packet, read from a running game. */
constexpr std::array<Pair, 3> kFromTheGame{{
    {{0x005F00DDu, 0x006000D5u, 0x004B00D5u, 0x004E00CDu}, {0x004D00CCu, 0x004900D5u, 0x006100D4u, 0x006000DEu}},
    {{0x009400E7u, 0x009800F7u, 0x009500E3u, 0x009900F2u}, {0x009900F2u, 0x009400E1u, 0x009800F8u, 0x009300E6u}},
    {{0x0055005Cu, 0x00560065u, 0x005A005Fu, 0x005A0062u}, {0x005A0062u, 0x005A005Fu, 0x00550065u, 0x0054005Bu}},
}};
constexpr std::uint32_t kFirst = 0x00F01000u;
constexpr std::uint32_t kSecond = kFirst + 0x34u;
constexpr std::uint32_t kCommand = 0x3C808080u;

std::uint32_t word(int x, int y) {
    return (static_cast<std::uint32_t>(x) & 0xFFFFu) + (static_cast<std::uint32_t>(y) << 16);
}

/* A first packet in guest memory and the twelve words of the packet that follows it. */
std::array<std::uint32_t, 12> packets(const Positions &first, const Positions &second,
                                      std::uint32_t first_command = kCommand) {
    g_memory.clear();
    g_memory[0x80000000u | kFirst] = first_command;
    std::array<std::uint32_t, 12> packet{};
    packet[0] = kCommand;
    for (int corner = 0; corner < 4; ++corner) {
        const std::uint32_t index = 1u + 3u * static_cast<std::uint32_t>(corner);
        g_memory[0x80000000u | (kFirst + index * 4u)] = first[corner];
        packet[index] = second[corner];
    }
    return packet;
}

void positions_follow_the_game() {
    for (const Pair &pair : kFromTheGame) {
        Positions grown{};
        disruptor_grown_quad_positions(pair.first.data(), grown.data());
        expect(grown == pair.second, "the arithmetic reproduces a second packet the game itself wrote");
    }
    /* Corners left of and above the screen: -20,-5  40,-7  -22,30  44,33. */
    const Positions outside{word(-20, -5), word(40, -7), word(-22, 30), word(44, 33)};
    Positions grown{};
    disruptor_grown_quad_positions(outside.data(), grown.data());
    expect(grown == Positions{word(48, 35), word(-27, 32), word(43, -10), word(-24, -8)},
           "negative coordinates round down like the game's arithmetic shift");
}

void sources_name_the_first_packet() {
    const Pair &pair = kFromTheGame[0];
    auto packet = packets(pair.first, pair.second);
    Positions address{}, source{};
    expect(disruptor_grown_quad_sources(kSecond, packet.data(), address.data(), source.data()) == 1,
           "a packet that is the grown copy of the one before it is recognised");
    expect(address == Positions{kFirst + 40u, kFirst + 28u, kFirst + 16u, kFirst + 4u} &&
               source == Positions{pair.first[3], pair.first[2], pair.first[1], pair.first[0]},
           "every corner names the corner of the first packet it came from, in reverse order");

    for (int corner = 0; corner < 4; ++corner) {
        auto moved = packet;
        moved[1u + 3u * static_cast<std::uint32_t>(corner)] += 1u;
        expect(disruptor_grown_quad_sources(kSecond, moved.data(), address.data(), source.data()) == 0,
               "one corner a pixel off is another polygon");
    }
    auto textured = packet;
    textured[0] = 0x2C808080u;
    expect(disruptor_grown_quad_sources(kSecond, textured.data(), address.data(), source.data()) == 0,
           "only shaded textured quads are drawn twice");
    auto translucent = packet;
    translucent[0] = 0x3E808080u;
    expect(disruptor_grown_quad_sources(kSecond, translucent.data(), address.data(), source.data()) == 0,
           "the two packets carry one command");
    packet = packets(pair.first, pair.second, 0x3E808080u);
    packet[0] = 0x3E808080u;
    expect(disruptor_grown_quad_sources(kSecond, packet.data(), address.data(), source.data()) == 1,
           "a translucent pair is recognised too");
    packet = packets(pair.first, pair.second);
    expect(disruptor_grown_quad_sources(kSecond + 4u, packet.data(), address.data(), source.data()) == 0,
           "the first packet lies exactly one packet before the second");
    packet = packets(pair.first, pair.second, 0x2C808080u);
    packet[0] = 0x2C808080u;
    expect(disruptor_grown_quad_sources(kSecond, packet.data(), address.data(), source.data()) == 0,
           "nor is a pair of plain textured quads with the same positions");
    /* A first packet that would lie below address zero, with matching words where the subtraction wraps to. */
    packet = packets(pair.first, pair.second);
    g_memory[0xFFFFFFFCu] = kCommand;
    for (int corner = 0; corner < 4; ++corner)
        g_memory[0x80000000u | (0xFFFFFFFCu + (1u + 3u * static_cast<std::uint32_t>(corner)) * 4u)] = pair.first[corner];
    expect(disruptor_grown_quad_sources(0x30u, packet.data(), address.data(), source.data()) == 0 &&
               disruptor_grown_quad_sources(kSecond, nullptr, address.data(), source.data()) == 0 &&
               disruptor_grown_quad_sources(kSecond, packet.data(), nullptr, source.data()) == 0 &&
               disruptor_grown_quad_sources(kSecond, packet.data(), address.data(), nullptr) == 0,
           "an address without room for a first packet and missing arguments are refused");
}

}  // namespace

int main() {
    positions_follow_the_game();
    sources_name_the_first_packet();
    if (g_failures) return 1;
    std::cout << "Disruptor grown quad tests passed\n";
    return 0;
}
