#include "game/render/sector_reach.h"

#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace {

using namespace disruptor::sector_reach;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Memory {
    std::unordered_map<uint32_t, uint32_t> words, halves, bytes;
    uint32_t word(uint32_t address) const { return words.contains(address) ? words.at(address) : 0u; }
    uint32_t half(uint32_t address) const { return halves.contains(address) ? halves.at(address) : 0u; }
    uint32_t byte(uint32_t address) const { return bytes.contains(address) ? bytes.at(address) : 0u; }
};

constexpr Portal kAcross{0, 0, 256, 0};
constexpr Portal kAlong{100, 0, 100, 512};
constexpr uint32_t kStack = 0x801FFE00u, kGate = 0x80100000u, kSector = 5u;

void test_distances() {
    expect(first_distance(kAcross, 128, 100) == 128 * 128 + 100 * 100, "the first end's distance, squared");
    expect(second_distance(kAcross, 300, 50) == 44 * 44 + 50 * 50, "the second end's distance, squared");
    expect(scale_index(kAcross) == 256u && scale_index(kAlong) == 512u, "the scale is found by |dz| + |dx|");
    expect(offset_distance(kAcross, 128, 100, 256u) == 100 * 100, "a portal along x: the offset straight across it");
    expect(offset_distance(kAlong, 40, 200, 128u) == 60 * 60, "a portal along z: the offset straight across it");
}

void test_within() {
    expect(within(kAcross, 128, 100, 101u, 256u) && !within(kAcross, 128, 100, 99u, 256u),
           "a portal whose ends are far is reached across its middle");
    expect(!within(kAcross, 300, 50, 60u, 256u), "the line is near, but past the portal's end");
    expect(within(kAcross, 300, 50, 67u, 256u), "the nearer end is in reach");
    expect(within(kAlong, 40, 200, 60u, 128u) && !within(kAlong, 40, 200, 59u, 128u),
           "a portal along z is reached across its middle");
    expect(!within(kAlong, 40, 600, 70u, 128u), "beyond a portal along z the line does not count");
}

void test_walk() {
    Walk walk;
    expect(!walk.enter(0u, false), "the walk's first sector is the game's own");
    expect(!walk.enter(1u, true), "a portal the game's distance reaches");
    expect(walk.enter(2u, false), "a portal past it");
    expect(walk.enter(3u, true), "below a portal past it");
    expect(!walk.enter(2u, true), "the next portal at that depth is reached again");
    expect(!walk.enter(3u, true), "and so is what is below it");
    expect(!walk.enter(kDepthCap, false), "the game's own cap returns before anything is asked for");
}

Memory memory_with(bool resident, uint32_t sector) {
    Memory memory;
    memory.words[kStack + kPortalOnStack] = kGate;
    memory.halves[kGate + 4u] = 256u;
    memory.bytes[kGate + kPortalSector] = sector;
    memory.words[kCamera] = 128u;
    memory.words[kCamera + 4u] = 100u;
    memory.halves[kScales + 2u * 256u] = 256u;
    if (resident) memory.words[kSectors + 4u * kSector] = 0x801DF744u;
    return memory;
}

Entry walk_into(const Memory &memory, uint32_t game_far, uint32_t walked_far) {
    Walk walk;
    walk.enter(0u, true);
    return enter(memory, walk, kStack, 1u, game_far, walked_far);
}

void test_enter() {
    const Memory out = memory_with(false, kSector), in = memory_with(true, kSector);
    const Entry past = walk_into(out, 99u, 200u);
    expect(past.hold && past.agrees, "past the game's distance, a sector out of memory turns the walk back");
    expect(!walk_into(out, 101u, 200u).hold, "within the game's distance the game asks for the area itself");
    expect(!walk_into(in, 99u, 200u).hold, "a sector in memory is drawn farther");
    expect(!walk_into(memory_with(false, kNoSector), 99u, 200u).hold, "the open sky is no sector");
    expect(!walk_into(out, 99u, 50u).agrees, "a portal the copy does not pass at the walked distance is told");

    Walk walk;
    walk.enter(0u, true);
    expect(!enter(in, walk, kStack, 1u, 99u, 200u).hold, "the first portal past the distance leads into memory");
    expect(enter(memory_with(false, kSector), walk, kStack, 2u, 500u, 500u).hold,
           "below it a reachable portal is still past the game's distance");
}

}  // namespace

int main() {
    test_distances();
    test_within();
    test_walk();
    test_enter();
    if (g_failures != 0) return 1;
    std::cout << "disruptor sector reach: PASS\n";
    return 0;
}
