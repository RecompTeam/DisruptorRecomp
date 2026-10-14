#pragma once

#include <cstdint>
#include <initializer_list>

/* Doors and lifts are the game's movers: it steps them on every second tick. Under 60 FPS a frame after an odd tick
 * shows a door half a step towards the next tick's step, in its vertex words and only while its sector is drawn, and
 * a frame after a lift's step shows the lift, and the camera of a player on it, half that step back. */
namespace disruptor::movers {

inline constexpr uint32_t kSectors = 0x800776E4u; /* a word a sector: its record, +16 the geometry, +20 what a locked door asks */
inline constexpr uint32_t kNoSector = 0xFFu;
inline constexpr uint32_t kEnd = 0xFFFFFFFFu;
inline constexpr int kMostParts = 8;
inline constexpr uint32_t kMostVertices = 4096u;
inline constexpr uint32_t kMostMovers = 64u;
inline constexpr uint32_t kMostHeld = 4096u;
inline constexpr uint32_t kMostLifts = 32u;
inline constexpr uint32_t kLiftBytes = 36u;  /* a lift's record */
inline constexpr uint32_t kLiftHeight = 20u; /* the height the lift is drawn at (0x8003A42C) */
inline constexpr uint32_t kLiftFloor = 8u;   /* the height it stands the player on (0x8002392C) */
inline constexpr uint32_t kLiftCarries = 27u; /* set while it carries the player (0x800238EC) */
inline constexpr uint32_t kSectorLift = 46u; /* a sector's lift, 0xFF for none (0x8003A40C) */
inline constexpr int32_t kMostLiftStep = 255;

/* What the next run of the game's mover routine (0x80023D2C) does to the step of a mover with a sector: -1, 0 or +1. */
template <class Memory>
int coming(const Memory &memory, uint32_t mover) {
    uint32_t state = memory.byte(mover);
    const uint32_t step = memory.byte(mover + 1u);
    if (memory.byte(mover + 4u) == 1u && state == 1u) state = 2u;
    if (memory.byte(mover + 15u) == 1u && state == 1u) state = 2u;
    if (state == 2u) return step != 0u ? -1 : 0;
    if (state != 3u) return 0;
    if (step == 0u) {
        const uint32_t asked = memory.word(memory.word(kSectors + 4u * memory.byte(mover + 14u)) + 20u);
        if (memory.word(kSectors + 4u * memory.byte(asked + 12u)) == 0u) return 0;
        if (memory.word(kSectors + 4u * memory.byte(asked + 28u)) == 0u) return 0;
    }
    return step != memory.byte(mover + 2u) ? 1 : 0;
}

/* The packed vertex change from `step` to half a step `towards` the next (+1 or -1): the game's mover (0x800457A4) at twice its resolution. */
inline uint32_t half_step(uint32_t scale, uint32_t first, uint32_t second, uint32_t step, int towards) {
    const int32_t along[3] = {static_cast<int32_t>(first) >> 17, static_cast<int32_t>(second << 16) >> 17,
                              static_cast<int32_t>(second) >> 17};
    const uint32_t whole = step * scale, half = ((2u * step + static_cast<uint32_t>(towards)) * scale) >> 1;
    uint32_t packed = 0u;
    for (int axis = 0; axis < 3; ++axis) {
        const int32_t from = static_cast<int32_t>(whole * static_cast<uint32_t>(along[axis])) >> 16;
        const int32_t to = static_cast<int32_t>(half * static_cast<uint32_t>(along[axis])) >> 16;
        packed += static_cast<uint32_t>(to - from) << (20 - 10 * axis);
    }
    return packed;
}

/* The vertex words of one moving part of a sector. */
struct Part {
    uint32_t first, last;
};

/* A sector's moving parts as the game's mover walks them. 0 when there is none or the walk does not end. */
template <class Memory>
int parts(const Memory &memory, uint32_t geometry, Part found[kMostParts]) {
    int count = 0;
    for (uint32_t at = geometry;;) {
        at += memory.word(at) >> 8;
        if (memory.word(at) == kEnd) return count;
        if (count == kMostParts) return 0;
        const uint32_t first = at + 8u + memory.word(at + 4u);
        uint32_t last = first;
        while (memory.word(last) != kEnd) {
            last += 4u;
            if (last - first > 4u * kMostVertices) return 0;
        }
        found[count++] = {first, last};
    }
}

/* The half steps now in memory, with every word as its half step left it. */
struct Shifts {
    struct One {
        Part part;
        uint32_t delta;
    };
    One held[kMostParts];
    uint32_t left[kMostHeld];
    int count = 0;
    uint32_t words = 0;
};

inline void forget(Shifts &shifts) {
    shifts.count = 0;
    shifts.words = 0u;
}

/* Takes every half step back, word by word: a word that is no longer what the half step left (the game wrote it, or another level's data lies there) stays as it is. */
template <class Memory>
void take_back(Memory &memory, Shifts &shifts) {
    uint32_t index = 0u;
    for (int part = 0; part < shifts.count; ++part) {
        const Shifts::One &one = shifts.held[part];
        for (uint32_t at = one.part.first; at != one.part.last; at += 4u, ++index)
            if (memory.word(at) == shifts.left[index]) memory.write(at, shifts.left[index] - one.delta);
    }
    forget(shifts);
}

/* Before the sector with this geometry is drawn: its mover, when it steps on the next tick and the player does not stand in it, goes half a step ahead. */
template <class Memory>
void lead(Memory &memory, Shifts &shifts, uint32_t geometry, uint32_t list, uint32_t movers, uint32_t player) {
    if (shifts.count != 0) return;
    for (uint32_t index = 0; index < movers && index < kMostMovers; ++index) {
        const uint32_t mover = memory.word(list + 4u * index), sector = memory.byte(mover + 14u);
        if (sector == kNoSector || sector == player) continue;
        const uint32_t record = memory.word(kSectors + 4u * sector);
        if (record == 0u || memory.word(record + 16u) != geometry) continue;
        const int towards = coming(memory, mover);
        Part found[kMostParts];
        const int count = towards != 0 ? parts(memory, geometry, found) : 0;
        for (int part = 0; part < count; ++part) {
            const uint32_t delta = half_step(memory.word(mover + 20u), memory.word(mover + 24u + 8u * part),
                                             memory.word(mover + 28u + 8u * part), memory.byte(mover + 1u), towards);
            if (delta == 0u || shifts.words + (found[part].last - found[part].first) / 4u > kMostHeld) continue;
            for (uint32_t at = found[part].first; at != found[part].last; at += 4u) {
                shifts.left[shifts.words] = memory.word(at) + delta;
                memory.write(at, shifts.left[shifts.words++]);
            }
            shifts.held[shifts.count++] = {found[part], delta};
        }
        return;
    }
}

/* The lifts' heights when the tick began, and the half steps the picture drawn after it holds back. */
struct Lifts {
    struct Held {
        uint32_t address, back, left;
    };
    uint32_t began[kMostLifts];
    uint32_t records = 0, known = 0;
    Held held[kMostLifts + 1];
    int count = 0;
};

inline void forget(Lifts &lifts) {
    lifts.known = 0u;
    lifts.count = 0;
}

template <class Memory>
void note(const Memory &memory, Lifts &lifts, uint32_t records, uint32_t count) {
    lifts.records = records;
    lifts.known = count < kMostLifts ? count : kMostLifts;
    for (uint32_t index = 0; index < lifts.known; ++index) lifts.began[index] = memory.word(records + kLiftBytes * index + kLiftHeight);
}

/* Gives back what the picture held back. A word that is no longer what was left there stays as it is. */
template <class Memory>
void catch_up(Memory &memory, Lifts &lifts) {
    for (int index = 0; index < lifts.count; ++index) {
        const Lifts::Held &one = lifts.held[index];
        if (memory.word(one.address) == one.left) memory.write(one.address, one.left + one.back);
    }
    lifts.count = 0;
}

/* A lift that stepped in the tick just run is drawn half its step back, with the camera of a player on its floor (`stands`). */
template <class Memory>
void lag(Memory &memory, Lifts &lifts, uint32_t records, uint32_t camera, uint32_t stands, uint32_t riding) {
    if (lifts.count != 0 || records != lifts.records) return;
    for (uint32_t index = 0; index < lifts.known; ++index) {
        const uint32_t record = records + kLiftBytes * index, height = record + kLiftHeight;
        const int32_t step = static_cast<int32_t>(memory.word(height) - lifts.began[index]);
        if (step / 2 == 0 || step > kMostLiftStep || step < -kMostLiftStep) continue;
        const bool carries = index == riding && memory.byte(record + kLiftCarries) != 0u && memory.word(record + kLiftFloor) == stands;
        for (const uint32_t address : {height, camera}) {
            if (address == camera && !carries) continue;
            const uint32_t left = memory.word(address) - static_cast<uint32_t>(step / 2);
            memory.write(address, left);
            lifts.held[lifts.count++] = {address, static_cast<uint32_t>(step / 2), left};
        }
    }
}

} // namespace disruptor::movers
