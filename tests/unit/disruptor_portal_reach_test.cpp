#include "game/render/portal_reach.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

using namespace disruptor::portal_reach;
namespace sector_reach = disruptor::sector_reach;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

double degrees_between(const Wedge &wedge) {
    const double left = std::atan2(wedge.left.z, wedge.left.x), right = std::atan2(wedge.right.z, wedge.right.x);
    double angle = (left - right) * 180.0 / std::acos(-1.0);
    if (angle < 0.0) angle += 360.0;
    return angle;
}

double length(const Ray &ray) { return std::hypot(ray.x, ray.z); }

void test_a_span_at_the_frame_edge_reaches_past_it() {
    expect(turn_guarded_left(0u) == static_cast<uint32_t>(-kTurnGuard) && turn_guarded_left(1u) == 1u &&
               turn_guarded_left(160u) == 160u,
           "only a left end at the frame's edge reaches past it");
    expect(turn_guarded_right(kFrameEdge) == kFrameEdge + 8u && turn_guarded_right(319u) == 319u &&
               turn_guarded_right(321u) == 321u,
           "only a right end at the frame's edge reaches past it");
    const double half_step = std::acos(-1.0) / 256.0;
    for (const double focal : {160.0, 120.0, 91.43, 60.0})
        expect(focal * std::tan(std::atan(160.0 / focal) + half_step) - 160.0 < kTurnGuard,
               "the guard covers half a heading step at every aspect");
}

constexpr Wedge kScreen{{-1448, 1448}, {1448, 1448}};  /* the walk's first wedge, 45 degrees each side of +z */

void test_widened() {
    const Wedge wider = widened(kScreen, kWedgeSlack);
    expect(std::abs(degrees_between(kScreen) - 90.0) < 0.1, "the first wedge spans 90 degrees");
    expect(std::abs(degrees_between(wider) - 102.0) < 0.1, "each ray turns 6 degrees outward");
    expect(std::abs(length(wider.left) - length(kScreen.left)) <= 1.0 &&
               std::abs(length(wider.right) - length(kScreen.right)) <= 1.0,
           "a turned ray keeps its length");
    expect(wider.left.x == -wider.right.x && wider.left.z == wider.right.z, "a wedge about +z stays symmetric");

    const Wedge narrow{{-100, 1000}, {-60, 1000}};
    const Wedge opened = widened(narrow, kWedgeSlack);
    expect(std::abs(degrees_between(opened) - degrees_between(narrow) - 12.0) < 0.2, "a narrow wedge opens by 12 degrees");
    expect(turn(opened) < 0, "the turned wedge keeps its sense");
}

void test_refused() {
    const Wedge reversed{kScreen.right, kScreen.left};
    const Wedge same = widened(reversed, kWedgeSlack);
    expect(same.left.x == reversed.left.x && same.right.z == reversed.right.z, "a wedge of the other sense is left alone");
    const Wedge sliver{{-60, 1000}, {-100, 1000}};
    const Wedge kept_sliver = widened(sliver, kWedgeSlack);
    expect(kept_sliver.left.x == sliver.left.x && kept_sliver.right.x == sliver.right.x,
           "a narrow wedge of the other sense is not turned over into this one");

    const Wedge large{{-kLargestRay, 100}, {100, 100}};
    const Wedge kept = widened(large, kWedgeSlack);
    expect(kept.left.x == large.left.x && kept.left.z == large.left.z, "a ray past the largest the walk builds is left alone");

    const Wedge wide{{-1000, 10}, {1000, 10}};
    const Wedge whole = widened(wide, kWedgeSlack);
    expect(whole.left.x == wide.left.x && whole.right.x == wide.right.x, "a wedge the turn would take past a half turn is left alone");

    const Wedge flat{{0, 0}, {0, 0}};
    expect(widened(flat, kWedgeSlack).left.x == 0, "an empty wedge is left alone");
}

struct Memory {
    std::unordered_map<uint32_t, uint32_t> words;
    uint32_t read(uint32_t address) const { return words.contains(address) ? words.at(address) : 0u; }
};

constexpr uint32_t kList = 0x80079D18u;

void test_spans() {
    Memory memory;
    memory.words[kList] = (100u << 16) | 200u;
    memory.words[kList + 8u] = (0u << 16) | 320u;
    memory.words[kList + 16u] = (250u << 16) | 320u;
    memory.words[kList + 24u] = 0xDEADBEEFu;
    const auto read = [&memory](uint32_t address) { return memory.read(address); };
    const auto write = [&memory](uint32_t address, uint32_t word) { memory.words[address] = word; };

    Spans spans;
    expect(!spans.open(), "nothing is open at first");
    spans.open(kList, kList + 24u, 8u, read, write);
    expect(spans.open(), "three spans are open");
    expect(memory.read(kList) == kFullSpan && memory.read(kList + 16u) == kFullSpan, "every listed span reaches 0 to 320");
    expect(memory.read(kList + 24u) == 0xDEADBEEFu, "the word past the list's end is not touched");

    spans.open(kList, kList + 24u, 8u, read, write);
    memory.words[kList + 16u] = (10u << 16) | 20u;
    spans.close(read, write);
    expect(memory.read(kList) == ((100u << 16) | 200u), "a span is put back");
    expect(memory.read(kList + 8u) == kFullSpan, "a span that was whole stays whole");
    expect(memory.read(kList + 16u) == ((10u << 16) | 20u), "a span the game changed meanwhile is its own");
    expect(!spans.open(), "after closing nothing is open");

    spans.open(kList, kList + 8u, 8u, read, write);
    spans.forget();
    spans.close(read, write);
    expect(memory.read(kList) == kFullSpan, "after a state load nothing is put back");

    spans.open(kList + 8u, kList, 8u, read, write);
    expect(!spans.open(), "an end before the list opens nothing");

    Spans many;
    many.open(kList, kList + 8u * (kMostRooms + 4u), 8u, read, write);
    expect(memory.read(kList + 8u * kMostRooms) == 0u, "no more spans open than there are rooms");
}

constexpr View kAhead{256, 0}; /* the camera looks along +z */
constexpr Wedge kInner{{-500, 1448}, {500, 1448}};
constexpr Wedge kMargin{{1500, 1448}, {1726, 1448}}; /* 46 to 50 degrees right of +z, inside the wider wedge only */

int same_column(int x) { return x; }
int squashed(int x) { return 160 + (x - 160) * 3 / 4; }

bool equal(const Wedge &one, const Wedge &other) {
    return one.left.x == other.left.x && one.left.z == other.left.z && one.right.x == other.right.x &&
           one.right.z == other.right.z;
}

void test_overlap() {
    expect(equal(overlap(kInner, kScreen), kInner), "a wedge inside another is all of their overlap");
    const Wedge partial = overlap({{-500, 1448}, {2000, 1448}}, kScreen);
    expect(partial.left.x == -500 && partial.right.x == 1448, "a ray past the other wedge gives way to its ray");
    expect(turn(overlap(kMargin, kScreen)) > 0, "wedges side by side overlap in nothing");
    expect(turn(overlap({{1448, 1448}, {1726, 1448}}, kScreen)) == 0, "wedges that touch overlap in a ray, as the game's tests take it");
}

void test_facing() {
    const sector_reach::Portal portal{100, 50, 300, 150};
    expect(facing(portal, -100, -50) == 0, "a camera on the portal's line sees it edge-on");
    expect(facing(portal, 0, 10) == -2000, "elsewhere the cross of its ends from the camera, as the walk takes it");
}

void test_listed() {
    expect(listed(kScreen, kAhead, same_column), "the first wedge lists its room");
    expect(!listed(kMargin, kAhead, same_column), "a wedge wholly right of the screen does not");
    expect(!listed({{-1726, 1448}, {-1500, 1448}}, kAhead, same_column), "nor one wholly left of it");
    expect(listed({{-300, 100}, {-200, 100}}, kAhead, same_column), "a wedge just left of the screen near the eye still does");
    expect(listed({{200, 100}, {300, 100}}, kAhead, same_column), "near the eye a wedge reaches 8 columns further in");
    expect(!listed({{200, 128}, {300, 128}}, kAhead, same_column), "from 128 ahead it does not");
    expect(listed(kMargin, kAhead, squashed), "the wide picture's squash brings a wedge past 4:3 onto the screen");
    expect(column({0, 1000}, kAhead, 160, same_column).x == 160 && column({0, -5}, kAhead, 160, same_column).ahead == 1,
           "a ray ahead lands mid-screen, one behind counts as just ahead");
}

const auto kNever = [](const Wedge &) { return false; };
const auto kLists = [](const Wedge &own) { return listed(own, kAhead, same_column); };

void test_sight() {
    Sight sight;
    sight.clear();
    expect(sight.seen(9u) && !sight.judged(), "before a walk every room is seen");

    const Visit open{};
    expect(sight.enter(0u, 1u, kScreen, open, kLists) && sight.seen(1u) && sight.judged(), "the first room is seen");
    expect(sight.enter(1u, 2u, kInner, open, kLists) && sight.seen(2u), "a room through a portal on the screen is seen");
    expect(!sight.enter(1u, 3u, kMargin, open, kLists) && !sight.seen(3u), "a room only the wider wedge reaches is not");
    expect(!sight.enter(2u, 4u, kInner, open, kLists) && !sight.seen(4u), "nor anything past it");

    Visit unloaded;
    unloaded.loaded = false;
    expect(sight.enter(1u, 5u, kInner, unloaded, kLists) && !sight.seen(5u), "the game enters a room out of memory and lists nothing");
    expect(!sight.enter(2u, 6u, kInner, open, kLists) && !sight.seen(6u), "and walks no further from it");

    expect(sight.enter(1u, 7u, {{500, 1448}, {-500, 1448}}, open, kLists) && sight.seen(7u), "a wedge of the other sense is not judged");
    expect(sight.enter(2u, 8u, kMargin, open, kLists) && sight.seen(8u), "nor what lies past it");

    Visit companions;
    companions.companions = {10u, 11u};
    sight.enter(1u, 9u, kInner, companions, kLists);
    expect(sight.seen(10u) && sight.seen(11u), "a room kept lists its companions");
    Visit refused = companions;
    refused.companions = {30u, 31u};
    sight.enter(1u, 29u, kMargin, refused, kLists);
    expect(!sight.seen(29u) && !sight.seen(30u), "a room refused lists none");

    Visit door = companions;
    door.door_open = false;
    door.companions = {13u, sector_reach::kNoSector};
    sight.enter(1u, 12u, kInner, door, kLists);
    expect(sight.seen(12u) && !sight.seen(13u), "a closed door keeps its room and stops the companions");
    expect(!sight.enter(2u, 14u, kInner, open, kLists) && !sight.seen(14u), "and the walk past it");

    sight.enter(1u, 2u, kInner, open, kNever);
    expect(sight.enter(2u, 15u, kInner, open, kLists) && sight.seen(15u), "a room listed before is not tested again");
    sight.enter(1u, 16u, kInner, open, kNever);
    expect(!sight.seen(16u), "a new room the test refuses is not seen");

    expect(sight.enter(sector_reach::kDepthCap, 17u, kMargin, open, kLists) && !sight.seen(17u), "past the depth cap nothing is judged");
    expect(sight.enter(1u, 18u, {{1448, 1448}, {1726, 1448}}, open, [](const Wedge &) { return true; }),
           "a portal touching the wedge's edge is walked through, as the game's tests take it");
    const uint8_t expected[] = {1u, 2u, 7u, 8u, 9u, 10u, 11u, 12u, 15u, 18u};
    bool ordered = sight.order_count() == sizeof expected;
    for (uint32_t index = 0; ordered && index < sight.order_count(); ++index) ordered = sight.order()[index] == expected[index];
    expect(ordered, "rooms are listed in the order the game's walk lists them");

    sight.enter(1u, kMostRooms, kInner, open, kLists);
    expect(!sight.judged() && sight.seen(3u), "a room the walk enters unread leaves every room seen");
    sight.enter(0u, 1u, kScreen, open, kLists);
    expect(sight.judged() && !sight.seen(3u) && sight.order_count() == 1u, "the next walk starts over");
    expect(sight.seen(sector_reach::kNoSector), "the list's empty room is always kept");
}

struct Bytes {
    std::unordered_map<uint32_t, uint8_t> bytes;
    uint32_t byte(uint32_t address) const { return bytes.contains(address) ? bytes.at(address) : 0u; }
    uint32_t half(uint32_t address) const { return byte(address) | byte(address + 1u) << 8; }
    uint32_t word(uint32_t address) const { return half(address) | half(address + 2u) << 16; }
    void set_byte(uint32_t address, uint32_t value) { bytes[address] = static_cast<uint8_t>(value); }
    void set_word(uint32_t address, uint32_t value) {
        for (uint32_t index = 0; index < 4u; ++index) set_byte(address + index, value >> (8u * index));
    }
};

void test_visit_and_view() {
    Bytes memory;
    constexpr uint32_t kSector = 0x80100000u, kDoorObject = 0x80120000u, kObjectList = 0x80110000u;
    memory.set_word(sector_reach::kSectors + 4u * 3u, kSector);
    memory.set_word(sector_reach::kSectors + 4u * 4u, 0x80100100u);
    memory.set_byte(kSector + kSectorDoor, sector_reach::kNoSector);
    memory.set_byte(kSector + kSectorCompanions, 4u);
    memory.set_byte(kSector + kSectorCompanions + 1u, 5u);
    Visit visit = visit_at(memory, 3u);
    expect(visit.loaded && visit.door_open && visit.companions[0] == 4u && visit.companions[1] == sector_reach::kNoSector,
           "a companion out of memory is not listed");
    memory.set_byte(kSector + kSectorCompanions, sector_reach::kNoSector);
    memory.set_word(sector_reach::kSectors + 4u * 5u, 0x80100200u);
    visit = visit_at(memory, 3u);
    expect(visit.companions[0] == sector_reach::kNoSector && visit.companions[1] == sector_reach::kNoSector,
           "without a first companion the second is not looked at");
    memory.set_byte(kSector + kSectorCompanions, 4u);
    memory.set_word(sector_reach::kSectors + 4u * 4u, 0u);
    visit = visit_at(memory, 3u);
    expect(visit.companions[0] == sector_reach::kNoSector && visit.companions[1] == 5u,
           "a first companion out of memory is passed over for the second");

    memory.set_byte(kSector + kSectorDoor, 2u);
    memory.set_word(kObjects, kObjectList);
    memory.set_word(kObjectList + 8u, kDoorObject);
    expect(!visit_at(memory, 3u).door_open, "a door whose bytes 1 and 5 are both 0 is closed");
    memory.set_byte(kDoorObject + 5u, 1u);
    expect(visit_at(memory, 3u).door_open, "either byte set opens it");
    expect(!visit_at(memory, 6u).loaded, "a room without a sector is out of memory");

    memory.set_byte(kHeading, 7u);
    memory.set_word(kCosines + 14u, 0xFF1Eu);
    memory.set_word(kSines + 14u, 0x0079u);
    const View view = view_at(memory);
    expect(view.cosine == -226 && view.sine == 121, "the view's halfwords are signed");
}

void test_objects() {
    Memory memory;
    constexpr uint32_t kRoom = 3u, kSector = 0x80100000u;
    memory.words[kObjectTables[0] + 4u * kRoom] = 0xA;
    memory.words[kObjectTables[2] + 4u * kRoom] = 0xC;
    memory.words[sector_reach::kSectors + 4u * kRoom] = kSector;
    memory.words[kSector + kSectorObjects] = 0xD;
    const auto read = [&memory](uint32_t address) { return memory.read(address); };
    std::vector<uint32_t> written;
    const auto write = [&](uint32_t address, uint32_t word) {
        written.push_back(address);
        memory.words[address] = word;
    };

    Objects objects;
    objects.hide(kRoom, read, write);
    expect(objects.hidden() && written.size() == 3u, "only the heads that hold a list are emptied");
    expect(memory.read(kObjectTables[0] + 4u * kRoom) == 0u && memory.read(kObjectTables[2] + 4u * kRoom) == 0u &&
               memory.read(kSector + kSectorObjects) == 0u,
           "every list of the room is empty while hidden");
    objects.show(read, write);
    expect(!objects.hidden() && memory.read(kObjectTables[0] + 4u * kRoom) == 0xA && memory.read(kSector + kSectorObjects) == 0xD,
           "showing gives every head back");

    objects.hide(kRoom, read, write);
    memory.words[kObjectTables[2] + 4u * kRoom] = 0x99;
    objects.show(read, write);
    expect(memory.read(kObjectTables[2] + 4u * kRoom) == 0x99 && memory.read(kObjectTables[0] + 4u * kRoom) == 0xA,
           "a head the game set meanwhile stays");

    objects.hide(kRoom, read, write);
    objects.forget();
    objects.show(read, write);
    expect(memory.read(kObjectTables[0] + 4u * kRoom) == 0u, "after a state load nothing is given back");

    memory.words[sector_reach::kSectors + 4u * 4u] = 0x1234u;
    memory.words[0x1234u + kSectorObjects] = 0xE;
    objects.hide(4u, read, write);
    expect(memory.read(0x1234u + kSectorObjects) == 0xE, "a sector word that is no address is not followed");
}

void test_keep() {
    Sight sight;
    const Visit open{};
    sight.enter(0u, 1u, kScreen, open, kLists);
    sight.enter(1u, 2u, kInner, open, kLists);
    sight.enter(1u, 3u, kMargin, open, kLists);
    sight.enter(1u, 4u, kInner, open, kLists);

    Memory memory;
    const uint32_t rooms[] = {1u, 3u, 4u, 2u, sector_reach::kNoSector};
    for (uint32_t index = 0; index < 5u; ++index) {
        memory.words[kList + 8u * index] = 0x100u + index;
        memory.words[kList + 8u * index + kRoomByte] = 0xAB00u | rooms[index];
    }
    memory.words[kList + 40u] = 0xDEADBEEFu;
    const auto read = [&memory](uint32_t address) { return memory.read(address); };
    const auto write = [&memory](uint32_t address, uint32_t word) { memory.words[address] = word; };

    expect(keep(kList, kList + 40u, 8u, sight, read, write) == kList + 32u, "the room only the wider walk reached leaves the list");
    expect(memory.read(kList + kRoomByte) == 0xAB01u && memory.read(kList + 8u + kRoomByte) == 0xAB02u &&
               memory.read(kList + 16u + kRoomByte) == 0xAB04u && memory.read(kList + 24u + kRoomByte) == 0xABFFu,
           "the rest stand in the game's order, an empty entry last");
    expect(memory.read(kList) == 0x100u && memory.read(kList + 8u) == 0x103u && memory.read(kList + 16u) == 0x102u,
           "each entry keeps its span");
    expect(memory.read(kList + 40u) == 0xDEADBEEFu, "nothing past the old end is written");

    expect(keep(kList, kList + 8u * (kMostRooms + 1u), 8u, sight, read, write) == kList + 8u * (kMostRooms + 1u),
           "a list longer than the rooms there are is left alone");
    Sight unjudged;
    unjudged.clear();
    memory.words[kList + kRoomByte] = 3u;
    expect(keep(kList, kList + 8u, 8u, unjudged, read, write) == kList + 8u && memory.read(kList + kRoomByte) == 3u,
           "without a judged walk the list is the game's");
    Sight blind;
    blind.enter(0u, 1u, kScreen, open, kLists);
    blind.enter(1u, 2u, kInner, open, kLists);
    blind.enter(1u, kMostRooms, kInner, open, kLists);
    memory.words[kList + kRoomByte] = 2u;
    memory.words[kList + 8u + kRoomByte] = 1u;
    keep(kList, kList + 16u, 8u, blind, read, write);
    expect(memory.read(kList + kRoomByte) == 2u, "nor after a room the walk entered unread");
}

}  // namespace

int main() {
    test_a_span_at_the_frame_edge_reaches_past_it();
    test_widened();
    test_refused();
    test_spans();
    test_overlap();
    test_facing();
    test_listed();
    test_sight();
    test_visit_and_view();
    test_objects();
    test_keep();
    if (g_failures != 0) return 1;
    std::cout << "portal reach: PASS\n";
    return 0;
}
