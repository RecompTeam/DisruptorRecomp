#include "../../src/game/render/mover_steps.h"

#include <cstdint>
#include <iostream>
#include <map>

namespace {

using namespace disruptor::movers;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Memory {
    std::map<uint32_t, uint32_t> words;
    int writes = 0;
    uint32_t word(uint32_t address) const {
        const auto found = words.find(address & ~3u);
        return found == words.end() ? 0u : found->second;
    }
    uint32_t byte(uint32_t address) const { return (word(address) >> (8u * (address & 3u))) & 0xFFu; }
    void write(uint32_t address, uint32_t value) {
        words[address] = value;
        ++writes;
    }
    void set_byte(uint32_t address, uint32_t value) {
        const uint32_t shift = 8u * (address & 3u);
        words[address & ~3u] = (word(address) & ~(0xFFu << shift)) | (value << shift);
    }
};

/* The game's mover (0x800457A4) from one whole step to another, written out from its instructions. */
uint32_t whole_step(uint32_t scale, uint32_t first, uint32_t second, uint32_t from, uint32_t to) {
    const int32_t along[3] = {static_cast<int32_t>(first) >> 17, static_cast<int32_t>(second << 16) >> 17,
                              static_cast<int32_t>(second) >> 17};
    uint32_t packed = 0u;
    for (int axis = 0; axis < 3; ++axis) {
        const int32_t was = static_cast<int32_t>((from * scale) * static_cast<uint32_t>(along[axis])) >> 16;
        const int32_t is = static_cast<int32_t>((to * scale) * static_cast<uint32_t>(along[axis])) >> 16;
        packed += static_cast<uint32_t>(((is - was) << 1) >> 1) << (20 - 10 * axis);
    }
    return packed;
}

constexpr uint32_t kScale = 0x1111u, kFlags = 0x83u, kUp = 0x00D00000u; /* the first level's doors: fifteen steps, 104 units up */

void test_half_steps() {
    expect(half_step(kScale, kFlags, kUp, 0, 1) == 3u && half_step(kScale, kFlags, kUp, 1, 1) == 4u,
           "a door of fifteen steps over 104 units does not go 3 and then 4 units in its first two half steps");
    expect(half_step(kScale, kFlags, kUp, 15, -1) == 0u - 3u && half_step(kScale, kFlags, kUp, 1, -1) == 0u - 3u,
           "a closing door does not go back by the half step that brought it there");
    const uint32_t ways[][2] = {{kFlags, kUp}, {kFlags, 0xFF300000u}, {0x00D00083u, 0u}, {0xFF300000u, 0x00680000u | 0x9800u},
                                {0x01A00000u, 0x7FFE0000u | 0xFFFEu}};
    for (const auto &way : ways)
        for (const uint32_t scale : {kScale, 0x0800u, 0x2000u, 0x0199u})
            for (uint32_t step = 0; step + 1 <= 65536u / scale; ++step) {
                const uint32_t there = half_step(scale, way[0], way[1], step, 1), back = half_step(scale, way[0], way[1], step + 1, -1);
                expect(there - back == whole_step(scale, way[0], way[1], step, step + 1),
                       "the half step ahead and the half step back from the next step do not meet: a whole step would not be two halves");
                expect(whole_step(scale, way[0], way[1], step + 1, step) == 0u - whole_step(scale, way[0], way[1], step, step + 1) &&
                           back - there == whole_step(scale, way[0], way[1], step + 1, step),
                       "a step down is not two half steps back");
            }
    expect(half_step(0x1111u, 0x00D00083u, 0u, 2, 1) == 4u << 20,
           "the first direction word's high part does not move the field at bit 20 alone");
    expect(half_step(0x1111u, kFlags, 0x00D0u, 2, 1) == 4u << 10,
           "the second direction word's low half does not move the field at bit 10 alone");
    expect(half_step(kScale, kFlags, kUp, 7, 0) == 0u, "no way to go is not no change");
}

constexpr uint32_t kMover = 0x800B835Cu, kSector = 63u, kRecord = 0x801841B0u, kAsked = 0x80183D84u;

Memory at_rest() {
    Memory memory;
    memory.words[kMover] = 0u;
    memory.set_byte(kMover + 2u, 15u);
    memory.set_byte(kMover + 14u, kSector);
    memory.set_byte(kMover + 15u, 0xFFu);
    memory.words[kMover + 20u] = kScale;
    memory.words[kMover + 24u] = kFlags;
    memory.words[kMover + 28u] = kUp;
    memory.words[kSectors + 4u * kSector] = kRecord;
    memory.words[kRecord + 20u] = kAsked;
    memory.set_byte(kAsked + 12u, 7u);
    memory.set_byte(kAsked + 28u, 9u);
    memory.words[kSectors + 4u * 7u] = 0x80180000u;
    memory.words[kSectors + 4u * 9u] = 0x80181000u;
    return memory;
}

Memory with(uint32_t state, uint32_t step, uint32_t countdown = 0u, uint32_t second = 0xFFu) {
    Memory memory = at_rest();
    memory.set_byte(kMover, state);
    memory.set_byte(kMover + 1u, step);
    memory.set_byte(kMover + 4u, countdown);
    memory.set_byte(kMover + 15u, second);
    return memory;
}

void test_coming() {
    expect(coming(with(0, 0), kMover) == 0 && coming(with(0, 15), kMover) == 0 && coming(with(1, 15), kMover) == 0 &&
               coming(with(4, 7), kMover) == 0,
           "a mover at rest, open and waiting, or in no state the game knows is taken to step");
    expect(coming(with(3, 0), kMover) == 1 && coming(with(3, 7), kMover) == 1 && coming(with(3, 14), kMover) == 1,
           "an opening mover is not taken to step up");
    expect(coming(with(3, 15), kMover) == 0, "a mover that opened fully is taken to step past its last step");
    expect(coming(with(2, 15), kMover) == -1 && coming(with(2, 1), kMover) == -1 && coming(with(2, 0), kMover) == 0,
           "a closing mover does not step down until it is shut");
    expect(coming(with(1, 15, 1), kMover) == -1 && coming(with(1, 15, 2), kMover) == 0 && coming(with(1, 15, 0), kMover) == 0,
           "a mover waiting open does not start to close on the run that ends its countdown, and only then");
    expect(coming(with(1, 15, 0, 1), kMover) == -1 && coming(with(1, 15, 0, 2), kMover) == 0 && coming(with(1, 15, 0, 0), kMover) == 0,
           "a mover waiting open does not start to close on the run that ends its second timer, and only then");
    expect(coming(with(3, 7, 1, 1), kMover) == 1 && coming(with(0, 0, 1, 1), kMover) == 0,
           "a timer running out turns a mover that is not waiting open");
    for (const uint32_t asked : {12u, 28u}) {
        Memory locked = with(3, 0);
        locked.words[kSectors + 4u * locked.byte(kAsked + asked)] = 0u;
        expect(coming(locked, kMover) == 0, "a door whose way is locked is taken to open");
        locked.set_byte(kMover + 1u, 1u);
        expect(coming(locked, kMover) == 1, "the lock is asked after the first step");
    }
}

constexpr uint32_t kGeometry = 0x80182E3Cu;

/* A static part, then two moving parts with three and two vertices, then the end. */
void lay_geometry(Memory &memory, uint32_t at = kGeometry) {
    memory.words[at] = (0x20u << 8) | 5u;
    const uint32_t one = at + 0x20u, two = one + 0x40u, end = two + 0x30u;
    memory.words[one] = (0x40u << 8) | 6u;
    memory.words[one + 4u] = 8u;
    for (uint32_t index = 0; index < 3u; ++index) memory.words[one + 16u + 4u * index] = 0x04010040u + index;
    memory.words[one + 28u] = kEnd;
    memory.words[two] = (0x30u << 8) | 7u;
    memory.words[two + 4u] = 0u;
    for (uint32_t index = 0; index < 2u; ++index) memory.words[two + 8u + 4u * index] = 0xC8020080u + index;
    memory.words[two + 16u] = kEnd;
    memory.words[end] = kEnd;
}

void test_parts() {
    Memory memory;
    lay_geometry(memory);
    Part found[kMostParts] = {};
    expect(parts(memory, kGeometry, found) == 2 && found[0].first == kGeometry + 0x30u && found[0].last == kGeometry + 0x3Cu &&
               found[1].first == kGeometry + 0x68u && found[1].last == kGeometry + 0x70u,
           "the moving parts of a sector are not found where the game's mover finds them");
    Memory still;
    still.words[kGeometry] = (0x20u << 8) | 5u;
    still.words[kGeometry + 0x20u] = kEnd;
    expect(parts(still, kGeometry, found) == 0, "a sector with no moving part has one");
    Memory endless;
    endless.words[kGeometry] = 5u;
    endless.words[kGeometry + 4u] = 0u;
    endless.words[kGeometry + 8u] = kEnd;
    expect(parts(endless, kGeometry, found) == 0, "a walk that never leaves its part is not refused");
    Memory long_list;
    long_list.words[kGeometry] = 0x20u << 8;
    long_list.words[kGeometry + 0x20u] = 0x8000u << 8;
    long_list.words[kGeometry + 0x24u] = 0u;
    long_list.words[kGeometry + 0x8020u] = kEnd;
    expect(parts(long_list, kGeometry, found) == 0, "a vertex list without an end is not refused");
}

constexpr uint32_t kList = 0x800B84DCu, kOther = 0x800B8400u;

Memory level(uint32_t state, uint32_t step) {
    Memory memory = with(state, step);
    lay_geometry(memory);
    memory.words[kRecord + 16u] = kGeometry;
    memory.words[kMover + 32u] = 0x00D00083u; /* the second part's own direction pair: along the field at bit 20 */
    memory.words[kMover + 36u] = 0u;
    memory.words[kOther] = 0u;
    memory.set_byte(kOther, 3u);
    memory.set_byte(kOther + 1u, 4u);
    memory.set_byte(kOther + 2u, 15u);
    memory.set_byte(kOther + 14u, 0xFFu);
    memory.words[kList] = kOther;
    memory.words[kList + 4u] = kMover;
    return memory;
}

void test_lead_and_take_back() {
    const Memory before = level(3, 3);
    Memory memory = before;
    Shifts shifts;
    lead(memory, shifts, kGeometry, kList, 2u, 12u);
    const uint32_t up = half_step(kScale, kFlags, kUp, 3, 1), along = half_step(kScale, 0x00D00083u, 0u, 3, 1);
    bool moved = shifts.count == 2 && up != 0u && along != 0u && up != along;
    for (uint32_t index = 0; index < 3u; ++index)
        moved = moved && memory.word(kGeometry + 0x30u + 4u * index) == before.word(kGeometry + 0x30u + 4u * index) + up;
    for (uint32_t index = 0; index < 2u; ++index)
        moved = moved && memory.word(kGeometry + 0x68u + 4u * index) == before.word(kGeometry + 0x68u + 4u * index) + along;
    expect(moved, "an opening door's vertices are not half a step ahead, each part by its own direction");
    expect(memory.writes == 5 && memory.word(kGeometry + 0x3Cu) == kEnd && memory.word(kGeometry + 0x70u) == kEnd,
           "something besides the vertices was written, or a list's end was");
    lead(memory, shifts, kGeometry, kList, 2u, 12u);
    expect(memory.writes == 5 && shifts.count == 2, "a sector drawn again before the half step is taken back moves again");
    take_back(memory, shifts);
    expect(memory.words == before.words && shifts.count == 0, "taking a half step back does not leave memory as it was");
    take_back(memory, shifts);
    expect(memory.writes == 10, "a half step is taken back twice");

    const struct { uint32_t state, step, geometry, count, player; const char *message; } none[] = {
        {0, 0, kGeometry, 2u, 12u, "a mover at rest is moved"},
        {3, 15, kGeometry, 2u, 12u, "a mover on its last step is moved"},
        {3, 3, kGeometry + 0x1000u, 2u, 12u, "a mover is moved while another sector is drawn"},
        {3, 3, kGeometry, 1u, 12u, "a mover past the list's count is moved"},
        {3, 3, kGeometry, 2u, kSector, "the mover the player stands in is moved"},
    };
    for (const auto &one : none) {
        Memory quiet = level(one.state, one.step);
        lay_geometry(quiet, kGeometry + 0x1000u);
        quiet.writes = 0;
        Shifts nothing;
        lead(quiet, nothing, one.geometry, kList, one.count, one.player);
        expect(quiet.writes == 0 && nothing.count == 0, one.message);
    }
    Memory unloaded = level(3, 3);
    unloaded.words[kSectors + 4u * kSector] = 0u;
    unloaded.words[16u] = kGeometry;
    Shifts nothing;
    lead(unloaded, nothing, kGeometry, kList, 2u, 12u);
    expect(unloaded.writes == 0 && nothing.count == 0, "a mover whose sector has no record is moved");

    Memory flat = level(3, 3);
    flat.words[kMover + 28u] = 0u;
    Shifts one_part;
    lead(flat, one_part, kGeometry, kList, 2u, 12u);
    expect(flat.writes == 2 && one_part.count == 1 && one_part.held[0].part.first == kGeometry + 0x68u,
           "a part that the half step does not move is written or held");

    Memory closing = level(2, 9);
    Shifts down;
    lead(closing, down, kGeometry, kList, 2u, 12u);
    expect(down.count == 2 && closing.word(kGeometry + 0x30u) == level(2, 9).word(kGeometry + 0x30u) + half_step(kScale, kFlags, kUp, 9, -1),
           "a closing door's vertices are not half a step back");

    Memory replaced = before;
    Shifts held;
    lead(replaced, held, kGeometry, kList, 2u, 12u);
    replaced.words[kGeometry + 0x34u] = 0x12345678u;
    const int writes = replaced.writes;
    take_back(replaced, held);
    expect(replaced.writes == writes + 4 && replaced.word(kGeometry + 0x34u) == 0x12345678u && held.count == 0 && held.words == 0u,
           "a word that is no longer what the half step left is written, or the half step stays held");
    replaced.words[kGeometry + 0x34u] = before.word(kGeometry + 0x34u);
    expect(replaced.words == before.words, "the words beside a changed one keep their half step");

    Memory again = before;
    Shifts reused;
    lead(again, reused, kGeometry, kList, 2u, 12u);
    forget(reused);
    again = before;
    lead(again, reused, kGeometry, kList, 2u, 12u);
    take_back(again, reused);
    expect(again.words == before.words, "a half step given after a forgotten one is not taken back whole");

    Memory wide = level(3, 3);
    const uint32_t far = kGeometry + 0x4000u;
    wide.words[kRecord + 16u] = far;
    wide.words[far] = (0x20u << 8) | 5u;
    wide.words[far + 0x20u] = (0x4000u << 8) | 6u;
    wide.words[far + 0x24u] = 0u;
    for (uint32_t index = 0; index < 3000u; ++index) wide.words[far + 0x28u + 4u * index] = 0x04010040u;
    wide.words[far + 0x28u + 4u * 3000u] = kEnd;
    wide.words[far + 0x4020u] = (0x4000u << 8) | 7u;
    wide.words[far + 0x4024u] = 0u;
    for (uint32_t index = 0; index < 3000u; ++index) wide.words[far + 0x4028u + 4u * index] = 0x04010040u;
    wide.words[far + 0x4028u + 4u * 3000u] = kEnd;
    wide.words[far + 0x8020u] = kEnd;
    Shifts most;
    lead(wide, most, far, kList, 2u, 12u);
    expect(most.count == 1 && most.words == 3000u && wide.writes == 3000 && wide.word(far + 0x4028u) == 0x04010040u,
           "more words are led than can be held to take back");

    Memory many = level(3, 3);
    for (uint32_t index = 0; index < kMostMovers; ++index) many.words[kList + 4u * index] = kOther;
    many.words[kList + 4u * kMostMovers] = kMover;
    Shifts bounded;
    lead(many, bounded, kGeometry, kList, kMostMovers + 1u, 12u);
    expect(many.writes == 0, "the list is read past the most movers a level has");
}

constexpr uint32_t kLifts = 0x800C16F0u, kCamera = 0x800775D0u;

uint32_t height_of(uint32_t index) { return kLifts + kLiftBytes * index + kLiftHeight; }
uint32_t floor_of(uint32_t index) { return kLifts + kLiftBytes * index + kLiftFloor; }

/* Lifts drawn at 1000, 2000, 3000 and so on, each standing a player 32 higher. The second carries one whose camera is at 2100. */
Memory lifts_at_rest(uint32_t count = 3u, uint32_t records = kLifts) {
    Memory memory;
    for (uint32_t index = 0; index < count; ++index) {
        memory.words[records + kLiftBytes * index + kLiftHeight] = 1000u * (index + 1u);
        memory.words[records + kLiftBytes * index + kLiftFloor] = 1000u * (index + 1u) + 32u;
    }
    memory.set_byte(records + kLiftBytes + kLiftCarries, 1u);
    memory.words[kCamera] = 2100u;
    return memory;
}

/* A lift steps as the game steps it: both heights by the same amount, and the camera with the second one. */
void step(Memory &memory, uint32_t index, uint32_t to) {
    const uint32_t by = to - memory.words[height_of(index)];
    memory.words[height_of(index)] += by;
    memory.words[floor_of(index)] += by;
    if (index == 1u) memory.words[kCamera] += by;
}

void test_lifts() {
    Memory memory = lifts_at_rest();
    Lifts lifts;
    note(memory, lifts, kLifts, 3u);
    lag(memory, lifts, kLifts, kCamera, 2032u, 1u);
    expect(memory.writes == 0 && lifts.count == 0, "a lift that did not step is held back");
    step(memory, 1u, 1984u);
    const Memory stepped = memory;
    lag(memory, lifts, kLifts, kCamera, 2016u, 1u);
    expect(memory.word(height_of(1)) == 1992u && memory.word(kCamera) == 2092u && memory.writes == 2 && lifts.count == 2,
           "a lift that went down a step is not drawn half that step back with its rider's camera");
    expect(memory.word(height_of(0)) == 1000u && memory.word(height_of(2)) == 3000u && memory.word(floor_of(1)) == 2016u,
           "a lift that stood still is moved, or the height the game collides with is written");
    lag(memory, lifts, kLifts, kCamera, 2016u, 1u);
    expect(memory.writes == 2, "a picture holds a lift back twice");
    catch_up(memory, lifts);
    expect(memory.words == stepped.words && memory.writes == 4 && lifts.count == 0, "catching up does not leave memory as the tick left it");
    catch_up(memory, lifts);
    expect(memory.writes == 4, "a lift catches up twice");

    const auto camera_alone = [&](const Memory &from, uint32_t stands, uint32_t riding, const char *message) {
        Memory seen = from;
        Lifts held;
        note(lifts_at_rest(), held, kLifts, 3u);
        lag(seen, held, kLifts, kCamera, stands, riding);
        expect(seen.word(height_of(1)) == 1992u && seen.word(kCamera) == from.word(kCamera) && seen.writes == 1, message);
    };
    for (const uint32_t riding : {0u, 2u, kNoSector})
        camera_alone(stepped, 2016u, riding, "the camera of a player in another lift's sector is held back, or the lift is not");
    for (const uint32_t stands : {2017u, 2052u, 1984u, 2032u})
        camera_alone(stepped, stands, 1u, "the camera of a player who does not stand on the lift's floor is held back");
    Memory left = stepped;
    left.set_byte(kLifts + kLiftBytes + kLiftCarries, 0u);
    camera_alone(left, 2016u, 1u, "the camera is held back with a lift that does not carry the player");

    /* The lift came up under a player 8 above its floor and took them along: the camera rose 8, the lift 16. */
    Memory picked = lifts_at_rest();
    Lifts up;
    note(picked, up, kLifts, 3u);
    step(picked, 1u, 2016u);
    picked.words[kCamera] = 2116u;
    lag(picked, up, kLifts, kCamera, 2048u, 1u);
    expect(picked.word(height_of(1)) == 2008u && picked.word(kCamera) == 2108u && picked.writes == 2,
           "a player the lift picked up in the tick is not held back with it");

    const struct { uint32_t to; uint32_t shown; int writes; const char *message; } steps[] = {
        {2016u, 2008u, 2, "a lift that went up a step is not drawn half that step back"},
        {1985u, 1992u, 2, "a step of fifteen is not held back by seven"},
        {2001u, 2001u, 0, "a step of one is held back"},
        {1999u, 1999u, 0, "a step of one down is held back"},
        {2000u + 255u, 2000u + 255u - 127u, 2, "the largest step is not held back by half"},
        {2000u + 256u, 2000u + 256u, 0, "a jump too large for a step is held back"},
        {2000u - 256u, 2000u - 256u, 0, "a drop too large for a step is held back"},
    };
    for (const auto &one : steps) {
        Memory moved = lifts_at_rest();
        Lifts held;
        note(moved, held, kLifts, 3u);
        step(moved, 1u, one.to);
        lag(moved, held, kLifts, kCamera, one.to + 32u, 1u);
        expect(moved.word(height_of(1)) == one.shown && moved.writes == one.writes, one.message);
    }

    Memory both = lifts_at_rest();
    Lifts two;
    note(both, two, kLifts, 3u);
    both.set_byte(kLifts + 2u * kLiftBytes + kLiftCarries, 1u);
    step(both, 0u, 1016u);
    step(both, 2u, 2984u);
    lag(both, two, kLifts, kCamera, 3016u, 2u);
    expect(both.word(height_of(0)) == 1008u && both.word(height_of(2)) == 2992u && both.word(kCamera) == 2108u && both.writes == 3,
           "two lifts that stepped are not each held back, with the camera by its own lift's step");

    Memory elsewhere = stepped;
    Lifts stale;
    note(lifts_at_rest(3u, kLifts + 0x1000u), stale, kLifts + 0x1000u, 3u);
    lag(elsewhere, stale, kLifts, kCamera, 2016u, 1u);
    expect(elsewhere.writes == 0, "heights noted for another level's records are compared with these");
    Lifts forgotten;
    note(lifts_at_rest(), forgotten, kLifts, 3u);
    forget(forgotten);
    Memory after_load = stepped;
    lag(after_load, forgotten, kLifts, kCamera, 2016u, 1u);
    expect(after_load.writes == 0 && forgotten.known == 0u, "a forgotten note still holds a lift back");

    Memory loaded = stepped;
    Lifts dropped;
    note(lifts_at_rest(), dropped, kLifts, 3u);
    lag(loaded, dropped, kLifts, kCamera, 2016u, 1u);
    const Memory lagged = loaded;
    forget(dropped);
    catch_up(loaded, dropped);
    expect(loaded.words == lagged.words && loaded.writes == lagged.writes && dropped.count == 0,
           "a lag forgotten at a state's load is given back to the loaded memory");

    Memory many = lifts_at_rest(40u);
    Lifts bounded;
    note(many, bounded, kLifts, 40u);
    many.set_byte(kLifts + 35u * kLiftBytes + kLiftCarries, 1u);
    step(many, 31u, 32000u - 16u);
    step(many, 35u, 36000u - 16u);
    lag(many, bounded, kLifts, kCamera, 36016u, 35u);
    expect(bounded.known == kMostLifts && many.writes == 1 && many.word(height_of(31)) == 32000u - 8u,
           "lifts past the most a level has are noted or held back");

    Memory written = stepped;
    Lifts over;
    note(lifts_at_rest(), over, kLifts, 3u);
    lag(written, over, kLifts, kCamera, 2016u, 1u);
    written.words[kCamera] = 5000u;
    const int before = written.writes;
    catch_up(written, over);
    expect(written.word(kCamera) == 5000u && written.word(height_of(1)) == 1984u && written.writes == before + 1,
           "a word the game wrote meanwhile is written over, or the other is not given back");
}

} // namespace

int main() {
    test_half_steps();
    test_coming();
    test_parts();
    test_lead_and_take_back();
    test_lifts();
    if (g_failures) return 1;
    std::cout << "PASS: movers: the coming step, half steps, a sector's moving parts, the lead and its taking back, a lift's lag\n";
    return 0;
}
