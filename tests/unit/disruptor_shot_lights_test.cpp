#include "game/render/shot_lights.h"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <map>

namespace {

using namespace disruptor::shot_lights;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* The game's memory: a read outside its 2 MB is counted, and gives what a stray read might, ones. */
bool in_game_memory(uint32_t address) { return address >= 0x80000000u && address < 0x80200000u; }

struct Memory {
    std::map<uint32_t, uint8_t> bytes;
    mutable int outside = 0;
    uint32_t byte(uint32_t address) const {
        if (!in_game_memory(address)) {
            ++outside;
            return 0xFFu;
        }
        const auto found = bytes.find(address);
        return found == bytes.end() ? 0u : found->second;
    }
    uint32_t word(uint32_t address) const {
        return byte(address) | byte(address + 1u) << 8 | byte(address + 2u) << 16 | byte(address + 3u) << 24;
    }
    void put(uint32_t address, uint32_t value) {
        for (uint32_t at = 0; at < 4u; ++at) bytes[address + at] = static_cast<uint8_t>(value >> (8u * at));
    }
};

constexpr uint32_t kRecord = 0x800B8F10u, kDescription = 0x8019EB7Cu;

/* The robot's bolt description as mission 9 loads it (capsule 1792805320): its frames' pointer, then 48 bytes. */
constexpr uint32_t kBolt[] = {0x8019E99Cu, 0x00000000u, 0x00000030u, 0x000000DCu, 0x00000010u, 0x00000020u, 0x00000040u,
                              0x00000040u, 0x070FFF1Au, 0x1E010001u, 0x434343FFu, 0x150C0300u, 0x20202040u, 0x20202040u,
                              0x00000063u, 0x000000B1u};

Memory robot_shot(uint32_t kind) {
    Memory memory;
    for (uint32_t at = 0; at < std::size(kBolt); ++at) memory.put(kDescription + 4u * at, kBolt[at]);
    memory.put(kKinds + 4u * kind, kDescription);
    memory.bytes[kRecord + kKindByte] = static_cast<uint8_t>(kind);
    return memory;
}

void finds_the_robots_bolt() {
    expect(description_key(robot_shot(4u), kDescription) == kLights[0].key, "the key is the robot's bolt description's");
    expect(gain(robot_shot(4u), kRecord) == 10.0f, "the robot's bolt lights ten times what its picture gives");
    expect(gain(robot_shot(2u), kRecord) == 10.0f, "under another kind's number too, as another level numbers it");
    Memory moved = robot_shot(4u);
    moved.put(kDescription, 0x80123456u);
    expect(gain(moved, kRecord) == 10.0f, "wherever its frames lie");
    Memory level = robot_shot(4u);
    level.put(kDescription + kLevelFrom, 0x444444FFu);
    expect(gain(level, kRecord) == 10.0f, "and with the level's own bytes, as level byte 4 sets them");
    for (const uint8_t stage : {1u, 3u, 255u}) {
        Memory hit = robot_shot(4u);
        hit.bytes[kRecord + kStageByte] = stage;
        expect(gain(hit, kRecord) == 4.0f, "its hit lights four times, at every frame of it");
    }
}

void leaves_other_shots() {
    Memory other = robot_shot(4u);
    other.bytes[kDescription + 36u] = 0x02u;
    expect(gain(other, kRecord) == 1.0f, "another description keeps its picture's light");
    for (const uint32_t at : {kKeyFrom, kLevelFrom - 1u, kLevelFrom + kLevelBytes, kKeyFrom + kKeyBytes - 1u}) {
        Memory changed = robot_shot(4u);
        changed.bytes[kDescription + at] ^= 1u;
        expect(gain(changed, kRecord) == 1.0f, "every byte of the 48 but the level's own is the description's key");
    }
    Memory past = robot_shot(4u);
    past.bytes[kDescription + kKeyFrom + kKeyBytes] ^= 1u;
    past.bytes[kDescription + kKeyFrom - 1u] ^= 1u;
    expect(gain(past, kRecord) == 10.0f, "and only those");
    Memory none = robot_shot(4u);
    none.bytes[kRecord + kKindByte] = static_cast<uint8_t>(kNoKind);
    none.put(kKinds + 4u * kNoKind, kDescription);
    expect(gain(none, kRecord) == 1.0f, "a record without a kind has none");
    Memory empty = robot_shot(4u);
    empty.put(kKinds + 16u, 0u);
    expect(gain(empty, kRecord) == 1.0f, "nor a kind without a description");
    for (const uint32_t record : {0u, 0x7FFFFFF0u, 0x801FFFFFu, 0x801FFFE3u, 0x80200000u, 0x80200001u, 0xFFFFFFF0u}) {
        Memory memory = robot_shot(4u);
        memory.bytes[record + kKindByte] = 4u;
        expect(gain(memory, record) == 1.0f && memory.outside == 0, "nor a record outside the game's memory, read or not");
    }
    Memory edge = robot_shot(4u);
    edge.bytes[0x801FFFE2u + kKindByte] = 4u;
    edge.bytes[0x801FFFE2u + kStageByte] = 1u;
    expect(gain(edge, 0x801FFFE2u) == 4.0f && edge.outside == 0, "a record whose stage is the memory's last byte is read whole");
    for (const uint32_t description : {0x801FFFC9u, 0x801FFFFCu, 0x00001000u, 0x80200000u, 0xFFFFFFFCu}) {
        Memory high = robot_shot(4u);
        high.put(kKinds + 16u, description);
        expect(gain(high, kRecord) == 1.0f && high.outside == 0, "nor a description that runs past it");
    }
    Memory last = robot_shot(4u);
    for (uint32_t at = 0; at < 56u; ++at) last.bytes[0x801FFFC8u + at] = last.bytes[kDescription + at];
    last.put(kKinds + 16u, 0x801FFFC8u);
    expect(gain(last, kRecord) == 10.0f && last.outside == 0, "a description that ends at the memory's last byte is read whole");
}

}  // namespace

int main() {
    finds_the_robots_bolt();
    leaves_other_shots();
    if (g_failures != 0) return 1;
    std::cout << "disruptor shot lights: PASS\n";
    return 0;
}
