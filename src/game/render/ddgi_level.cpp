#include "game/render/ddgi_level.h"

#include "gpu_ddgi.h"
#include "mod_plugins.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace disruptor::ddgi_level {
namespace {

/* The level's rooms as the renderer 0x80040E68 walks them and the mesh renderer 0x80046134 reads their meshes. */
constexpr uint32_t kRoomCount = 0x800715A0u;   /* gp+1108, set at level load 0x80016EB4 */
constexpr uint32_t kRoomOrder = 0x80075E9Cu;   /* the rooms' indices in load order */
constexpr uint32_t kRooms = 0x800776E4u;       /* room pointers by index */
constexpr uint32_t kTextures = 0x8007162Cu;    /* gp+0x4E0: two words a texture, clut and uv0, tpage and uv1 */
constexpr uint32_t kRamEnd = 0x80200000u;
constexpr int kMostRooms = 256;
constexpr int kMostChunks = 512;
constexpr int kMostEntries = 1024;              /* a face's corner is a 10-bit index */
constexpr int kMostTriangles = 98304;
constexpr int32_t kMostOrigin = 1 << 24;   /* room origins times 8 stay well inside 32 bits */
constexpr int kEveryDraws = 30;

bool in_ram(uint32_t address, uint32_t bytes) { return address >= 0x80000000u && address < kRamEnd && kRamEnd - address >= bytes; }

int32_t signed_word(uint32_t address) { return static_cast<int32_t>(psx_mod_read_word(address)); }

struct Corner {
    float world[3];
};

class LevelReader {
public:
    /* The faces of every room, or false when a structure does not read as the mesh renderer's. */
    bool read(std::vector<GpuDdgiTriangle> &out, uint64_t &digest) {
        out.clear();
        digest = faces_ = materials_ = 1469598103934665603ull;
        const uint32_t rooms = psx_mod_read_word(kRoomCount), textures = psx_mod_read_word(kTextures);
        if (rooms == 0 || rooms > kMostRooms || !in_ram(textures, 8)) return false;
        mix(digest, rooms);
        for (uint32_t i = 0; i < rooms; ++i) {
            const uint32_t room = psx_mod_read_word(kRooms + 4u * psx_mod_read_byte(kRoomOrder + i));
            if (!in_ram(room, 0x24)) return false;
            const uint32_t mesh = psx_mod_read_word(room + 0x10);
            const int32_t origin[3] = {signed_word(room + 0x18), signed_word(room + 0x1C), signed_word(room + 0x20)};
            for (const int32_t value : origin)
                if (value <= -kMostOrigin || value >= kMostOrigin) return false;
            mix(digest, mesh);
            for (const int32_t value : origin) mix(digest, static_cast<uint32_t>(value));
            if (mesh && !read_mesh(mesh, origin, textures, out, digest)) return false;
        }
        return true;
    }

    uint64_t faces() const { return faces_; }
    uint64_t materials() const { return materials_; }

private:
    static void mix(uint64_t &digest, uint32_t value) { digest = (digest ^ value) * 1099511628211ull; }

    bool read_mesh(uint32_t at, const int32_t origin[3], uint32_t textures, std::vector<GpuDdgiTriangle> &out,
                   uint64_t &digest) {
        for (int chunk = 0; chunk < kMostChunks; ++chunk) {
            if (!in_ram(at, 8)) return false;
            const uint32_t header = psx_mod_read_word(at), size = header >> 8, lights = psx_mod_read_word(at + 4);
            if (header & 0x80000000u) return true;
            if (size < 16 || lights >= size || !in_ram(at, size)) return false;
            const uint32_t end = at + size;
            mix(digest, header);
            mix(digest, lights);
            uint32_t vertex = at + 8 + lights;
            entries_.clear();
            for (;;) {
                if (vertex + 4 > end) return false;
                const uint32_t word = psx_mod_read_word(vertex);
                vertex += 4;
                if (word == 0xFFFFFFFFu) break;
                mix(digest, word);
                const int32_t a = static_cast<int32_t>((word >> 20) & 1023u), b = static_cast<int32_t>((word >> 10) & 1023u),
                              c = static_cast<int32_t>(word & 1023u);
                const Corner corner{{static_cast<float>(-(origin[1] * 8 + b * 16)), static_cast<float>(-(origin[2] * 8 + c * 16)),
                                     static_cast<float>(origin[0] * 8 + a * 16)}};
                for (uint32_t copy = 0; copy <= word >> 30; ++copy) entries_.push_back(corner);
                if (entries_.size() > kMostEntries) return false;
            }
            mix(digest, static_cast<uint32_t>(entries_.size()));
            uint32_t face = vertex;
            for (;;) {
                if (face + 4 > end) return false;
                const uint32_t first = psx_mod_read_word(face);
                if (first == 0xFFFFFFFFu) break;
                if (face + 8 > end) return false;
                const uint32_t second = psx_mod_read_word(face + 4);
                face += 8;
                mix(faces_, first);
                mix(faces_, second);
                const uint32_t refs[4] = {first >> 22, (first >> 12) & 1023u, second >> 22, (second >> 12) & 1023u};
                if (refs[0] >= entries_.size() || refs[1] >= entries_.size() || refs[2] >= entries_.size() ||
                    refs[3] >= entries_.size())
                    return false;
                const uint32_t texture = textures + ((first >> 2) & 1023u) * 8u;
                if (!in_ram(texture, 8)) return false;
                const uint32_t clut_uv = psx_mod_read_word(texture), page_uv = psx_mod_read_word(texture + 4);
                mix(materials_, clut_uv);
                mix(materials_, page_uv);
                const uint8_t u0 = clut_uv & 255u, v0 = (clut_uv >> 8) & 255u;
                /* The renderer's quad is A B D C with uv0, the table's uv1, uv0 + (0, 31) and uv0 + (31, 31). */
                const uint32_t corner[4] = {refs[0], refs[1], refs[3], refs[2]};
                const uint8_t uv[4][2] = {{u0, v0},
                                          {static_cast<uint8_t>(page_uv & 255u), static_cast<uint8_t>((page_uv >> 8) & 255u)},
                                          {u0, static_cast<uint8_t>(v0 + 31)},
                                          {static_cast<uint8_t>(u0 + 31), static_cast<uint8_t>(v0 + 31)}};
                for (const auto &three : kHalves) {
                    if (out.size() >= kMostTriangles) return false;
                    GpuDdgiTriangle triangle{};
                    for (int k = 0; k < 3; ++k) {
                        for (int axis = 0; axis < 3; ++axis) triangle.world[k][axis] = entries_[corner[three[k]]].world[axis];
                        triangle.uv[k][0] = uv[three[k]][0];
                        triangle.uv[k][1] = uv[three[k]][1];
                        triangle.colour[k][0] = triangle.colour[k][1] = triangle.colour[k][2] = 128;
                    }
                    triangle.clut = static_cast<uint16_t>(clut_uv >> 16);
                    triangle.texpage = static_cast<uint16_t>(page_uv >> 16);
                    triangle.flags = 1;
                    out.push_back(triangle);
                }
            }
            if (face + 4 != end) return false;
            at = face + 4;
        }
        return false;
    }

    static constexpr int kHalves[2][3] = {{0, 1, 2}, {1, 2, 3}};
    std::vector<Corner> entries_;
    uint64_t faces_ = 0, materials_ = 0;
};

LevelReader g_reader;
std::vector<GpuDdgiTriangle> g_faces;
uint64_t g_published = 0, g_shape = 0, g_face_words = 0, g_materials = 0;
int g_draws = kEveryDraws; /* the first draw with the sun on reads at once */
bool g_told = false;

}  // namespace

void publish_if_changed() {
    if (!gl_renderer_ddgi_sun() || !gl_renderer_ddgi_sun_here()) {
        g_published = 0;
        g_draws = kEveryDraws;
        return;
    }
    if (++g_draws < kEveryDraws) return;
    g_draws = 0;
    uint64_t digest = 0;
    const auto began = std::chrono::steady_clock::now();
    const bool read = g_reader.read(g_faces, digest);
    if (std::getenv("PSX_DDGI"))
        std::fprintf(stderr, "disruptor: level read in %.2f ms\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
    if (!read) {
        if (!g_told) std::fprintf(stderr, "disruptor: the level's meshes did not read whole, the sun keeps the faces it had\n");
        g_told = true;
        return;
    }
    /* Materials count too: the game rewrites animated textures' entries every frame (measured on mission 2), the sun sees
     * the phase of every 30th draw, and with the same corners the renderer repacks them into the BVH it has. */
    const uint64_t whole = digest ^ g_reader.faces() * 31u ^ g_reader.materials() * 131u;
    if (whole == g_published) return;
    if (std::getenv("PSX_DDGI"))
        std::fprintf(stderr, "disruptor: level read changed: shape %d faces %d materials %d\n", digest != g_shape,
                     g_reader.faces() != g_face_words, g_reader.materials() != g_materials);
    if (!gpu_ddgi_set_level(g_faces.data(), static_cast<int>(g_faces.size()))) return;
    g_shape = digest, g_face_words = g_reader.faces(), g_materials = g_reader.materials();
    g_published = whole;
}

}  // namespace disruptor::ddgi_level
