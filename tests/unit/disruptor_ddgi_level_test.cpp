#include "gpu_ddgi.h"
#include "mod_plugins.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
int g_sun = 1, g_here = 1, g_accept = 1, g_calls = 0, g_reads = 0;
std::vector<GpuDdgiTriangle> g_level;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

std::size_t physical(std::uint32_t address) { return static_cast<std::size_t>(address & 0x001FFFFFu); }

void seed_byte(std::uint32_t address, std::uint8_t value) { g_ram[physical(address)] = value; }

void seed_word(std::uint32_t address, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) g_ram[physical(address) + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

}  // namespace

extern "C" {

std::uint8_t psx_mod_read_byte(std::uint32_t address) { return g_ram[physical(address)]; }

std::uint32_t psx_mod_read_word(std::uint32_t address) {
    g_reads += address == 0x800715A0u;
    const std::size_t p = physical(address);
    return static_cast<std::uint32_t>(g_ram[p]) | static_cast<std::uint32_t>(g_ram[p + 1]) << 8 |
           static_cast<std::uint32_t>(g_ram[p + 2]) << 16 | static_cast<std::uint32_t>(g_ram[p + 3]) << 24;
}

int gl_renderer_ddgi_sun(void) { return g_sun; }
int gl_renderer_ddgi_sun_here(void) { return g_here; }

int gpu_ddgi_set_level(const GpuDdgiTriangle *tris, int count) {
    ++g_calls;
    g_level.assign(tris, tris + count);
    return g_accept;
}

}  // extern "C"

#include "../../src/game/render/ddgi_level.cpp"

namespace {

constexpr std::uint32_t kTable = 0x80120000u;

// One mesh chunk: corners by their 10-bit steps from the room's origin, then quads by four corner indices and a texture.
struct Chunk {
    std::vector<std::uint32_t> corners;
    std::vector<std::array<std::uint32_t, 5>> quads; /* corner indices A B C D and the texture */
};

std::uint32_t corner(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t copies = 0) {
    return copies << 30 | a << 20 | b << 10 | c;
}

// Writes a mesh at the address, returns the address past its end mark.
std::uint32_t write_mesh(std::uint32_t at, const std::vector<Chunk> &chunks) {
    for (const Chunk &chunk : chunks) {
        const std::uint32_t size = 8 + 4 * static_cast<std::uint32_t>(chunk.corners.size()) + 4 +
                                   8 * static_cast<std::uint32_t>(chunk.quads.size()) + 4;
        seed_word(at, size << 8);
        seed_word(at + 4, 0);
        std::uint32_t word = at + 8;
        for (const std::uint32_t c : chunk.corners) seed_word(word, c), word += 4;
        seed_word(word, 0xFFFFFFFFu), word += 4;
        for (const auto &q : chunk.quads) {
            seed_word(word, q[0] << 22 | q[1] << 12 | q[4] << 2);
            seed_word(word + 4, q[2] << 22 | q[3] << 12);
            word += 8;
        }
        seed_word(word, 0xFFFFFFFFu);
        at += size;
    }
    seed_word(at, 0x80000000u);
    return at + 4;
}

void write_room(int index, std::uint32_t room, std::uint32_t mesh, std::int32_t x, std::int32_t y, std::int32_t z) {
    seed_word(0x800776E4u + 4u * static_cast<std::uint32_t>(index), room);
    seed_word(room + 0x10, mesh);
    seed_word(room + 0x18, static_cast<std::uint32_t>(x));
    seed_word(room + 0x1C, static_cast<std::uint32_t>(y));
    seed_word(room + 0x20, static_cast<std::uint32_t>(z));
}

void write_texture(int index, std::uint16_t clut, std::uint8_t u0, std::uint8_t v0, std::uint16_t page, std::uint8_t u1,
                   std::uint8_t v1) {
    seed_word(kTable + 8u * static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(clut) << 16 | v0 << 8 | u0);
    seed_word(kTable + 8u * static_cast<std::uint32_t>(index) + 4, static_cast<std::uint32_t>(page) << 16 | v1 << 8 | u1);
}

Chunk square(std::uint32_t texture = 1) {
    return Chunk{{corner(1, 2, 3), corner(5, 2, 3), corner(1, 2, 7), corner(5, 2, 7)}, {{0, 1, 3, 2, texture}}};
}

// A level of one room at origin (10, -5, 3) with one square face.
void one_room() {
    g_ram.fill(0);
    seed_word(0x800715A0u, 1);
    seed_word(0x8007162Cu, kTable);
    seed_byte(0x80075E9Cu, 0);
    write_room(0, 0x80100000u, 0x80110000u, 10, -5, 3);
    write_texture(1, 0x7A40, 32, 64, 0x0089, 63, 64);
    write_mesh(0x80110000u, {square()});
}

void draws(int count) {
    for (int i = 0; i < count; ++i) disruptor::ddgi_level::publish_if_changed();
}

bool corner_is(const GpuDdgiTriangle &t, int k, float x, float y, float z) {
    return t.world[k][0] == x && t.world[k][1] == y && t.world[k][2] == z;
}

void reads_the_rooms_faces() {
    one_room();
    g_calls = 0;
    draws(1);
    expect(g_calls == 1, "the first draw with the sun on reads the level at once");
    expect(g_level.size() == 2, "a square face is two triangles");
    if (g_level.size() != 2) return;
    const GpuDdgiTriangle &first = g_level[0], &second = g_level[1];
    // A corner is (-(y*8 + b*16), -(z*8 + c*16), x*8 + a*16), and the game's quad is A B D C.
    expect(corner_is(first, 0, 8, -72, 96) && corner_is(first, 1, 8, -72, 160) && corner_is(first, 2, 8, -136, 96),
           "the first triangle is A B D in world units");
    expect(corner_is(second, 0, 8, -72, 160) && corner_is(second, 1, 8, -136, 96) && corner_is(second, 2, 8, -136, 160),
           "the second is B D C");
    expect(first.uv[0][0] == 32 && first.uv[0][1] == 64 && first.uv[1][0] == 63 && first.uv[1][1] == 64 && first.uv[2][0] == 32 &&
               first.uv[2][1] == 95,
           "A takes the table's first uv, B its second, D the first 31 down");
    expect(second.uv[2][0] == 63 && second.uv[2][1] == 95, "C is the first uv 31 across and 31 down");
    expect(first.clut == 0x7A40 && first.texpage == 0x0089 && first.flags == 1, "the table's CLUT and page, textured");
    expect(first.colour[0][0] == 128 && second.colour[2][2] == 128, "lit as drawn: the neutral vertex colour");
}

// Inside a triangle of the face's plane (x is the same for all corners), by the sides of its three edges in (y, z).
bool inside(const GpuDdgiTriangle &t, float y, float z) {
    const auto side = [&](int a, int b) {
        return (t.world[b][1] - t.world[a][1]) * (z - t.world[a][2]) - (t.world[b][2] - t.world[a][2]) * (y - t.world[a][1]);
    };
    const float s0 = side(0, 1), s1 = side(1, 2), s2 = side(2, 0);
    return (s0 > 0 && s1 > 0 && s2 > 0) || (s0 < 0 && s1 < 0 && s2 < 0);
}

void covers_the_face_once() {
    one_room();
    g_sun = 0;
    draws(1);
    g_sun = 1;
    draws(1);
    expect(g_level.size() == 2, "a square face is two triangles");
    if (g_level.size() != 2) return;
    int once = 0;
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 7; ++j) {
            const float y = -136.0f + 64.0f * (static_cast<float>(i) + 0.3f) / 7.0f;
            const float z = 96.0f + 64.0f * (static_cast<float>(j) + 0.6f) / 7.0f;
            once += inside(g_level[0], y, z) + inside(g_level[1], y, z) == 1;
        }
    expect(once == 49, "the two triangles cover the face once: no point of it in both or in neither");
}
void reads_again_every_thirty_draws() {
    one_room();
    g_sun = 1;
    g_calls = 0;
    g_reads = 0;
    draws(29);
    expect(g_reads == 0, "nothing is read between the thirtieth draws");
    draws(1);
    expect(g_reads == 1, "the thirtieth reads the level");
    expect(g_calls == 0, "a level read the same is not handed over again");

    write_texture(1, 0x1111, 32, 64, 0x0089, 63, 64);
    draws(30);
    expect(g_calls == 1 && g_level.size() == 2 && g_level[0].clut == 0x1111,
           "a texture's first word changed is handed over: the renderer repacks the materials into the BVH it has");
    write_texture(1, 0x1111, 32, 64, 0x0001, 63, 64);
    draws(30);
    expect(g_calls == 2 && g_level[0].texpage == 0x0001, "and so is its second word");
    write_texture(1, 0x7A40, 32, 64, 0x0089, 63, 64);
    draws(30);
    expect(g_calls == 3, "and back");

    write_mesh(0x80110000u, {Chunk{{corner(1, 2, 3), corner(9, 2, 3), corner(1, 2, 7), corner(9, 2, 7)}, {{0, 1, 3, 2, 1}}}});
    draws(30);
    expect(g_calls == 4 && g_level.size() == 2 && g_level[0].world[1][2] == 224.0f, "a moved corner is read and handed over");

    write_texture(2, 0x7A40, 32, 64, 0x0089, 63, 64);
    write_mesh(0x80110000u, {Chunk{{corner(1, 2, 3), corner(9, 2, 3), corner(1, 2, 7), corner(9, 2, 7)}, {{0, 1, 3, 2, 2}}}});
    draws(30);
    expect(g_calls == 5, "a face's own words are part of the digest: its texture index changed to an entry of the same words");
}

void a_refused_level_is_offered_again() {
    one_room();
    draws(30);
    g_calls = 0;
    g_accept = 0;
    write_mesh(0x80110000u, {square(3)});
    draws(30);
    expect(g_calls == 1, "a changed level is offered");
    draws(30);
    expect(g_calls == 2, "the renderer could not take it: offered again unchanged");
    g_accept = 1;
    draws(30);
    expect(g_calls == 3, "and taken");
    draws(30);
    expect(g_calls == 3, "then kept until it changes");
}

void sun_off_forgets() {
    one_room();
    draws(30);
    g_calls = 0;
    g_sun = 0;
    draws(45);
    expect(g_calls == 0, "with the sun off nothing is read");
    g_sun = 1;
    draws(1);
    expect(g_calls == 1, "turned on again, the level is read and handed over at once");
    g_here = 0;
    draws(45);
    expect(g_calls == 1, "a mission without a sun reads nothing");
    g_here = 1;
    draws(1);
}

void copies_and_order() {
    one_room();
    write_mesh(0x80110000u, {Chunk{{corner(1, 2, 3, 1), corner(5, 2, 3), corner(1, 2, 7), corner(5, 2, 7)}, {{1, 2, 4, 3, 1}}}});
    seed_word(0x800715A0u, 2);
    seed_byte(0x80075E9Cu, 1);
    seed_byte(0x80075E9Cu + 1, 0);
    write_room(1, 0x80101000u, 0x80130000u, 0, 0, 0);
    write_mesh(0x80130000u, {square(), square()});
    g_calls = 0;
    draws(30);
    expect(g_calls == 1 && g_level.size() == 6, "every room and every chunk is read: two rooms, three squares");
    if (g_level.size() != 6) return;
    expect(corner_is(g_level[0], 0, -32, -48, 16), "rooms come in load order: room 1 first, at its own origin");
    expect(corner_is(g_level[4], 0, 8, -72, 96) && corner_is(g_level[4], 1, 8, -72, 160),
           "a corner given twice takes two indices: the faces after it still meet the right corners");
}

void malformed_levels() {
    // Each read starts with nothing handed over, so a read that went through would be handed over and seen.
    const auto refused = [](const char *message) {
        g_sun = 0;
        draws(1);
        g_sun = 1;
        g_calls = 0;
        draws(1);
        expect(g_calls == 0, message);
    };
    one_room();
    write_room(0, 0x00001000u, 0x80110000u, 10, -5, 3);
    refused("a room outside RAM leaves the level as it was");
    one_room();
    write_mesh(0x80110000u, {Chunk{{corner(1, 2, 3), corner(5, 2, 3), corner(1, 2, 7)}, {{0, 1, 2, 3, 1}}}});
    refused("a face naming a corner the chunk does not have");
    one_room();
    write_room(0, 0x80100000u, 0x80110000u, 1 << 24, -5, 3);
    refused("an origin far outside any level");
    one_room();
    seed_word(0x800715A0u, 0);
    refused("a level of no rooms");
    one_room();
    seed_word(0x800715A0u, 300);
    refused("more rooms than the game has");
    one_room();
    seed_word(0x80110000u, 12u << 8);
    refused("a chunk smaller than its own words");
    one_room();
    seed_word(0x8007162Cu, 0x801FFFFCu);
    refused("a texture table at the end of RAM");
    one_room();
    write_room(0, 0x80100000u, 0, 10, -5, 3);
    seed_word(0x8007162Cu, 0x7FFFF000u);
    refused("a texture table outside RAM is refused though no face reads it");
    one_room();
    seed_word(0x8007162Cu, 0x7FFFF000u);
    write_mesh(0x80110000u, {square(512)});
    refused("a texture table below RAM is refused though a face's entry would land inside RAM");
    one_room();
    g_sun = 0;
    draws(1);
    g_sun = 1;
    g_calls = 0;
    draws(1);
    expect(g_calls == 1, "the same start with a whole level hands it over: the refusals above are the structures'");
}

}  // namespace

int main() {
    reads_the_rooms_faces();
    covers_the_face_once();
    reads_again_every_thirty_draws();
    a_refused_level_is_offered_again();
    sun_off_forgets();
    copies_and_order();
    malformed_levels();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "disruptor_ddgi_level: ok\n";
    return 0;
}
