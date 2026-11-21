#include "game/render/ddgi_level.h"

#include "game/render/far_rendering.h"
#include "game_host.h"
#include "gpu_ddgi.h"
#include "mod_plugins.h"
#include "game/render/portal_reach.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
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
    uint8_t light[3];
};

/* 0x800464F0's near colour: 2 x a byte below 128, else the 15-bit entry byte - 128 steps down from the vertex words. */
bool vertex_light(uint8_t byte, uint32_t block, uint32_t end, uint8_t out[3]) {
    if (byte < 128u) {
        out[0] = out[1] = out[2] = static_cast<uint8_t>(2u * byte);
        return true;
    }
    const uint32_t entry = end - 2u - 2u * (byte - 128u);
    if (entry < block) return false;
    const uint32_t colour = psx_mod_read_byte(entry) | static_cast<uint32_t>(psx_mod_read_byte(entry + 1)) << 8;
    out[0] = static_cast<uint8_t>((colour & 31u) << 3);
    out[1] = static_cast<uint8_t>(((colour >> 5) & 31u) << 3);
    out[2] = static_cast<uint8_t>(((colour >> 10) & 31u) << 3);
    return true;
}

uint32_t balcony_door_leaf(uint8_t sector, uint32_t mesh, const int32_t origin[3]) {
    if ((sector != 126u && sector != 127u) || origin[1] != 12032 || origin[2] != 1408) return 0;
    const bool east = sector == 127u;
    const uint32_t offset = east ? 0xFCu : 0xF4u;
    if (origin[0] != (east ? 12416 : 11840) || !in_ram(mesh, offset + 0x238u) ||
        psx_mod_read_word(mesh) != ((offset << 8) | 0xFFu)) return 0;
    const uint32_t leaf = mesh + offset;
    return psx_mod_read_word(leaf) == 0x238FFu && psx_mod_read_word(leaf + 4u) == 0x40u ? leaf : 0u;
}

class LevelReader {
public:
    /* The faces of every room, or false when a structure does not read as the mesh renderer's. */
    bool read(std::vector<GpuDdgiTriangle> &out, uint64_t &digest, std::bitset<256> *remaining = nullptr,
              std::array<float, kMostRooms> *ceilings = nullptr) {
        out.clear();
        digest = faces_ = materials_ = 1469598103934665603ull;
        stand_ins_ = 0;
        const uint32_t rooms = psx_mod_read_word(kRoomCount), textures = psx_mod_read_word(kTextures);
        if (rooms == 0 || rooms > kMostRooms || !in_ram(textures, 8)) return false;
        mix(digest, rooms);
        for (uint32_t i = 0; i < rooms; ++i) {
            const auto sector = psx_mod_read_byte(kRoomOrder + i);
            if (remaining && !remaining->test(sector)) continue;
            if (remaining) remaining->reset(sector);
            const uint32_t room = psx_mod_read_word(kRooms + 4u * sector);
            if (!in_ram(room, 0x24)) return false;
            const uint32_t mesh = psx_mod_read_word(room + 0x10);
            const int32_t origin[3] = {signed_word(room + 0x18), signed_word(room + 0x1C), signed_word(room + 0x20)};
            for (const int32_t value : origin)
                if (value <= -kMostOrigin || value >= kMostOrigin) return false;
            mix(digest, mesh);
            for (const int32_t value : origin) mix(digest, static_cast<uint32_t>(value));
            const size_t first = out.size();
            // A moving door is drawn by the game at its current height, while the night mask keeps its mesh for the whole area.
            const uint32_t moving_door = remaining ? balcony_door_leaf(sector, mesh, origin) : 0u;
            if (mesh && !read_mesh(mesh, origin, textures, out, digest, moving_door)) return false;
            if (ceilings) {
                float &top = (*ceilings)[sector];
                top = static_cast<float>(origin[2]);
                for (size_t face = first; face < out.size(); ++face)
                    for (const auto &point : out[face].world) top = std::max(top, -point[1] / 8.0f);
            }
        }
        return true;
    }

    uint64_t faces() const { return faces_; }
    int stand_ins() const { return stand_ins_; }
    uint64_t materials() const { return materials_; }

private:
    static void mix(uint64_t &digest, uint32_t value) { digest = (digest ^ value) * 1099511628211ull; }

    bool read_mesh(uint32_t at, const int32_t origin[3], uint32_t textures, std::vector<GpuDdgiTriangle> &out,
                   uint64_t &digest, uint32_t moving_door) {
        for (int chunk = 0; chunk < kMostChunks; ++chunk) {
            if (!in_ram(at, 8)) return false;
            const uint32_t header = psx_mod_read_word(at), size = header >> 8, lights = psx_mod_read_word(at + 4);
            if (header & 0x80000000u) return true;
            if (size < 16 || lights >= size || !in_ram(at, size)) return false;
            const uint32_t end = at + size;
            mix(digest, header);
            mix(digest, lights);
            /* 0x800461AC: a block whose low byte names a room draws only while that room is unseen, a stand-in for it. */
            const bool stand_in = (header & 255u) != 255u;
            if (stand_in || at == moving_door) {
                stand_ins_ += stand_in;
                at = end;
                continue;
            }
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
                Corner corner{{static_cast<float>(-(origin[1] * 8 + b * 16)), static_cast<float>(-(origin[2] * 8 + c * 16)),
                               static_cast<float>(origin[0] * 8 + a * 16)},
                              {}};
                for (uint32_t copy = 0; copy <= word >> 30; ++copy) {
                    const uint32_t record = static_cast<uint32_t>(entries_.size());  /* each record its own light byte */
                    if (record >= lights ||
                        !vertex_light(psx_mod_read_byte(at + 8 + record), at + 8, at + 8 + lights, corner.light))
                        return false;
                    entries_.push_back(corner);
                }
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
                const uint32_t tile = (first >> 2) & 1023u;
                const uint32_t texture = textures + tile * 8u;
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
                        for (int channel = 0; channel < 3; ++channel)
                            triangle.colour[k][channel] = entries_[corner[three[k]]].light[channel];
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

    static constexpr int kHalves[2][3] = {{0, 1, 2}, {2, 1, 3}};  /* wound alike, as the frame recorder's */
    std::vector<Corner> entries_;
    uint64_t faces_ = 0, materials_ = 0;
    int stand_ins_ = 0;
};

LevelReader g_reader;
std::vector<GpuDdgiTriangle> g_faces;
uint64_t g_published = 0, g_shape = 0, g_face_words = 0, g_materials = 0;
int g_draws = kEveryDraws; /* the first draw with lighting on reads at once */
bool g_told = false;

struct NightRooms {
    std::bitset<256> rooms;
    std::array<std::array<uint32_t, 7>, kMostRooms> layouts{};
    uint32_t textures = 0, generation = 0;
    bool operator==(const NightRooms &) const = default;
};
struct NightArea {
    NightRooms key;
    std::vector<PsxNightVertex> vertices;
    int id = 0;
};
constexpr size_t kNightAreaLimit = 64;
std::list<std::shared_ptr<const NightArea>> night_areas;
std::mutex night_mutex;
int next_night_id = 1;
uint32_t night_generation = 0;
std::atomic<bool> night_requested{false};
NightRooms failed_rooms;
uint32_t failed_at = 0;

void add_unloaded_portals(NightArea &area, const std::array<float, kMostRooms> &ceilings) {
    namespace portal = disruptor::portal_reach;
    for (unsigned sector = 0; sector < kMostRooms; ++sector) {
        if (!area.key.rooms.test(sector)) continue;
        const uint32_t room = psx_mod_read_word(kRooms + 4u * sector);
        uint32_t gate = psx_mod_read_word(room + portal::kSectorPortals);
        for (unsigned i = 0; i < portal::kMostPortals && in_ram(gate, portal::kPortalStride); ++i, gate += portal::kPortalStride) {
            const uint32_t a = psx_mod_read_word(gate), b = psx_mod_read_word(gate + 4);
            if (a == 0xFFFFFFFFu) break;
            const uint8_t next = psx_mod_read_byte(gate + sector_reach::kPortalSector);
            if (next != sector_reach::kNoSector && !area.key.rooms.test(next)) {
                const uint32_t heights = psx_mod_read_word(gate + 8);
                const float low = static_cast<int16_t>(heights), high = std::min(ceilings[sector], float(static_cast<int16_t>(heights >> 16)));
                const float ax = static_cast<int16_t>(a), az = static_cast<int16_t>(a >> 16);
                const float bx = static_cast<int16_t>(b), bz = static_cast<int16_t>(b >> 16);
                if (low < high) {
                    for (const int corner : {0, 1, 2, 2, 1, 3})
                        area.vertices.push_back({{corner & 1 ? bx : ax, corner < 2 ? low : high, corner & 1 ? bz : az},
                                                 {az - bz, bx - ax}, static_cast<uint16_t>(corner), 0xFFFFu});
                }
            }
            if (psx_mod_read_byte(gate + portal::kPortalLast) == 255u) break;
        }
    }
}
}  // namespace

int night_area(std::size_t queued_frames) {
    if (!night_requested.load(std::memory_order_relaxed)) return 0;
    const uint32_t count = psx_mod_read_word(kRoomCount);
    if (!count || count > kMostRooms) return 0;
    NightRooms key;
    key.textures = psx_mod_read_word(kTextures);
    key.generation = night_generation;
    for (uint32_t i = 0; i < count; ++i) {
        const auto sector = psx_mod_read_byte(kRoomOrder + i);
        const auto room = psx_mod_read_word(kRooms + 4u * sector);
        if (!in_ram(room, 44)) return 0;
        key.rooms.set(sector);
        key.layouts[sector] = {room, psx_mod_read_word(room + 0x10), psx_mod_read_word(room + 0x14),
                               psx_mod_read_word(room + 0x18), psx_mod_read_word(room + 0x1C),
                               psx_mod_read_word(room + 0x20), psx_mod_read_word(room + 0x28)};
    }
    {
        const std::scoped_lock lock(night_mutex);
        const auto known = std::find_if(night_areas.begin(), night_areas.end(),
                                        [&](const auto &area) { return area->key == key; });
        if (known != night_areas.end()) {
            night_areas.splice(night_areas.begin(), night_areas, known);
            return night_areas.front()->id;
        }
    }
    const uint32_t tick = psx_mod_read_word(0x8005B920u);
    if (key == failed_rooms && tick - failed_at < 60u) return 0;
    disruptor_far_rendering_trim_triton_balcony();
    LevelReader reader;
    std::vector<GpuDdgiTriangle> triangles;
    uint64_t digest;
    auto remaining = key.rooms;
    std::array<float, kMostRooms> ceilings{};
    if (!reader.read(triangles, digest, &remaining, &ceilings) || triangles.empty()) {
        failed_rooms = key;
        failed_at = tick;
        return 0;
    }
    auto area = std::make_shared<NightArea>();
    area->key = key;
    area->vertices.reserve(triangles.size() * 3);
    for (const auto &triangle : triangles) {
        for (int i = 0; i < 3; ++i) {
            const auto &point = triangle.world[i];
            area->vertices.push_back({{point[2] / 8.0f, -point[1] / 8.0f, -point[0] / 8.0f},
                                      {float(triangle.uv[i][0]), float(triangle.uv[i][1])}, triangle.clut, triangle.texpage});
        }
    }
    add_unloaded_portals(*area, ceilings);
    const std::scoped_lock lock(night_mutex);
    area->id = next_night_id;
    next_night_id = next_night_id == std::numeric_limits<int>::max() ? 1 : next_night_id + 1;
    night_areas.push_front(area);
    while (night_areas.size() > kNightAreaLimit + queued_frames) night_areas.pop_back();
    return area->id;
}

const PsxNightVertex *night_geometry(int area, int *vertices) {
    night_requested.store(true, std::memory_order_relaxed);
    // The draw can upload its geometry after releasing the cache lock.
    static thread_local std::shared_ptr<const NightArea> reading;
    const std::scoped_lock lock(night_mutex);
    const auto found = std::find_if(night_areas.begin(), night_areas.end(),
                                    [area](const auto &candidate) { return candidate->id == area; });
    reading = found != night_areas.end() ? *found : nullptr;
    *vertices = reading ? static_cast<int>(reading->vertices.size()) : 0;
    return reading ? reading->vertices.data() : nullptr;
}

void invalidate_night_area() { ++night_generation; }

void publish_if_changed() {
    if (!gl_renderer_ddgi() && !gl_renderer_ddgi_shadows() && !gl_renderer_ddgi_lamps()) {
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
        std::fprintf(stderr, "disruptor: level read in %.2f ms, %zu faces, %d stand-in blocks left out\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(), g_faces.size(),
                     g_reader.stand_ins());
    if (!read) {
        if (!g_told) std::fprintf(stderr, "disruptor: the level's meshes did not read whole, the lighting keeps the faces it had\n");
        g_told = true;
        return;
    }
    /* Materials count too: the game rewrites animated textures' entries every frame (measured on mission 2), the probes see
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
