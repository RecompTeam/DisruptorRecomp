#include "game/render/misplaced_riser.h"

#include <cstdint>
#include <iostream>
#include <map>

namespace {

using namespace disruptor::misplaced_riser;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Words {
    std::map<uint32_t, uint32_t> at;
    uint32_t word(uint32_t address) const {
        const auto found = at.find(address);
        return found == at.end() ? 0u : found->second;
    }
    void write(uint32_t address, uint32_t value) { at[address] = value; }
};

constexpr uint32_t kRoom = 0x80190000u;
constexpr uint32_t kVertexOffset = 0x40u;
constexpr uint32_t kPartSize = 0x1000u;

/* A part of `size` bytes whose vertex list starts kVertexOffset after its first eight bytes, faces after 200 vertices. */
uint32_t add_part(Words &words, uint32_t part, bool stairs) {
    words.write(part, kPartSize << 8 | 0xFFu);
    words.write(part + 4u, kVertexOffset);
    const uint32_t vertices = part + 8u + kVertexOffset;
    for (uint32_t k = 0; k < 200u; k++) words.write(vertices + 4u * k, packed_vertex(512u, 64u * k, 0u) | 1u << 30);
    if (stairs)
        for (uint32_t k = 0; k < kFootAcross.size(); k++)
            words.write(vertices + 4u * (kFirstFootVertex + k), packed_vertex(kFootHeight, kShippedAlong, kFootAcross[k]));
    words.write(vertices + 4u * 200u, 0x11223344u);
    words.write(vertices + 4u * 201u, 0x55667788u);
    for (uint32_t k = 0; stairs && k < kFaces.size(); k++) {
        const uint32_t face = part + kPartSize - 8u - 12u * k;
        words.write(face, kFaces[k].first);
        words.write(face + 4u, kFaces[k].second);
    }
    return part + kPartSize;
}

Words staircase_room() {
    Words words;
    const uint32_t second = add_part(words, kRoom, false);
    const uint32_t end = add_part(words, second, true);
    words.write(end, kLastPart);
    return words;
}

uint32_t foot(const Words &words, uint32_t part, uint32_t k) {
    return words.word(part + 8u + kVertexOffset + 4u * (kFirstFootVertex + k));
}

void test_a_foot_vertex_packs_as_the_game_does() {
    expect(packed_vertex(kFootHeight, kShippedAlong, 2048u) == 0x0803D040u, "height, along and across are 10-bit fields of 16 units");
}

void test_the_riser_moves_to_the_landing_edge() {
    Words words = staircase_room();
    const Words shipped = words;
    expect(fixed_parts(words, kRoom) == 1u, "the staircase's part is fixed, the other is not");
    const uint32_t part = kRoom + kPartSize;
    bool feet = true;
    for (uint32_t k = 0; k < kFootAcross.size(); k++)
        feet &= foot(words, part, k) == packed_vertex(kFootHeight, kLandingAlong, kFootAcross[k]);
    expect(feet, "the riser's five foot vertices stand at the landing's edge");
    uint32_t changed = 0;
    for (const auto &[address, value] : words.at) changed += shipped.word(address) != value;
    expect(changed == kFootAcross.size() + 2u * kFaces.size(), "nothing but the feet and the four faces changes");
    for (const auto &[address, value] : words.at) {
        for (const Face &face : kFaces) {
            if (shipped.word(address) == face.first) {
                expect(value >> 22 == face.top_first, "a face's first top corner is the landing edge's record");
                expect((value & 0x003FFFFFu) == (face.first & 0x003FFFFFu), "and the rest of its first word stays");
            }
            if (shipped.word(address) == face.second) {
                expect((value >> 12 & 1023u) == face.top_last, "its last top corner is the landing edge's record");
                expect((value & ~(1023u << 12)) == (face.second & ~(1023u << 12)), "and the rest of its second word stays");
                expect(value >> 22 == face.second >> 22, "its bottom corners keep their records");
            }
        }
    }
}

void test_a_fixed_staircase_is_left_alone() {
    Words words = staircase_room();
    fixed_parts(words, kRoom);
    const Words once = words;
    expect(fixed_parts(words, kRoom) == 0u && words.at == once.at, "a second pass finds nothing to fix");
}

void test_anything_else_is_left_alone() {
    Words other_foot = staircase_room();
    other_foot.write(kRoom + kPartSize + 8u + kVertexOffset + 4u * (kFirstFootVertex + 2u), 0u);
    const Words before_foot = other_foot;
    expect(fixed_parts(other_foot, kRoom) == 0u && other_foot.at == before_foot.at, "a different foot vertex changes nothing");
    Words missing_face = staircase_room();
    for (auto &[address, value] : missing_face.at)
        if (value == kFaces[3].second) value ^= 1u;
    const Words before_face = missing_face;
    expect(fixed_parts(missing_face, kRoom) == 0u && missing_face.at == before_face.at,
           "without all four faces nothing is written, not even the feet");
}

void test_feet_outside_the_part_are_left_alone() {
    Words words = staircase_room();
    const uint32_t part = kRoom + kPartSize;
    const uint32_t outside = kPartSize - 8u - 4u * kFirstFootVertex;
    words.write(part + 4u, outside);
    for (uint32_t k = 0; k < kFootAcross.size(); k++)
        words.write(part + 8u + outside + 4u * (kFirstFootVertex + k), packed_vertex(kFootHeight, kShippedAlong, kFootAcross[k]));
    const Words before = words;
    expect(fixed_parts(words, kRoom) == 0u && words.at == before.at, "a vertex list running past its part is not written");
}

void test_the_walk_stops_where_the_renderer_does() {
    Words words = staircase_room();
    words.write(kRoom + kPartSize, kLastPart | kPartSize << 8);
    expect(fixed_parts(words, kRoom) == 0u, "the walk ends at a part marked last, the staircase's own mark too");
    Words empty;
    expect(fixed_parts(empty, kRoom) == 0u, "and at a part of no size");
}

}  // namespace

int main() {
    test_a_foot_vertex_packs_as_the_game_does();
    test_the_riser_moves_to_the_landing_edge();
    test_a_fixed_staircase_is_left_alone();
    test_anything_else_is_left_alone();
    test_feet_outside_the_part_are_left_alone();
    test_the_walk_stops_where_the_renderer_does();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "misplaced_riser: PASS\n";
    return 0;
}
