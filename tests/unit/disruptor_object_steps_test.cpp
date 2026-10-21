#include "../../src/game/render/object_steps.h"

#include <cstdint>
#include <iostream>
#include <map>

namespace {

using namespace disruptor::objects;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Memory {
    std::map<uint32_t, uint8_t> bytes;
    uint32_t byte(uint32_t address) const {
        const auto found = bytes.find(address);
        return found == bytes.end() ? 0u : found->second;
    }
    uint32_t half(uint32_t address) const { return byte(address) | byte(address + 1u) << 8; }
    uint32_t word(uint32_t address) const { return half(address) | half(address + 2u) << 16; }
    void write_half(uint32_t address, uint16_t value) {
        bytes[address] = static_cast<uint8_t>(value);
        bytes[address + 1u] = static_cast<uint8_t>(value >> 8);
    }
    void write(uint32_t address, uint32_t value) {
        write_half(address, static_cast<uint16_t>(value));
        write_half(address + 2u, static_cast<uint16_t>(value >> 16));
    }
};

constexpr uint32_t kShotA = 0x800BDFE0u, kShotB = 0x800BE070u, kActor = 0x800B72DCu, kOther = 0x800B7344u;

void place_shot(Memory &memory, uint32_t record, int32_t x, int32_t z, int32_t y, uint16_t life, int16_t vx = 4737,
                int16_t vz = -3768) {
    memory.write(record + 4u, static_cast<uint32_t>(x));
    memory.write(record + 8u, static_cast<uint32_t>(z));
    memory.write(record + 12u, static_cast<uint32_t>(y));
    memory.write_half(record + kShotLife, life);
    memory.write_half(record + kShotVelocity, static_cast<uint16_t>(vx));
    memory.write_half(record + kShotVelocity + 2u, static_cast<uint16_t>(vz));
}

void place_actor(Memory &memory, uint32_t record, uint16_t x, uint16_t z, uint16_t y) {
    memory.write_half(record + 22u, x);
    memory.write_half(record + 24u, z);
    memory.write_half(record + 26u, y);
}

int32_t shot_x(const Memory &memory, uint32_t record) { return static_cast<int32_t>(memory.word(record + 4u)); }

/* The capsule's corridor: two shots in sector 4, two actors in sector 9. */
Memory corridor() {
    Memory memory;
    memory.write(kSectorShots + 4u * 4u, kShotA);
    memory.write(kShotA, kShotB);
    place_shot(memory, kShotA, 100496, 14020, 15552, 46);
    place_shot(memory, kShotB, 101680, 14364, 15552, 42, 5403, -2613);
    memory.write(kSectorActors + 4u * 9u, kActor);
    memory.write(kActor, kOther);
    place_actor(memory, kActor, 5550, 2664, 898);
    place_actor(memory, kOther, 5235, 2700, 898);
    return memory;
}

void test_half_step_back() {
    Memory memory = corridor();
    Steps steps;
    note(memory, steps);
    expect(steps.known == 4u, "the two shots and the two actors are noted");
    place_shot(memory, kShotA, 100644, 13902, 15552, 45);
    place_actor(memory, kActor, 5568, 2664, 898);
    lag(memory, steps);
    expect(shot_x(memory, kShotA) == 100570 && static_cast<int32_t>(memory.word(kShotA + 8u)) == 13961,
           "a shot that stepped 148 and -118 is drawn half of that back");
    expect(memory.half(kActor + 22u) == 5559u && memory.half(kActor + 24u) == 2664u, "an actor that stepped 18 is drawn 9 back");
    expect(shot_x(memory, kShotB) == 101680 && memory.half(kOther + 22u) == 5235u, "objects that did not step are left");
    catch_up(memory, steps);
    expect(shot_x(memory, kShotA) == 100644 && static_cast<int32_t>(memory.word(kShotA + 8u)) == 13902 &&
               memory.half(kActor + 22u) == 5568u,
           "the game gets every coordinate back as it left them");
    lag(memory, steps);
    catch_up(memory, steps);
    note(memory, steps);
    lag(memory, steps);
    expect(steps.count == 0u && shot_x(memory, kShotA) == 100644, "a tick that stepped nothing holds nothing back");
}

void test_new_shot_in_a_record() {
    Memory memory = corridor();
    Steps steps;
    note(memory, steps);
    place_shot(memory, kShotB, 101700, 14300, 15552, 63, 5403, -2613);
    lag(memory, steps);
    expect(shot_x(memory, kShotB) == 101700, "a new shot in a noted shot's record is drawn where it is");
}

void test_shot_moved_into_another_record() {
    Memory memory = corridor();
    memory.write(kShotA, 0u);
    place_shot(memory, kShotA, 97808, 14078, 15552, 63, 6150, 1220);
    Steps steps;
    note(memory, steps);
    /* The game moved the shot into a freed record and dropped its own: the record left behind holds what it held. */
    memory.write(kSectorShots + 4u * 4u, kShotB);
    memory.write(kShotB, 0u);
    place_shot(memory, kShotB, 98000, 14116, 15552, 62, 6150, 1220);
    lag(memory, steps);
    expect(shot_x(memory, kShotB) == 97904 && static_cast<int32_t>(memory.word(kShotB + 8u)) == 14097,
           "a shot the game moved into another record steps from where it was noted");
    expect(shot_x(memory, kShotA) == 97808, "the record it left is not written");
}

void test_far_moves_are_not_steps() {
    Memory memory = corridor();
    Steps steps;
    note(memory, steps);
    place_actor(memory, kActor, 5550 + kMostActorStep + 2, 2664, 898);
    place_shot(memory, kShotA, 100496 + kMostShotStep + 2, 14020, 15552, 45);
    lag(memory, steps);
    expect(steps.count == 0u, "an actor or a shot moved further than a step is drawn where it is");
}

void test_rewritten_coordinate_stays() {
    Memory memory = corridor();
    Steps steps;
    note(memory, steps);
    place_actor(memory, kActor, 5568, 2682, 898);
    lag(memory, steps);
    memory.write_half(kActor + 22u, 7000u);
    catch_up(memory, steps);
    expect(memory.half(kActor + 22u) == 7000u && memory.half(kActor + 24u) == 2682u,
           "a coordinate the game wrote meanwhile stays, the other one comes back");
}

void test_lists_that_do_not_end() {
    Memory memory = corridor();
    memory.write(kOther, kActor);
    memory.write(kSectorActors + 4u * 200u, 0x80300000u);
    Steps steps;
    note(memory, steps);
    expect(steps.known == kMostObjects, "a list that loops stops at the most objects kept");
    Memory odd = corridor();
    odd.write(kSectorActors + 4u * 9u, kActor + 2u);
    Steps none;
    note(odd, none);
    expect(none.known == 2u, "a record outside main memory or off its alignment is no actor");
}

/* Two shots of one life and velocity, 100 apart: the first moves into a third record as it steps. */
void test_equal_shots_in_a_compaction() {
    constexpr uint32_t kShotC = 0x800BE0B8u;
    Memory memory;
    memory.write(kSectorShots, kShotA);
    memory.write(kShotA, kShotB);
    place_shot(memory, kShotA, 0, 0, 0, 10, 2880, 0);
    place_shot(memory, kShotB, 100, 0, 0, 10, 2880, 0);
    Steps steps;
    note(memory, steps);
    memory.write(kSectorShots, kShotC);
    memory.write(kShotC, kShotB);
    memory.write(kShotB, 0u);
    place_shot(memory, kShotC, 90, 0, 0, 9, 2880, 0);
    place_shot(memory, kShotB, 190, 0, 0, 9, 2880, 0);
    lag(memory, steps);
    expect(shot_x(memory, kShotC) == 45 && shot_x(memory, kShotB) == 145,
           "a moved shot steps from the noted shot its velocity brings there, not from the nearer one");
    Memory twins;
    twins.write(kSectorShots, kShotA);
    twins.write(kShotA, kShotB);
    place_shot(twins, kShotA, 0, 0, 0, 10, 2880, 0);
    place_shot(twins, kShotB, 0, 0, 0, 10, 2880, 0);
    Steps both;
    note(twins, both);
    twins.write(kSectorShots, kShotC);
    twins.write(kShotC, 0u);
    place_shot(twins, kShotC, 90, 0, 0, 9, 2880, 0);
    lag(twins, both);
    expect(shot_x(twins, kShotC) == 90, "a moved shot two noted shots could have become is drawn where it is");
}

void test_shot_off_its_velocity() {
    Memory memory = corridor();
    Steps steps;
    note(memory, steps);
    place_shot(memory, kShotB, 101808, 14574, 15768, 41, 5403, -2613);
    lag(memory, steps);
    expect(shot_x(memory, kShotB) == 101808, "a shot that did not go its velocity, as at a hit, is drawn where it is");
}

void test_actor_taken_over_nearby() {
    Memory memory = corridor();
    memory.write(kActor + 4u, 0x800B44FCu);
    memory.write(kActor + 44u, 0x0A000B00u);
    Steps steps;
    note(memory, steps);
    place_actor(memory, kActor, 5570, 2664, 898);
    memory.write(kActor + 4u, 0x800B4520u);
    lag(memory, steps);
    expect(memory.half(kActor + 22u) == 5570u, "another actor taking the record over 20 units away is drawn where it is");
}

}  // namespace

int main() {
    test_half_step_back();
    test_new_shot_in_a_record();
    test_shot_moved_into_another_record();
    test_far_moves_are_not_steps();
    test_rewritten_coordinate_stays();
    test_lists_that_do_not_end();
    test_equal_shots_in_a_compaction();
    test_shot_off_its_velocity();
    test_actor_taken_over_nearby();
    if (g_failures == 0) std::cout << "object steps: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
