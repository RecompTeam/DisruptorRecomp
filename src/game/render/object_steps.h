#pragma once

#include <cstdint>

/* Enemies and shots step on every second tick too. Under 60 FPS a frame after their step shows them half that step back. */
namespace disruptor::objects {

inline constexpr uint32_t kSectorShots = 0x80077BF4u;  /* a word a sector: its first shot, 36 bytes, the next at +0 (0x8003C03C) */
inline constexpr uint32_t kSectorActors = 0x80078004u; /* a word a sector: its first actor, 104 bytes, the next at +0 (0x80026C70) */
inline constexpr uint32_t kSectorCount = 256u;         /* an actor's sector is a byte (+56) */
inline constexpr uint32_t kShotLife = 32u;             /* a halfword one lower after each step */
inline constexpr uint32_t kShotVelocity = 16u;         /* x and z halfwords */
inline constexpr uint32_t kActorMarks[2] = {4u, 44u};
inline constexpr uint32_t kMostObjects = 512u;
inline constexpr int32_t kMostShotStep = 2048;  /* a shot's units, 148 a step measured */
inline constexpr int32_t kMostActorStep = 128;  /* steps of up to 32 measured, another actor taken into the record from 256 */

enum class Kind : uint8_t { shot, actor };

/* A shot's x, z and y are words at +4, +8 and +12, an actor's halfwords at +22, +24 and +26 (0x80021700). */
inline uint32_t coordinate(uint32_t record, Kind kind, int axis) {
    return kind == Kind::shot ? record + 4u + 4u * static_cast<uint32_t>(axis) : record + 22u + 2u * static_cast<uint32_t>(axis);
}

template <class Memory>
int32_t read(const Memory &memory, uint32_t address, Kind kind) {
    return kind == Kind::shot ? static_cast<int32_t>(memory.word(address)) : static_cast<int32_t>(memory.half(address));
}

template <class Memory>
void write(Memory &memory, uint32_t address, Kind kind, int32_t value) {
    if (kind == Kind::shot) memory.write(address, static_cast<uint32_t>(value));
    else memory.write_half(address, static_cast<uint16_t>(value));
}

inline bool record_at(uint32_t address) { return address >= 0x80010000u && address < 0x80200000u && (address & 3u) == 0u; }

/* Every sector's shots, then every sector's actors, kMostObjects at most whatever the lists hold. */
template <class Memory, class Visit>
void each_object(const Memory &memory, Visit &&visit) {
    uint32_t seen = 0u;
    for (uint32_t sector = 0u; sector < kSectorCount; ++sector)
        for (uint32_t at = memory.word(kSectorShots + 4u * sector); record_at(at) && seen < kMostObjects; at = memory.word(at), ++seen)
            visit(at, Kind::shot);
    for (uint32_t sector = 0u; sector < kSectorCount; ++sector)
        for (uint32_t at = memory.word(kSectorActors + 4u * sector); record_at(at) && seen < kMostObjects; at = memory.word(at), ++seen)
            visit(at, Kind::actor);
}

/* Where each object was when the tick began, and the half steps the picture drawn after it holds back. */
struct Steps {
    struct Place {
        uint32_t record;
        Kind kind;
        int32_t at[3];
        uint32_t mark[2];
    };
    struct Held {
        uint32_t address;
        Kind kind;
        int32_t back, left;
    };
    Place began[kMostObjects];
    uint32_t known = 0u;
    bool taken[kMostObjects];
    Held held[3u * kMostObjects];
    uint32_t count = 0u;
};

inline void forget(Steps &steps) {
    steps.known = 0u;
    steps.count = 0u;
}

/* What tells one object from another taking its record over: a shot's life and velocity, an actor's words at +4 and
 * +44 (the same over all 94 steps of the lake capsule, the same in none of the 15 records taken over). */
template <class Memory>
void marks(const Memory &memory, uint32_t record, Kind kind, uint32_t mark[2]) {
    mark[0] = kind == Kind::shot ? memory.half(record + kShotLife) : memory.word(record + kActorMarks[0]);
    mark[1] = memory.word(record + (kind == Kind::shot ? kShotVelocity : kActorMarks[1]));
}

template <class Memory>
void note(const Memory &memory, Steps &steps) {
    steps.known = 0u;
    each_object(memory, [&](uint32_t record, Kind kind) {
        Steps::Place &place = steps.began[steps.known++];
        place.record = record;
        place.kind = kind;
        for (int axis = 0; axis < 3; ++axis) place.at[axis] = read(memory, coordinate(record, kind, axis), kind);
        marks(memory, record, kind, place.mark);
    });
}

/* Gives back what the picture held back. A coordinate that is no longer what was left there stays as it is. */
template <class Memory>
void catch_up(Memory &memory, Steps &steps) {
    for (uint32_t index = 0u; index < steps.count; ++index) {
        const Steps::Held &one = steps.held[index];
        if (read(memory, one.address, one.kind) == one.left) write(memory, one.address, one.kind, one.left + one.back);
    }
    steps.count = 0u;
}

/* Whether the object now in `record` is the one noted at `place`, having taken one step from there. */
template <class Memory>
bool stepped_from(const Memory &memory, const Steps::Place &place, uint32_t record, Kind kind) {
    if (place.kind != kind) return false;
    uint32_t mark[2];
    marks(memory, record, kind, mark);
    const int32_t most = kind == Kind::shot ? kMostShotStep : kMostActorStep;
    for (int axis = 0; axis < 3; ++axis) {
        const int32_t step = read(memory, coordinate(record, kind, axis), kind) - place.at[axis];
        if (step > most || step < -most) return false;
    }
    if (kind == Kind::actor) return place.record == record && mark[0] == place.mark[0] && mark[1] == place.mark[1];
    if (mark[0] + 1u != place.mark[0] || mark[1] != place.mark[1]) return false;
    /* A shot goes its velocity >> 5 on x and z each step (148 and -118 for 4737 and -3768 measured). */
    for (int axis = 0; axis < 2; ++axis) {
        const int32_t velocity = static_cast<int16_t>(mark[1] >> (16 * axis));
        const int32_t step = read(memory, coordinate(record, kind, axis), kind) - place.at[axis];
        if (step - (velocity >> 5) > 1 || step - (velocity >> 5) < -1) return false;
    }
    return true;
}

/* The noted place of the object in `record`: its own record's, or for a shot the game moved into another record, the
 * one noted shot that steps there (the near shot of capsule 1792608893 went from 0x800BE0B8 to 0x800BE070). None when
 * two could. */
template <class Memory>
const Steps::Place *noted(const Memory &memory, const Steps &steps, uint32_t record, Kind kind, uint32_t hint) {
    const auto own = [&](uint32_t index) {
        return index < steps.known && !steps.taken[index] && steps.began[index].record == record &&
               stepped_from(memory, steps.began[index], record, kind);
    };
    if (own(hint)) return &steps.began[hint];
    for (uint32_t index = 0u; index < steps.known; ++index) {
        if (steps.began[index].record != record) continue;
        if (own(index)) return &steps.began[index];
        if (kind == Kind::actor) return nullptr;
    }
    const Steps::Place *found = nullptr;
    for (uint32_t index = 0u; kind == Kind::shot && index < steps.known; ++index) {
        if (steps.taken[index] || !stepped_from(memory, steps.began[index], record, kind)) continue;
        if (found) return nullptr;
        found = &steps.began[index];
    }
    return found;
}

/* An object that stepped in the tick just run is drawn half its step back. */
template <class Memory>
void lag(Memory &memory, Steps &steps) {
    if (steps.count != 0u) return;
    for (uint32_t index = 0u; index < steps.known; ++index) steps.taken[index] = false;
    uint32_t next = 0u;
    each_object(memory, [&](uint32_t record, Kind kind) {
        const Steps::Place *was = noted(memory, steps, record, kind, next);
        if (!was) return;
        const uint32_t index = static_cast<uint32_t>(was - steps.began);
        steps.taken[index] = true;
        next = index + 1u;
        for (int axis = 0; axis < 3; ++axis) {
            const uint32_t address = coordinate(record, kind, axis);
            const int32_t back = (read(memory, address, kind) - was->at[axis]) / 2;
            if (back == 0) continue;
            write(memory, address, kind, read(memory, address, kind) - back);
            steps.held[steps.count++] = {address, kind, back, read(memory, address, kind)};
        }
    });
}

} // namespace disruptor::objects
