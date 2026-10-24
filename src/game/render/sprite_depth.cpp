/*
 * Camera depth of Disruptor's CPU-projected world sprites (SLUS-00224).
 *
 * Pickups, enemies and effects are projected by the game's own code as
 * x = 160 + 160 * X / Z, then built into a POLY_FT4 around that point. The
 * GTE never sees them, so the renderer has no depth for them and cannot
 * move them with the camera between two game frames. An instruction seam
 * at each projection reads Z, and the packet seam of the same funnel hands
 * it over with the packet. The fourth funnel can defer an actor
 * into a sorted record instead: its Z waits under the record's index.
 * The same seam reads X and Y, so the remainders of the two divisions go
 * along: the sprite stands between pixels like the walls around it. Its
 * size goes along too, before the game cuts it to whole pixels. The third
 * funnel sizes an effect after its projection: the size is worked out
 * ahead and holds only if the game's registers agree at the packet.
 * Guest state is only read, but for the range a funnel sorts and fades by:
 * under the widescreen squash it is made the squashed world's. And a body
 * sorts kBodyReach nearer than the game sorts it, its shadow with it, so the
 * floor polygon it lies on is not drawn over it, yet not past an upright
 * world face that stands in front of it. A world sprite already linked over
 * it on screen moves along to stay over it, and its fade is the game's own.
 */

#include "game/render/far_rendering.h"
#include "game/render/shot_lights.h"

#include "cpu_state.h"
#include "gpu.h"
#include "gte_precision.h"
#include "lockstep.h"
#include "mod_memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

extern "C" void gte_ws_x_squash(int32_t *num, int32_t *den);

namespace {

constexpr uint32_t kProjectWord = 0xAFA20010u;  /* sw $v0, 16($sp): the projected x is stored */
constexpr uint32_t kRecordSite = 0x8003D1B4u;
constexpr uint32_t kRecordWord = 0xA4379EA0u;  /* sh $s7, record.key */
constexpr uint32_t kRecordBytes = 20u;
constexpr uint32_t kDeferredPacketSite = 0x800433A0u;
constexpr uint32_t kDeferredProjection = 0x8003D078u;
/* Measured on a strafe: without this factor the camera puts no moving sprite face within 1.5 px of where it was. */
constexpr int32_t kGteDepthPerSpriteDepth = 8;
constexpr uint32_t kFocal = 160u;
constexpr int64_t kOne = 65536;
constexpr int32_t kFarColumn = 4096;  /* past this a 16.16 column no longer fits */
constexpr int32_t kHorizon = 120;
constexpr uint32_t kReciprocals = 0x80057E98u;  /* 65536 / n in 16 bits, a table the game builds at start */
constexpr uint32_t kCameraHeight = 0x800775D0u;
constexpr uint32_t kShadowPacketSite = 0x8003D488u;
constexpr uint32_t kEffectPacketSite = 0x8003C848u;
constexpr uint8_t kShotRecordGpr = 19u; /* $s3: the shot the draw 0x8003C000 is at, from 0x8003C03C to 0x8003CAF4 */
constexpr uint32_t kWarped = 1u;
constexpr int32_t kLargest = 0x1FF;  /* the game caps a side here and rescales the other */
constexpr int64_t kTaken = 3 * kOne / 2;  /* the renderer takes a size this far over its packet's, an effect's two cuts can leave more */
constexpr int64_t kBodyLead = 0x18;  /* 0x8003CEC8..0x8003CEEC: the fourth funnel sorts this much nearer, down to kNearest */
constexpr int64_t kNearest = 8;
constexpr int64_t kBodyReach = 64;
constexpr int64_t kHalvedBelow = kNearest + 2 * kBodyReach;
constexpr int64_t kSlotShift = 3;
constexpr int64_t kFartherSlots = (kBodyLead >> kSlotShift) + 1;  /* the game's own lead can sort a body this many slots before a face it lies past */
constexpr int64_t kLastSlot = 509;  /* 0x80040EC4 clears 512 entries from two before the table's slot 0 */
constexpr uint32_t kSlotBytes = 4u;
constexpr uint32_t kOrderingTable = 836u;  /* gp + 836: the ordering table's first slot */
constexpr uint32_t kFadeStart = 1172u;  /* gp + 1172: the range the deferred pass fades from (0x80043184) */
constexpr int32_t kFadeLast = 767;
constexpr int32_t kFadeStep = 6;
constexpr int32_t kClutRow = 6;  /* a CLUT id's y starts at bit 6 */
constexpr uint32_t kClutStore = 8u;  /* the deferred pass stores the CLUT at 8($s1), 0x8004331C */
constexpr uint8_t kRecordKey = 20u;  /* $s4 holds the record's key through the deferred pass */
constexpr uint8_t kRecordClut = 17u;
constexpr uint8_t kGlobals = 28u;
constexpr uint32_t kTagNext = 0x00FFFFFFu;
constexpr uint32_t kKseg0 = 0x80000000u;
constexpr uint32_t kSpriteWords = 9u;  /* a funnel's POLY_FT4: its tag counts nine words, its code is 0x2C..0x2F */
constexpr uint32_t kSpriteCode = 0x2Cu;
constexpr uint32_t kSpriteCodeMask = 0xFCu;
constexpr int kLinksWalked = 8192;
constexpr double kUpright = 0.6;  /* a face whose normal rises less than this share of its length is a wall */
constexpr std::size_t kMovedMost = 64;
constexpr uint32_t kTagWords = 0xFF000000u;

/* Where a funnel keeps the size it scales by 160 / Z. */
enum class Size : uint8_t { Words, Bytes, Effect, Body };

/* A register, or a stack slot when gpr is 0. */
struct Operand {
    uint8_t gpr;
    uint8_t stack;
};

struct Funnel {
    uint32_t projection;
    uint8_t depth_gpr;
    std::array<uint32_t, 2> packets;
    Operand x;
    Operand y;
    Size size;
    uint8_t range_gpr;
    bool led;  /* the range has had its lead by the time of the projection */
};

constexpr auto kFunnels = std::array<Funnel, 4>{{
    {0x8003B98Cu, 16u, {0x8003BB88u, 0u}, {7u, 0u}, {5u, 0u}, Size::Words, 18u, false},
    {0x8003BDA0u, 16u, {0x8003BFB0u, 0u}, {7u, 0u}, {6u, 0u}, Size::Bytes, 19u, false},
    {0x8003C4B0u, 21u, {kEffectPacketSite, 0x8003CAD4u}, {0u, 0x18u}, {0u, 0x20u}, Size::Effect, 23u, false},
    {kDeferredProjection, 20u, {kShadowPacketSite, 0u}, {8u, 0u}, {5u, 0u}, Size::Body, 23u, true},
}};

/* 16.16: the unrounded column of the projection, and how far it stands from the row the game stored. */
struct Place {
    int32_t x;
    int32_t dy;
};

/* A size in the game's units, before 160 / Z. */
struct Extent {
    int32_t wide;
    int32_t tall;
};

/* 16.16: the unrounded row the rectangle is centred on, and its unrounded sides. */
struct Shape {
    int32_t row;
    int32_t width;
    int32_t height;
    bool shadow;
};

struct Sprite {
    int32_t depth = 0;
    std::optional<Place> place;
    std::optional<Shape> shape;
    /* What an effect's first packet has to show in $s6 and $s2 for the shape to be the game's. */
    std::optional<Extent> cut;
    /* A body's record key as the game stored it, as its walk led it (its shadow follows), and as it is now. */
    int32_t key = 0;
    int32_t walked = 0;
    int32_t led = 0;
};

struct Box {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

uint32_t g_pending_projection = 0;
Sprite g_pending{};
std::array<Sprite, 256> g_record{};

[[nodiscard]] bool readable(const CPUState& cpu) {
    return cpu.read_word && cpu.read_half && cpu.read_byte;
}

struct CpuMemory {
    const CPUState& cpu;
    uint32_t word(uint32_t address) const { return cpu.read_word(address); }
    uint32_t byte(uint32_t address) const { return cpu.read_byte(address); }
};

[[nodiscard]] std::optional<int32_t> operand(const CPUState& cpu, Operand source) {
    if (source.gpr != 0u) return static_cast<int32_t>(cpu.gpr[source.gpr]);
    if (!cpu.read_word) return std::nullopt;
    return static_cast<int32_t>(cpu.read_word(cpu.gpr[29] + source.stack));
}

[[nodiscard]] constexpr int32_t scaled(int64_t units, int32_t depth) {
    return static_cast<int32_t>(static_cast<int64_t>(kFocal) * units * kOne / depth);
}

[[nodiscard]] constexpr int32_t low_product(int32_t left, int32_t right) {
    return static_cast<int32_t>(static_cast<uint32_t>(left) * static_cast<uint32_t>(right));
}

/* The fourth funnel's own steps from 0x8003CE60 to 0x8003CF88. Nothing when they do not end on the game's $s2 and $s3. */
[[nodiscard]] std::optional<Extent> body_extent(const CPUState& cpu, int32_t depth) {
    const uint32_t actor = cpu.gpr[21], body = cpu.gpr[22];
    const uint32_t kind = cpu.read_byte(actor + 0x3Au);
    const uint32_t frame = cpu.read_byte(actor + 0x3Bu) + (static_cast<uint8_t>(kind - 7u) < 2u ? 0u : cpu.gpr[16] & 0xFFu);
    const uint32_t picture = cpu.read_word(body) + 20u * frame;
    const int32_t scale = cpu.read_half(kReciprocals + 2u * cpu.read_word(body + 0xCu));
    const int32_t near = static_cast<int32_t>(kFocal) * cpu.read_half(kReciprocals + 2u * static_cast<uint32_t>(depth));
    const Extent extent{
        .wide = low_product(low_product(static_cast<int32_t>(cpu.read_word(body + 0x1Cu)), cpu.read_byte(picture + 8u)), scale) >> 16,
        .tall = low_product(low_product(static_cast<int32_t>(cpu.read_word(body + 0x18u)), cpu.read_byte(picture + 9u)), scale) >> 16,
    };
    const int32_t width = low_product(extent.wide, near) >> 16, height = low_product(extent.tall, near) >> 16;
    if (width <= 0 || height <= 0 || width > kLargest || height > kLargest ||
        width != static_cast<int32_t>(cpu.gpr[18]) || height != static_cast<int32_t>(cpu.gpr[19])) {
        return std::nullopt;
    }
    return extent;
}

/* One side of an effect: 16.16 before the game's two cuts, and what those cuts leave of it. */
struct Side {
    int32_t exact;
    int32_t cut;
};

struct Effect {
    Side wide;
    Side tall;
};

/* units * part / whole over the depth, cut once to whole pixels and once more after the ratio. */
[[nodiscard]] constexpr std::optional<Side> effect_side(int32_t units, int32_t part, int32_t whole, int32_t depth) {
    const int32_t cut = low_product(static_cast<int32_t>(kFocal) * units / depth, part) / whole;
    if (cut <= 0 || cut > kLargest) return std::nullopt;
    const int64_t exact = static_cast<int64_t>(kFocal) * units * part * kOne / (static_cast<int64_t>(depth) * whole);
    return Side{.exact = static_cast<int32_t>(std::min(exact, cut * kOne + kTaken)), .cut = cut};
}

/* The third funnel's own steps from 0x8003C4D4 to 0x8003C5F4, which run after its projection. */
[[nodiscard]] std::optional<Effect> effect_size(const CPUState& cpu, int32_t depth) {
    const uint32_t object = cpu.gpr[19], picture = cpu.gpr[20], kind = cpu.read_word(cpu.gpr[29] + 0x28u);
    const uint32_t which = cpu.read_byte(object + 0x1Du);
    const auto units = static_cast<int32_t>(cpu.read_word(kind + (which != 0u ? 0x18u : 0x14u)));
    const int32_t across = cpu.read_byte(kind + which + 0x30u), down = cpu.read_byte(kind + which + 0x34u);
    if (units <= 0 || units > 0xFFFF || across == 0 || down == 0) return std::nullopt;
    std::optional<Side> wide = effect_side(units, cpu.read_byte(picture + 8u), across, depth);
    const std::optional<Side> tall = effect_side(units, cpu.read_byte(picture + 9u), down, depth);
    if (!wide || !tall) return std::nullopt;
    if ((cpu.gpr[30] & 0xFFu) != 0u) {
        /* A mirrored packet runs back from x + (w + 1) / 2 over w + 1 columns, two more than one that is not. */
        wide = Side{.exact = wide->exact + 2 * static_cast<int32_t>(kOne), .cut = -wide->cut};
    }
    return Effect{.wide = *wide, .tall = *tall};
}

[[nodiscard]] std::optional<Extent> extent_of(const CPUState& cpu, const Funnel& funnel, int32_t depth) {
    if (!readable(cpu)) return std::nullopt;
    std::optional<Extent> extent;
    if (funnel.size == Size::Words) {
        extent = Extent{.wide = static_cast<int32_t>(cpu.read_word(cpu.gpr[21] + 4u)),
                        .tall = static_cast<int32_t>(cpu.read_word(cpu.gpr[21] + 8u))};
    } else if (funnel.size == Size::Bytes) {
        extent = Extent{.wide = cpu.read_byte(cpu.gpr[18] + 8u), .tall = cpu.read_byte(cpu.gpr[18] + 9u)};
    } else {
        extent = body_extent(cpu, depth);
    }
    if (!extent || extent->wide <= 0 || extent->tall <= 0 || extent->wide > 0xFFFF || extent->tall > 0xFFFF) return std::nullopt;
    return extent;
}

/* The shadow block 0x8003D22C..0x8003D488 hangs its own rectangle on the caster's column. Nothing when its numbers do not give the packet. */
[[nodiscard]] std::optional<Shape> shadow_shape(const CPUState& cpu, uint32_t packet, int32_t depth) {
    const uint32_t actor = cpu.gpr[21], body = cpu.gpr[22];
    const Extent extent{.wide = static_cast<int32_t>(cpu.read_word(body + 0xF8u)),
                        .tall = static_cast<int32_t>(cpu.read_word(body + 0xFCu))};
    const int32_t drop = static_cast<int32_t>(cpu.read_half(actor + 0x28u)) - static_cast<int32_t>(cpu.read_word(body + 0xF4u)) -
                         static_cast<int32_t>(cpu.read_word(kCameraHeight));
    const int32_t width = low_product(static_cast<int32_t>(kFocal), extent.wide) / depth;
    const int32_t height = low_product(static_cast<int32_t>(kFocal), extent.tall) / depth;
    const int32_t fall = low_product(static_cast<int32_t>(kFocal), drop) / depth;
    const uint32_t first = cpu.read_word(packet + 8u);
    const auto left = static_cast<int16_t>(first), top = static_cast<int16_t>(first >> 16);
    const auto right = static_cast<int16_t>(cpu.read_word(packet + 16u)), bottom = static_cast<int16_t>(cpu.read_word(packet + 24u) >> 16);
    if (width <= 0 || height <= 0 || right - left + 1 != width || bottom - top + 1 != height ||
        top != kHorizon - fall - (height >> 1)) {
        return std::nullopt;
    }
    return Shape{.row = static_cast<int32_t>(kHorizon * kOne) - scaled(drop, depth),
                 .width = scaled(extent.wide, depth),
                 .height = scaled(extent.tall, depth),
                 .shadow = true};
}

[[nodiscard]] Sprite projected(const CPUState& cpu, const Funnel& funnel) {
    Sprite sprite{.depth = static_cast<int32_t>(cpu.gpr[funnel.depth_gpr])};
    const std::optional<int32_t> x = operand(cpu, funnel.x), y = operand(cpu, funnel.y);
    /* $a0 is the screen warp's switch in every funnel: on, 0x8003B6E0 goes on to move the stored point. */
    if (sprite.depth <= 0 || !x || !y || cpu.gpr[4] == kWarped) return sprite;
    const auto across = static_cast<int32_t>(kFocal * static_cast<uint32_t>(*x));
    const auto down = static_cast<int32_t>(kFocal * static_cast<uint32_t>(*y));
    const int32_t column = static_cast<int32_t>(kFocal) + across / sprite.depth;
    if (column < -kFarColumn || column > kFarColumn) return sprite;
    /* $v1 and $v0 still hold what the game made of them: anything else and these are not its operands. */
    if (static_cast<int32_t>(cpu.gpr[3]) != down / sprite.depth ||
        static_cast<int32_t>(cpu.gpr[2]) != psx_ws_project_x(column)) {
        return sprite;
    }
    const auto fraction = static_cast<int32_t>(across % sprite.depth * kOne / sprite.depth);
    const Place place{.x = psx_ws_project_x16(column, fraction),
                      .dy = static_cast<int32_t>(-(down % sprite.depth) * kOne / sprite.depth)};
    sprite.place = place;
    const int32_t row = static_cast<int32_t>((kHorizon - down / sprite.depth) * kOne) + place.dy;
    if (funnel.size == Size::Effect) {
        if (const std::optional<Effect> effect = readable(cpu) ? effect_size(cpu, sprite.depth) : std::nullopt) {
            sprite.shape = Shape{.row = row, .width = effect->wide.exact, .height = effect->tall.exact, .shadow = false};
            sprite.cut = Extent{.wide = effect->wide.cut, .tall = effect->tall.cut};
        }
    } else if (const std::optional<Extent> extent = extent_of(cpu, funnel, sprite.depth)) {
        sprite.shape = Shape{.row = row,
                             .width = scaled(extent->wide, sprite.depth),
                             .height = scaled(extent->tall, sprite.depth),
                             .shadow = false};
    }
    return sprite;
}

/* A funnel's range, as at 0x8003B8C4..0x8003B8D4: the depth and three eighths of the step aside. */
[[nodiscard]] constexpr int64_t range_of(const Funnel& funnel, int32_t depth, int64_t aside) {
    const int64_t range = depth + (3 * aside >> 3);
    return funnel.led ? std::max(range - kBodyLead, kNearest) : range;
}

/* The world renderer ranges a vertex by its squashed column (0x80046360): a sprite's step aside has to count for as little, or the floor under it sorts in front. */
void squash_range(CPUState& cpu, const Funnel& funnel) {
    int32_t num = 1, den = 1;
    gte_ws_x_squash(&num, &den);
    const std::optional<int32_t> x = operand(cpu, funnel.x);
    const auto depth = static_cast<int32_t>(cpu.gpr[funnel.depth_gpr]);
    if (num <= 0 || den <= 0 || !x || depth <= 0) return;
    const int64_t aside = *x < 0 ? -static_cast<int64_t>(*x) : *x;
    if (static_cast<int32_t>(cpu.gpr[funnel.range_gpr]) != range_of(funnel, depth, aside)) return;
    cpu.gpr[funnel.range_gpr] = static_cast<uint32_t>(range_of(funnel, depth, aside * num / den));
}

[[nodiscard]] bool comparator_active() { return g_ls_mode != 0 || g_ls_replay_active != 0; }

/* kBodyReach nearer; under kHalvedBelow halfway to the game's floor of kNearest, so that near bodies keep their order. */
[[nodiscard]] constexpr int64_t led(int64_t range) {
    if (range <= kNearest) return range;
    return range >= kHalvedBelow ? range - kBodyReach : kNearest + (range - kNearest) / 2;
}

[[nodiscard]] constexpr bool overlap(const Box& a, const Box& b) {
    return a.left <= b.right && b.left <= a.right && a.top <= b.bottom && b.top <= a.bottom;
}

/* A body's box from the place and size its funnel worked out, in the packet's own coordinates. */
[[nodiscard]] std::optional<Box> box_of(const Sprite& sprite) {
    if (!sprite.place || !sprite.shape) return std::nullopt;
    const int64_t x = sprite.place->x, row = sprite.shape->row;
    const int64_t half_width = sprite.shape->width / 2, half_height = sprite.shape->height / 2;
    return Box{.left = static_cast<int32_t>((x - half_width) >> 16), .top = static_cast<int32_t>((row - half_height) >> 16),
               .right = static_cast<int32_t>((x + half_width) >> 16), .bottom = static_cast<int32_t>((row + half_height) >> 16)};
}

[[nodiscard]] Box packet_box(const CPUState& cpu, uint32_t packet) {
    Box box{.left = INT32_MAX, .top = INT32_MAX, .right = INT32_MIN, .bottom = INT32_MIN};
    for (uint32_t corner = 8u; corner <= 32u; corner += 8u) {
        const uint32_t xy = cpu.read_word(packet + corner);
        const int32_t x = static_cast<int16_t>(xy), y = static_cast<int16_t>(xy >> 16);
        box = Box{.left = std::min(box.left, x), .top = std::min(box.top, y), .right = std::max(box.right, x), .bottom = std::max(box.bottom, y)};
    }
    return box;
}

/* A world sprite a body passes: the node that stays before it, its address as tags name it, where it is, and its slot. */
struct Passed {
    uint32_t before;
    uint32_t node;
    uint32_t at;
    int64_t slot;
};

/* How far a walk got: the sprites it takes along, and the slot it reached. */
struct Walk {
    std::vector<Passed> sprites;
    int64_t reached;
};

/* A body's camera depth and its reach towards the eye, half its width in the world, both in GTE units, and its middle on the screen. */
struct Front {
    double depth;
    double reach;
    double x;
    double row;
};

[[nodiscard]] std::optional<Front> front_of(const Sprite& sprite) {
    if (sprite.depth <= 0 || !sprite.shape || !sprite.place) return std::nullopt;
    const double depth = static_cast<double>(sprite.depth) * kGteDepthPerSpriteDepth;
    const double width = static_cast<double>(sprite.shape->width) / kOne * sprite.depth / kFocal * kGteDepthPerSpriteDepth;
    return Front{.depth = depth,
                 .reach = width / 2.0,
                 .x = static_cast<double>(sprite.place->x) / kOne,
                 .row = static_cast<double>(sprite.shape->row) / kOne};
}

/* How far `point` lies past the plane through `corner` square to `normal`, on the side away from the eye; negative on its side. */
[[nodiscard]] double past_plane(const std::array<double, 3>& normal, double length, const std::array<double, 3>& corner,
                                const std::array<double, 3>& point) {
    double eye = 0.0, side = 0.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        eye -= normal[axis] * corner[axis];
        side += normal[axis] * (point[axis] - corner[axis]);
    }
    return (eye * side < 0.0 ? 1.0 : -1.0) * std::fabs(side) / length;
}

/* The nearest depth of a triangle (screen place and 1 / z at each corner) over the box, 0 when it is not over it: on a plane
 * 1 / z is linear on the screen, so the nearest point is a corner of the triangle clipped to the box. */
[[nodiscard]] double nearest_over(const std::array<std::array<double, 3>, 3>& triangle, const Box& box) {
    struct Edge {
        std::size_t axis;
        double bound;
        bool least;
    };
    std::vector<std::array<double, 3>> shape(triangle.begin(), triangle.end());
    const std::array<Edge, 4> edges{{{0, static_cast<double>(box.left), true},
                                     {0, box.right + 1.0, false},
                                     {1, static_cast<double>(box.top), true},
                                     {1, box.bottom + 1.0, false}}};
    for (const auto& [axis, bound, least] : edges) {
        const auto inside = [axis, bound, least](const std::array<double, 3>& point) {
            return least ? point[axis] >= bound : point[axis] <= bound;
        };
        std::vector<std::array<double, 3>> kept;
        for (std::size_t index = 0; index < shape.size(); ++index) {
            const std::array<double, 3>& from = shape[index];
            const std::array<double, 3>& to = shape[(index + 1) % shape.size()];
            if (inside(from)) kept.push_back(from);
            if (inside(from) != inside(to)) {
                const double share = (bound - from[axis]) / (to[axis] - from[axis]);
                kept.push_back({from[0] + share * (to[0] - from[0]), from[1] + share * (to[1] - from[1]), from[2] + share * (to[2] - from[2])});
            }
        }
        shape = std::move(kept);
        if (shape.empty()) return 0.0;
    }
    double nearest = 0.0;
    for (const auto& point : shape) nearest = std::max(nearest, point[2]);
    return nearest > 0.0 ? 1.0 / nearest : 0.0;
}

/* How the world polygon at `at` (corners from the GTE) hides the body, nearer than it by more than its reach: an upright face
 * over its box stands in front of it, another over the box's upper half lies over it as a floor over a drone. The lower half
 * is left out, as there the floor before a body lying on it falls. An upright face nearer than the body over the upper half,
 * with the body's middle past its plane, is one the body lies behind (`past`), as a pickup under a shut door. */
enum class Cover { none, front, past, over };

[[nodiscard]] Cover occludes(const CPUState& cpu, uint32_t at, uint32_t tag, const Box& box, const Front& front) {
    const uint32_t code = cpu.read_byte(at + 7u);
    if ((code & 0xE0u) != 0x20u) return Cover::none;
    const bool quad = (code & 0x08u) != 0u, textured = (code & 0x04u) != 0u, gouraud = (code & 0x10u) != 0u;
    const uint32_t corners = quad ? 4u : 3u;
    if (tag >> 24 != 1u + corners * (textured ? 2u : 1u) + (gouraud ? corners - 1u : 0u)) return Cover::none;
    const uint32_t stride = 4u + (textured ? 4u : 0u) + (gouraud ? 4u : 0u);
    int32_t num = 1, den = 1;
    gte_ws_x_squash(&num, &den);
    const double unsquash = num > 0 && den > 0 ? static_cast<double>(den) / num : 1.0;
    std::array<std::array<double, 3>, 4> place{}, camera{};
    for (uint32_t corner = 0; corner < corners; ++corner) {
        const uint32_t word = at + 8u + corner * stride;
        int32_t x16 = 0, y16 = 0;
        uint16_t z = 0;
        if (gte_precision_load_word_ex(word - kKseg0, cpu.read_word(word), &x16, &y16, &z) != GTE_PRECISION_LOOKUP_ACCEPTED || z == 0u) {
            return Cover::none;
        }
        const double x = static_cast<double>(x16) / kOne, y = static_cast<double>(y16) / kOne;
        place[corner] = {x, y, static_cast<double>(z)};
        camera[corner] = {(x - kFocal) * unsquash * z / kFocal, (y - kHorizon) * z / kFocal, static_cast<double>(z)};
    }
    constexpr std::array<std::array<uint32_t, 3>, 2> kTriangles{{{0u, 1u, 2u}, {1u, 3u, 2u}}};
    const Box upper{.left = box.left, .top = box.top, .right = box.right, .bottom = box.top + (box.bottom - box.top) / 2};
    const std::array<double, 3> middle{(front.x - kFocal) * unsquash * front.depth / kFocal, (front.row - kHorizon) * front.depth / kFocal,
                                       front.depth};
    Cover found = Cover::none;
    for (uint32_t triangle = 0; triangle < (quad ? 2u : 1u); ++triangle) {
        const auto& [a, b, c] = kTriangles[triangle];
        std::array<double, 3> u{}, v{};
        for (int axis = 0; axis < 3; ++axis) {
            u[axis] = camera[b][axis] - camera[a][axis];
            v[axis] = camera[c][axis] - camera[a][axis];
        }
        const std::array<double, 3> normal{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const double length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length == 0.0) continue;
        const bool upright = std::fabs(normal[1]) < kUpright * length;
        const auto over = [&](const Box& part) {
            return nearest_over({{{place[a][0], place[a][1], 1.0 / place[a][2]},
                                  {place[b][0], place[b][1], 1.0 / place[b][2]},
                                  {place[c][0], place[c][1], 1.0 / place[c][2]}}},
                                part);
        };
        const double nearest = over(upright ? box : upper);
        if (upright && nearest > 0.0 && past_plane(normal, length, camera[a], middle) > 0.0) {
            const double above = over(upper);
            if (above > 0.0 && above < front.depth) return Cover::past;
        }
        if (found == Cover::none && nearest > 0.0 && nearest < front.depth - front.reach) found = upright ? Cover::front : Cover::over;
    }
    return found;
}

/* A funnel's POLY_FT4: the CPU placed its corners, so the GTE kept no projection for them, as it does for the world's. */
[[nodiscard]] bool world_sprite(const CPUState& cpu, uint32_t at, uint32_t tag) {
    if (tag >> 24 != kSpriteWords || (cpu.read_byte(at + 7u) & kSpriteCodeMask) != kSpriteCode) return false;
    int32_t x16 = 0, y16 = 0;
    uint16_t z = 0;
    return gte_precision_load_word_ex(at + 8u - kKseg0, cpu.read_word(at + 8u), &x16, &y16, &z) != GTE_PRECISION_LOOKUP_ACCEPTED;
}

/* The world sprites over `box` along the table's chain from slot `from` down to slot `to`, in the order they are drawn,
 * and with `chain` every node on the way. When `may_stop` an upright face in front of the body ends the walk in its slot,
 * the sprites of that slot staying where they are, and a face over it ends it where it began; otherwise, or when the chain
 * does not reach `to`, nothing. */
[[nodiscard]] std::optional<Walk> passed_sprites(const CPUState& cpu, uint32_t table, int64_t from, int64_t to, const Box& box,
                                                 const Front& front, bool may_stop, std::vector<uint32_t>* chain = nullptr) {
    const uint32_t first = table + static_cast<uint32_t>(from) * kSlotBytes;
    const uint32_t last = table + static_cast<uint32_t>(to) * kSlotBytes;
    std::vector<Passed> passed;
    uint32_t before = first;
    uint32_t at = first;
    int64_t slot = from;
    if (chain) chain->assign(1, first);
    for (int link = 0; link < kLinksWalked; ++link) {
        const uint32_t node = cpu.read_word(at) & kTagNext;
        if (node == kTagNext) return std::nullopt;
        /* The DMA's own reading of a node address: folded into RAM, or in the render arena past it. */
        at = kKseg0 | psx_mod_gpu_dma_resolve_address(node);
        if (at == last) return Walk{.sprites = passed, .reached = to};
        if (chain) chain->push_back(at);
        if (at >= table && at < first) {
            before = at;
            slot = (at - table) / kSlotBytes;
            continue;
        }
        const uint32_t tag = cpu.read_word(at);
        if (world_sprite(cpu, at, tag)) {
            if (overlap(packet_box(cpu, at), box)) {
                if (passed.size() == kMovedMost) return std::nullopt;
                passed.push_back(Passed{.before = before, .node = node, .at = at, .slot = slot});
                continue;
            }
        } else if (const Cover cover = occludes(cpu, at, tag, box, front); cover != Cover::none) {
            if (!may_stop) return std::nullopt;
            if (cover == Cover::over) return Walk{.sprites = {}, .reached = from};
            std::erase_if(passed, [slot](const Passed& sprite) { return sprite.slot == slot; });
            return Walk{.sprites = passed, .reached = slot};
        }
        before = at;
    }
    return std::nullopt;
}

/* The slot of the farthest face linked from slot `from` down to the body's slot `to` that the body lies behind: the game's own
 * lead sorted the body in front of it. Nothing when there is none or the chain does not reach `to`. */
[[nodiscard]] std::optional<int64_t> face_behind(const CPUState& cpu, uint32_t table, int64_t from, int64_t to, const Box& box,
                                                 const Front& front) {
    const uint32_t first = table + static_cast<uint32_t>(from) * kSlotBytes;
    const uint32_t last = table + static_cast<uint32_t>(to) * kSlotBytes;
    uint32_t at = first;
    int64_t slot = from;
    std::optional<int64_t> behind;
    for (int link = 0; link < kLinksWalked; ++link) {
        const uint32_t node = cpu.read_word(at) & kTagNext;
        if (node == kTagNext) return std::nullopt;
        at = kKseg0 | psx_mod_gpu_dma_resolve_address(node);
        if (at == last) return behind;
        if (at >= table && at < first) {
            slot = (at - table) / kSlotBytes;
            continue;
        }
        const uint32_t tag = cpu.read_word(at);
        if (!behind && !world_sprite(cpu, at, tag) && occludes(cpu, at, tag, box, front) == Cover::past) behind = slot;
    }
    return std::nullopt;
}

[[nodiscard]] bool writable(const CPUState& cpu) { return readable(cpu) && cpu.write_half && cpu.write_word; }

void unlink(CPUState& cpu, const std::vector<Passed>& passed) {
    for (const Passed& sprite : passed) {
        const uint32_t before = cpu.read_word(sprite.before);
        cpu.write_word(sprite.before, (before & kTagWords) | (cpu.read_word(sprite.at) & kTagNext));
    }
}

/* Links `sprites` at the head of `entry`, the first of them drawn first. */
void head_of(CPUState& cpu, uint32_t entry, const std::vector<Passed>& sprites) {
    for (auto sprite = sprites.rbegin(); sprite != sprites.rend(); ++sprite) {
        const uint32_t head = cpu.read_word(entry);
        cpu.write_word(sprite->at, (cpu.read_word(sprite->at) & kTagWords) | (head & kTagNext));
        cpu.write_word(entry, (head & kTagWords) | sprite->node);
    }
}

/* The world sprites the last record took along, at the head of its new slot until its shadow comes. */
std::vector<Passed> g_moved;

/* Moves the world sprites over `box` that a range going from `key` to `nearer` passes to the head of the slot of `nearer`,
 * in the order they are drawn, so they stay over it. False, and nothing moves, when the chain cannot be walked there. */
[[nodiscard]] std::optional<int64_t> move_over(CPUState& cpu, int64_t key, int64_t nearer, const Box& box, const Front& front) {
    const uint32_t table = cpu.read_word(cpu.gpr[kGlobals] + kOrderingTable);
    const std::optional<Walk> walk = passed_sprites(cpu, table, key >> kSlotShift, nearer >> kSlotShift, box, front, true);
    if (!walk) return std::nullopt;
    unlink(cpu, walk->sprites);
    head_of(cpu, table + static_cast<uint32_t>(walk->reached) * kSlotBytes, walk->sprites);
    g_moved = walk->sprites;
    return walk->reached;
}

/* The shadow's own world sprites join the body's at the head of the new slot, all in the order the game drew them:
 * a body's sprite went right after the node it followed, which is still on the chain. */
[[nodiscard]] bool join_over(CPUState& cpu, int64_t key, int64_t nearer, const Box& box, const Front& front) {
    const uint32_t table = cpu.read_word(cpu.gpr[kGlobals] + kOrderingTable);
    const uint32_t entry = table + static_cast<uint32_t>(nearer >> kSlotShift) * kSlotBytes;
    std::vector<uint32_t> chain;
    const std::optional<Walk> walk = passed_sprites(cpu, table, key >> kSlotShift, nearer >> kSlotShift, box, front, false, &chain);
    if (!walk) return false;
    const std::vector<Passed>* const passed = &walk->sprites;
    if (passed->empty()) return true;
    uint32_t rest = cpu.read_word(entry) & kTagNext;
    for (const Passed& sprite : g_moved) {
        if (rest != sprite.node) return false;
        rest = cpu.read_word(sprite.at) & kTagNext;
    }
    const auto place = [&chain](uint32_t at) { return std::find(chain.begin(), chain.end(), at) - chain.begin(); };
    std::vector<std::pair<std::array<int64_t, 3>, Passed>> order;
    for (std::size_t index = 0; index < g_moved.size(); ++index) {
        const int64_t after = place(g_moved[index].before);
        if (after == static_cast<int64_t>(chain.size())) return false;
        order.push_back({{after, 1, static_cast<int64_t>(index)}, g_moved[index]});
    }
    for (const Passed& sprite : *passed) order.push_back({{place(sprite.at), 0, 0}, sprite});
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    unlink(cpu, *passed);
    cpu.write_word(entry, (cpu.read_word(entry) & kTagWords) | rest);
    std::vector<Passed> sprites;
    for (const auto& [_, sprite] : order) sprites.push_back(sprite);
    head_of(cpu, entry, sprites);
    return true;
}

/* The frame's records are 0 to `last` (the game counts them from 0 each frame, 0x80040EC8): a led key is never nearer than
 * one the game put nearer, and only ever goes farther to stay so, which no wall forbids. A body taken behind a face orders the
 * rest by its game key, so a body in front of that face stays in front of it. */
void keep_order(CPUState& cpu, std::size_t last, uint32_t first_key) {
    std::vector<std::size_t> order(last + 1);
    for (std::size_t index = 0; index <= last; ++index) order[index] = index;
    std::stable_sort(order.begin(), order.end(), [](std::size_t a, std::size_t b) { return g_record[a].key < g_record[b].key; });
    int32_t floor = INT32_MIN, behind = INT32_MIN;
    for (const std::size_t index : order) {
        Sprite& record = g_record[index];
        const int32_t least = record.key > behind && floor != INT32_MIN ? floor + 1 : floor;
        if (record.led < least) {
            record.led = least;
            cpu.write_half(first_key + kRecordBytes * static_cast<uint32_t>(index), static_cast<uint16_t>(least));
        }
        floor = std::max(floor, std::min(record.led, record.key));
        behind = record.key;
    }
}

/* Where a body's walk takes its key from `key`: back to the slot of a face it lies behind that the game sorted farther, or
 * nearer by led(), the world sprites over it moved along, held by a wall in front of it. The game's key when the walk cannot
 * be made or does not get there. */
[[nodiscard]] int32_t walk_body(CPUState& cpu, int32_t key, const Sprite& record) {
    const auto nearer = static_cast<int32_t>(led(key));
    const std::optional<Box> box = box_of(record);
    const std::optional<Front> front = front_of(record);
    const int64_t own = key >> kSlotShift;
    if (box && front && own < kLastSlot) {
        const uint32_t table = cpu.read_word(cpu.gpr[kGlobals] + kOrderingTable);
        if (const std::optional<int64_t> behind = face_behind(cpu, table, std::min(own + kFartherSlots, kLastSlot), own, *box, *front)) {
            return static_cast<int32_t>(*behind << kSlotShift | (key & ((1 << kSlotShift) - 1)));
        }
    }
    if (nearer >> kSlotShift == own) return nearer;
    if (!box || !front) return key;
    const std::optional<int64_t> reached = move_over(cpu, key, nearer, *box, *front);
    if (!reached) return key;
    return *reached == nearer >> kSlotShift ? nearer : static_cast<int32_t>(*reached << kSlotShift | (key & ((1 << kSlotShift) - 1)));
}

/* After `sh $s7, record.key`: the deferred pass cuts its list and links by the key, so both move with it. */
void lead_record(CPUState& cpu, std::size_t index, Sprite& record) {
    if (!writable(cpu)) return;
    const uint32_t at = cpu.gpr[1] + static_cast<uint32_t>(static_cast<int16_t>(kRecordWord & 0xFFFFu));
    const auto key = static_cast<int16_t>(cpu.read_half(at));
    g_moved.clear();
    record.key = key;
    record.walked = record.led = walk_body(cpu, key, record);
    if (record.led != key) cpu.write_half(at, static_cast<uint16_t>(record.led));
    keep_order(cpu, index, at - kRecordBytes * static_cast<uint32_t>(index));
}

/* The shadow, sorted by the same range before the lead, goes where its body's walk went, with the world sprites over it;
 * a body kept in order farther than that is then drawn under its shadow, as the game draws them in one slot. */
void lead_shadow(CPUState& cpu, const Sprite& body, uint32_t packet) {
    uint32_t& range = cpu.gpr[kFunnels.back().range_gpr];
    const std::optional<Front> front = front_of(body);
    if (body.walked == body.key || static_cast<int32_t>(range) != body.key || !front || !writable(cpu)) return;
    if (body.walked < body.key && body.walked >> kSlotShift != body.key >> kSlotShift &&
        !join_over(cpu, body.key, body.walked, packet_box(cpu, packet), *front)) {
        return;
    }
    range = static_cast<uint32_t>(body.walked);
}

[[nodiscard]] constexpr int32_t fade_row(int32_t key, int32_t start) {
    return std::clamp(key - start, 0, kFadeLast) >> kFadeStep;
}

/* The pass faded the body by its led key (0x80043190..0x8004331C): its CLUT goes back to the row of the game's key. */
void restore_fade(CPUState& cpu, const Sprite& record, uint32_t packet) {
    if (record.led == record.key || static_cast<int32_t>(cpu.gpr[kRecordKey]) != record.led || !cpu.read_word || !cpu.read_half ||
        !cpu.write_half) {
        return;
    }
    int32_t rows = 0;
    if (!disruptor_far_rendering_list_fade_rows(packet, record.led, record.key, &rows)) {
        const auto start = static_cast<int32_t>(cpu.read_word(cpu.gpr[kGlobals] + kFadeStart));
        rows = fade_row(record.key, start) - fade_row(record.led, start);
    }
    if (rows == 0) return;
    const uint32_t at = cpu.gpr[kRecordClut] + kClutStore;
    cpu.write_half(at, static_cast<uint16_t>(cpu.read_half(at) + (rows << kClutRow)));
}

[[nodiscard]] Sprite take_pending(uint32_t projection) {
    const Sprite sprite = g_pending_projection == projection ? g_pending : Sprite{};
    g_pending_projection = 0;
    g_pending = Sprite{};
    return sprite;
}

}  // namespace

extern "C" void disruptor_sprite_depth_instruction_hook(
        CPUState *cpu, uint32_t address, uint32_t instruction, int phase) {
    if (!cpu || phase != 1) return;
    if (address == kRecordSite) {
        const uint32_t offset = cpu->gpr[3];
        const std::size_t index = offset / kRecordBytes;
        if (instruction != kRecordWord || offset % kRecordBytes != 0u ||
            index >= g_record.size()) {
            return;
        }
        g_record[index] =
            g_pending_projection == kDeferredProjection ? g_pending : Sprite{};
        if (!comparator_active()) lead_record(*cpu, index, g_record[index]);
        g_pending.key = g_record[index].key;
        g_pending.walked = g_record[index].walked;
        return;
    }
    if (instruction != kProjectWord) return;
    for (const Funnel& funnel : kFunnels) {
        if (funnel.projection != address) continue;
        g_pending_projection = address;
        g_pending = projected(*cpu, funnel);
        squash_range(*cpu, funnel);
        return;
    }
}

extern "C" void disruptor_sprite_depth_packet(
        CPUState *cpu, uint32_t packet_site, uint32_t packet) {
    if (!cpu) return;
    Sprite sprite{};
    if (packet_site == kDeferredPacketSite) {
        const std::size_t index = cpu->gpr[22];
        if (index < g_record.size()) {
            sprite = g_record[index];
            g_record[index] = Sprite{};
        }
        if (!comparator_active()) restore_fade(*cpu, sprite, packet);
    } else {
        for (const Funnel& funnel : kFunnels) {
            if (funnel.packets[0] == packet_site || funnel.packets[1] == packet_site) {
                sprite = take_pending(funnel.projection);
                break;
            }
        }
    }
    if (sprite.cut && (packet_site != kEffectPacketSite || static_cast<int32_t>(cpu->gpr[22]) != sprite.cut->wide ||
                       static_cast<int32_t>(cpu->gpr[18]) != sprite.cut->tall)) {
        sprite.shape.reset();
    }
    if (packet_site == kShadowPacketSite && !comparator_active()) lead_shadow(*cpu, sprite, packet);
    if (packet_site == kShadowPacketSite && sprite.place) {
        /* The row and the size so far are the caster's. */
        sprite.place->dy = 0;
        sprite.shape = readable(*cpu) ? shadow_shape(*cpu, packet, sprite.depth) : std::nullopt;
    }
    if (sprite.depth <= 0 || sprite.depth > INT32_MAX / kGteDepthPerSpriteDepth) return;
    gpu_temporal_note_sprite(cpu, packet, sprite.depth * kGteDepthPerSpriteDepth);
    if (packet_site == kFunnels[2].packets[0] || packet_site == kFunnels[2].packets[1]) {
        gpu_temporal_effect_sprite(packet);
        const float gain = readable(*cpu) ? disruptor::shot_lights::gain(CpuMemory{*cpu}, cpu->gpr[kShotRecordGpr]) : 1.0f;
        if (gain != 1.0f) gpu_temporal_light_sprite(packet, gain);
    }
    if (!sprite.place) return;
    gpu_temporal_place_sprite(packet, sprite.place->x, sprite.place->dy);
    if (sprite.shape) {
        gpu_temporal_size_sprite(packet, sprite.shape->row, sprite.shape->width, sprite.shape->height,
                                 sprite.shape->shadow, packet_site == kDeferredPacketSite);
    }
}
