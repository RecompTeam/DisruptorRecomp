#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>

#include "game/render/sector_reach.h"

/* The portal walk (0x8003A0F0) keeps a room only where the wedge through its portals reaches the screen, and the visible
 * loop (0x80041114) drops a room's polygon lying wholly beside that wedge's span. A ceiling seen over a wall is neither:
 * it shows the frame's clear colour. The rooms' spans are opened to the whole screen for the visible loop and the walk's
 * wedge is turned a little outward at every portal. A room only the wider walk reaches keeps its mesh and nothing else:
 * its objects are not drawn and the list the game reads afterwards holds the rooms of its own walk. */
namespace disruptor::portal_reach {

inline constexpr uint32_t kFullSpan = 320u;    /* the word of a span from 0 to 320: the right bound low, the left high */
inline constexpr uint32_t kMostRooms = 256u;   /* a room is a byte and the walk lists it once */
inline constexpr double kWedgeSlack = 6.0;     /* degrees: 3 left a corner open in one view of the hall, 6 closed it */
inline constexpr int32_t kLargestRay = 16384;  /* the walk's rays reached 1934 over three capsules */
inline constexpr uint32_t kFrameEdge = 320u;
inline constexpr int32_t kTurnGuard = 8;  /* squashed pixels a half heading step brings in at the frame's edge: 4.0 at 4:3, 6.2 at 32:9 */

/* The mesh renderer drops a polygon wholly beside its sector's span, and a part-step turn of smooth mouse aim carries
 * what lies just past the frame's edge into view: an end of the span at the frame's edge reaches past it. */
inline uint32_t turn_guarded_left(uint32_t left) { return left == 0u ? static_cast<uint32_t>(-kTurnGuard) : left; }
inline uint32_t turn_guarded_right(uint32_t right) {
    return right == kFrameEdge ? kFrameEdge + static_cast<uint32_t>(kTurnGuard) : right;
}
inline constexpr uint32_t kRoomByte = 4u;      /* of a visible list entry, the span word before it */
/* The heads the visible loop's object passes walk for a room: 0x8003BBE0, 0x8003C000, 0x8003CB38, a word a room each. */
inline constexpr std::array<uint32_t, 3> kObjectTables{0x80074024u, 0x80077BF4u, 0x80078004u};
inline constexpr uint32_t kSectorObjects = 8u; /* 0x8003B78C walks the list at this word of the room's sector */

struct Ray {
    int32_t x, z;
};

/* The walk's two rays as it passes them in $a0 to $a3: the left one turns from the right one the negative way. */
struct Wedge {
    Ray left, right;
};

constexpr int64_t turn(const Wedge &wedge) {
    return int64_t{wedge.left.x} * wedge.right.z - int64_t{wedge.left.z} * wedge.right.x;
}

inline Ray rotated(const Ray &ray, double radians) {
    const double cosine = std::cos(radians), sine = std::sin(radians);
    return {static_cast<int32_t>(std::lround(ray.x * cosine - ray.z * sine)),
            static_cast<int32_t>(std::lround(ray.x * sine + ray.z * cosine))};
}

constexpr bool usable(const Ray &ray) {
    return ray.x > -kLargestRay && ray.x < kLargestRay && ray.z > -kLargestRay && ray.z < kLargestRay;
}

/* The wedge with each ray turned `degrees` outward, or the same wedge when it is not one the walk builds. */
inline Wedge widened(const Wedge &wedge, double degrees) {
    if (turn(wedge) >= 0 || !usable(wedge.left) || !usable(wedge.right)) return wedge;
    const double radians = degrees * std::acos(-1.0) / 180.0;
    const Wedge wider{rotated(wedge.left, radians), rotated(wedge.right, -radians)};
    return turn(wider) < 0 ? wider : wedge;
}

constexpr bool ram_word(uint32_t address) { return (address & 0xFFE00003u) == 0x80000000u; }

constexpr int64_t cross(const Ray &one, const Ray &other) {
    return int64_t{one.x} * other.z - int64_t{one.z} * other.x;
}

/* The rays two wedges both hold, as the walk narrows its wedge to a portal (0x8003ABD0): turned the wrong way when none. */
constexpr Wedge overlap(const Wedge &one, const Wedge &other) {
    return {cross(one.left, other.left) >= 0 ? one.left : other.left,
            cross(one.right, other.right) <= 0 ? one.right : other.right};
}

inline constexpr uint32_t kHeading = 0x80077624u; /* a byte, the index of the two tables of signed halfwords */
inline constexpr uint32_t kCosines = 0x80057798u;
inline constexpr uint32_t kSines = 0x80057998u;

struct View {
    int32_t cosine, sine;
};

template <class Memory>
View view_at(const Memory &memory) {
    const uint32_t heading = memory.byte(kHeading);
    return {static_cast<int16_t>(memory.half(kCosines + 2u * heading)), static_cast<int16_t>(memory.half(kSines + 2u * heading))};
}

struct Column {
    int32_t x, ahead;
};

/* A ray on the screen as the walk puts it before the nudge near the eye, `centre` 160 left and 162 right (0x8003A4BC). */
template <typename Project>
Column column(const Ray &ray, const View &view, int32_t centre, Project project) {
    using sector_reach::difference, sector_reach::low, sector_reach::sum;
    const int32_t across = difference(low(view.cosine, ray.x), low(view.sine, ray.z)) >> 8;
    const int32_t ahead = std::max(sum(low(view.sine, ray.x), low(view.cosine, ray.z)) >> 8, 1);
    return {std::clamp(project(sum(low(across, 160) / ahead, centre)), 0, static_cast<int32_t>(kFullSpan)), ahead};
}

/* Whether the walk lists a room it meets first with this wedge: not when the wedge lies wholly beside the screen (0x8003A69C). */
template <typename Project>
bool listed(const Wedge &wedge, const View &view, Project project) {
    constexpr int32_t kFull = static_cast<int32_t>(kFullSpan);
    Column left = column(wedge.left, view, 160, project), right = column(wedge.right, view, 162, project);
    if (left.ahead < 128) left.x = std::max(left.x - 8, 0);
    if (right.ahead < 128) right.x = std::min(right.x + 8, kFull);
    return left.x < kFull && right.x > 0; /* its exception under 9 ahead is inside the nudge's 128 */
}

inline constexpr uint32_t kListedAt = 0x80076E64u; /* a byte a room: its entry in the visible list, 255 while unlisted */

/* The cross of a portal's ends seen from the camera, as the walk takes it at 0x8003ABF4: 0 when the camera stands on its line. */
constexpr int32_t facing(const sector_reach::Portal &portal, int32_t x, int32_t z) {
    using sector_reach::difference, sector_reach::low;
    return difference(low(difference(portal.second_x, x), difference(portal.first_z, z)),
                      low(difference(portal.second_z, z), difference(portal.first_x, x)));
}

inline constexpr uint32_t kObjects = 0x80071424u; /* $gp + 0x2D8, a word an object */
inline constexpr uint32_t kSectorDoor = 41u;      /* an object that ends the walk while both its bytes 1 and 5 are 0 */
inline constexpr uint32_t kSectorCompanions = 44u; /* two rooms the walk lists whole with a room it keeps (0x8003A748) */

/* What the walk finds in a room's sector once it keeps the room. */
struct Visit {
    bool loaded = true;
    bool door_open = true;
    std::array<uint32_t, 2> companions{sector_reach::kNoSector, sector_reach::kNoSector};
};

template <class Memory>
Visit visit_at(const Memory &memory, uint32_t room) {
    Visit visit;
    const auto loaded = [&memory](uint32_t other) { return memory.word(sector_reach::kSectors + 4u * other) != 0u; };
    const uint32_t sector = memory.word(sector_reach::kSectors + 4u * room);
    visit.loaded = sector != 0u;
    if (!ram_word(sector)) return visit;
    if (const uint32_t door = memory.byte(sector + kSectorDoor); door != sector_reach::kNoSector) {
        const uint32_t object = memory.word(memory.word(kObjects) + 4u * door);
        visit.door_open = !ram_word(object) || memory.byte(object + 1u) != 0u || memory.byte(object + 5u) != 0u;
    }
    const uint32_t first = memory.byte(sector + kSectorCompanions), second = memory.byte(sector + kSectorCompanions + 1u);
    if (first == sector_reach::kNoSector) return visit;
    if (loaded(first)) visit.companions[0] = first;
    if (second != sector_reach::kNoSector && loaded(second)) visit.companions[1] = second;
    return visit;
}

/* By depth, the wedge the game's own walk is handed: what the wider walk is handed narrowed by the parent's own. */
class Sight {
public:
    /* Before any walk of a frame every room counts as seen. */
    void clear() {
        reach_.fill(Reach::Unknown);
        walked_ = false;
    }

    /* The walk entering `room` at `depth` with `wedge`, before it is widened. `lists` is the game's test of a new room.
     * Returns whether the game's own walk enters it too. */
    template <typename Lists>
    bool enter(uint32_t depth, uint32_t room, const Wedge &wedge, const Visit &visit, Lists lists) {
        if (depth >= sector_reach::kDepthCap) return true;
        if (depth == 0u) {
            seen_.reset();
            order_count_ = 0u;
            walked_ = true;
            blind_ = false;
        }
        Reach reach = turn(wedge) < 0 ? Reach::Open : Reach::Unknown;
        Wedge own = wedge;
        if (depth != 0u && reach_[depth - 1u] != Reach::Open) {
            reach = reach_[depth - 1u];
        } else if (depth != 0u && reach == Reach::Open) {
            own = overlap(wedge, own_[depth - 1u]);
            if (turn(own) > 0) reach = Reach::Shut;
        }
        const bool entered = reach != Reach::Shut;
        if (room >= kMostRooms && entered) blind_ = true;
        bool kept = false;
        if (entered && room < kMostRooms && room != sector_reach::kNoSector) {
            if (!visit.loaded)
                reach = Reach::Shut;
            else if (reach == Reach::Open && !seen_.test(room) && !lists(own))
                reach = Reach::Shut;
            else
                kept = true;
        }
        if (kept) {
            list(room);
            for (const uint32_t companion : visit.companions)
                if (visit.door_open && companion < kMostRooms && companion != sector_reach::kNoSector) list(companion);
            if (!visit.door_open) reach = Reach::Shut;
        }
        reach_[depth] = reach;
        own_[depth] = own;
        return entered;
    }

    /* A room the walk entered without telling which leaves every room seen. */
    bool seen(uint32_t room) const {
        return !walked_ || blind_ || room >= kMostRooms || room == sector_reach::kNoSector || seen_.test(room);
    }
    /* Whether this frame's walk tells the game's rooms from the wider walk's. */
    bool judged() const { return walked_ && !blind_; }
    /* The rooms in the order the game's walk lists them. */
    const uint8_t *order() const { return order_.data(); }
    uint32_t order_count() const { return order_count_; }

private:
    void list(uint32_t room) {
        if (seen_.test(room)) return;
        seen_.set(room);
        order_[order_count_++] = static_cast<uint8_t>(room);
    }

    enum class Reach : uint8_t { Unknown, Open, Shut };
    std::array<Reach, sector_reach::kDepthCap> reach_{};
    std::array<Wedge, sector_reach::kDepthCap> own_{};
    std::bitset<kMostRooms> seen_{};
    std::array<uint8_t, kMostRooms> order_{};
    uint32_t order_count_ = 0u;
    bool walked_ = false;
    bool blind_ = false;
};

/* The object lists of the rooms the game's walk does not reach, emptied while the visible loop draws. */
class Objects {
public:
    template <typename Read, typename Write>
    void hide(uint32_t room, Read read, Write write) {
        for (const uint32_t table : kObjectTables) empty(table + 4u * room, read, write);
        const uint32_t sector = read(sector_reach::kSectors + 4u * room);
        if (ram_word(sector)) empty(sector + kSectorObjects, read, write);
    }

    /* A head the game set meanwhile is its own and stays. */
    template <typename Read, typename Write>
    void show(Read read, Write write) {
        for (uint32_t index = 0; index < count_; ++index)
            if (read(saved_[index].address) == 0u) write(saved_[index].address, saved_[index].word);
        count_ = 0u;
    }

    void forget() { count_ = 0u; }
    bool hidden() const { return count_ != 0u; }

private:
    template <typename Read, typename Write>
    void empty(uint32_t address, Read read, Write write) {
        const uint32_t word = read(address);
        if (word == 0u || count_ == saved_.size()) return;
        saved_[count_++] = {address, word};
        write(address, 0u);
    }

    struct Saved {
        uint32_t address, word;
    };
    std::array<Saved, kMostRooms * (kObjectTables.size() + 1u)> saved_{};
    uint32_t count_ = 0u;
};

/* Rewrites the visible list with the seen rooms' entries, in the order the game's walk lists them: the list's new end. */
template <typename Read, typename Write>
uint32_t keep(uint32_t first, uint32_t end, uint32_t stride, const Sight &sight, Read read, Write write) {
    struct Entry {
        uint32_t span, room;
    };
    if (!sight.judged() || end < first || (end - first) / stride > kMostRooms) return end;
    std::array<Entry, kMostRooms> entries{};
    std::array<bool, kMostRooms> written{};
    uint32_t count = 0u;
    for (uint32_t entry = first; entry < end; entry += stride, ++count)
        entries[count] = {read(entry), read(entry + kRoomByte)};
    std::array<int32_t, kMostRooms> at{};
    at.fill(-1);
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t room = entries[index].room & 0xFFu;
        if (sight.seen(room) && at[room] < 0) at[room] = static_cast<int32_t>(index);
    }
    uint32_t kept = first;
    const auto put = [&](uint32_t index) {
        write(kept, entries[index].span);
        write(kept + kRoomByte, entries[index].room);
        written[index] = true;
        kept += stride;
    };
    for (uint32_t order = 0; order < sight.order_count(); ++order)
        if (const int32_t index = at[sight.order()[order]]; index >= 0 && !written[index]) put(static_cast<uint32_t>(index));
    for (uint32_t index = 0; index < count; ++index)
        if (!written[index] && sight.seen(entries[index].room & 0xFFu)) put(index);
    return kept;
}

/* The visible list's spans while the visible loop draws with them opened: every way a frame ends puts them back. */
class Spans {
public:
    template <typename Read, typename Write>
    void open(uint32_t first, uint32_t end, uint32_t stride, Read read, Write write) {
        if (end < first) return;
        for (uint32_t entry = first; entry < end && count_ < kMostRooms; entry += stride) {
            saved_[count_++] = {entry, read(entry)};
            write(entry, kFullSpan);
        }
    }

    /* A span the game changed since it was opened is its own and stays. */
    template <typename Read, typename Write>
    void close(Read read, Write write) {
        for (uint32_t index = 0; index < count_; ++index)
            if (read(saved_[index].address) == kFullSpan) write(saved_[index].address, saved_[index].word);
        count_ = 0u;
    }

    /* After a state load the list is the loaded one: nothing is put back. */
    void forget() { count_ = 0u; }
    bool open() const { return count_ != 0u; }

private:
    struct Saved {
        uint32_t address, word;
    };
    std::array<Saved, kMostRooms> saved_{};
    uint32_t count_ = 0u;
};

}  // namespace disruptor::portal_reach
