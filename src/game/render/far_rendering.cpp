/*
 * Far rendering for Disruptor (SLUS-00224), the saved draw distance.
 *
 * The game keeps its far plane and the beginning of its depth fade in two
 * resident globals.  For one main-renderer call the extension writes the
 * preset's values into them, so every reader inside the renderer gets them,
 * the hand-written mesh renderer at 0x80046134 too, which reads both by
 * absolute address.  The screen effects between the traversal and the visible
 * loop store the globals themselves, so they run on the game's values and the
 * preset's are written again after them.  The game's values go back at the renderer's epilogue,
 * before a save state is taken and at the next entry after a missed epilogue,
 * so play, memory cards and save states keep them.
 *
 * Both seams are pinned by address (and, at the exit, opcode).  The renderer
 * context, gameplay state, globals, and primitive pointer are validated before
 * the globals are written.  An unbalanced/nested entry is cleaned up and the
 * new invocation is rejected, so a missed seam fails closed.
 */

#include "game/render/far_rendering.h"

#include "game/guest.h"
#include "game/render/ice_winding.h"
#include "game/render/marked_corners.h"
#include "game/render/misplaced_riser.h"
#include "game/render/near_split.h"
#include "game/render/portal_reach.h"
#include "game/render/rooftops_houses.h"
#include "game/render/quad_visibility.h"
#include "game/render/sector_reach.h"

#include "cpu_state.h"
#include "func_override.h"
#include "game/session/weapon_load.h"
#include "gpu.h"
#include "gpu_ws_tag_match.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "mod_memory.h"
#include "psx_netplay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

/* render_arena.cpp: the renderer's two fades take the distances drawn with. */
extern "C" void disruptor_render_arena_publish_fades(void);
extern "C" int disruptor_render_arena_relocated(void);
extern "C" int gl_renderer_smooth_fade(void);

namespace {

constexpr uint32_t kRendererEntry = 0x80040E68u;
constexpr uint32_t kPlayerSectorAddress = 0x80077630u;
constexpr uint8_t kConsole02Sector = 0xE5u;
constexpr uint32_t kDrawSector = 0x80046134u;  /* $a1: the span's left at +12, its right at +16, words */
constexpr uint32_t kRendererRestore = 0x8004279Cu;
constexpr uint32_t kRendererRestoreOpcode = 0x8FBF016Cu;
constexpr uint32_t kSharedSides = 0x80046C18u;  /* at the sides a face's corners share, t0..t3 the corner records */
constexpr uint32_t kSharedSidesOpcode = 0x3021003Fu;
constexpr uint32_t kSpanLeft = 0x1F80004Cu;
constexpr uint32_t kSpanRight = 0x1F800050u;
constexpr uint32_t kNearSplitCentre = 0x8004791Cu;  /* the middle point stored, a1 at it, corners in t0..t3 */
constexpr uint32_t kNearSplitCentreOpcode = 0xACAD0004u;
constexpr uint32_t kQuarterCorner0Uv = 0x80047AD0u;  /* a quarter's last texture word stored, t9 at its packet */
constexpr uint32_t kQuarterCorner1Uv = 0x80047B90u;
constexpr uint32_t kQuarterCorner3Uv = 0x80047D18u;
constexpr uint32_t kQuarterUvOpcode = 0xAF290030u;

constexpr uint32_t kFarDistance = 0x800715ECu;
constexpr uint32_t kFadeStart = 0x800715E0u;
constexpr uint32_t kPrimitivePointer = 0x800714DCu; /* $gp + 0x390, the next packet: the mesh renderer loads $t9 from it at 0x80046B50, stores it at 0x800470CC */
constexpr uint32_t kRendererPrimitiveStartOffset = 0x70u;

constexpr uint32_t kTraversalDepthLoad = 0x8003A0F8u;
constexpr uint32_t kTraversalDepthLoadOpcode = 0x93B300C0u;
constexpr uint32_t kTraversalRoomMark = 0x8003A390u;
constexpr uint32_t kTraversalRoomMarkOpcode = 0xA082001Fu;
constexpr uint32_t kTraversalDepthCap = 11u;

constexpr uint32_t kPortalFirstCandidate = 0x8003A92Cu;
constexpr uint32_t kPortalFirstCandidateOpcode = 0x00005812u;
constexpr uint32_t kPortalSecondCandidate = 0x8003A964u;
constexpr uint32_t kPortalSecondCandidateOpcode = 0x00681021u;
constexpr uint32_t kPortalFinalCandidate = 0x8003AA74u;
constexpr uint32_t kPortalFinalCandidateOpcode = 0x00AC1021u;

constexpr uint32_t kAfterTraversal = 0x800410E0u;
constexpr uint32_t kAfterTraversalOpcode = 0x93830310u;
constexpr uint32_t kBeforeVisibleLoop = 0x800410F8u;
constexpr uint32_t kBeforeVisibleLoopOpcode = 0x8F820680u;
constexpr uint32_t kAfterVisibleLoop = 0x800411D8u;
constexpr uint32_t kAfterVisibleLoopOpcode = 0x8F8301D4u;
constexpr uint32_t kVisibleSpanBase = 0x80079D18u;
constexpr uint32_t kVisibleSpanStride = 8u;
constexpr uint32_t kVisibleEnd = 0x800717CCu; /* $gp + 0x680 */
constexpr uint32_t kRoomObjects = 0x8003B78Cu;     /* the visible loop's first call after a room's mesh */

constexpr uint32_t kCurrentHealth = 0x80077660u;
constexpr uint32_t kMaximumHealth = 0x80077664u;
constexpr uint32_t kSceneMode = 0x8007145Cu;
constexpr uint32_t kPlayerPointer = 0x80071714u;
constexpr uint32_t kPlayerInactiveOffset = 0x7Fu;

constexpr uint32_t kRamFirst = 0x80000000u;
constexpr uint32_t kRamLast = 0x801FFFFFu;
constexpr uint32_t kMinimumPlausibleFar = 64u;
constexpr uint32_t kMaximumPlausibleSourceFar = 8192u;
/* The mesh emitter at 0x80046D64 indexes its 512-entry ordering table by (depth + up to 240) / 8, depth up to far + 160. */
constexpr uint32_t kMaximumSafeEffectiveFar = 3688u;
/* Far units of mesh kept past the game's own cull at far + 160, where the world has dissolved: more than the deepest polygon of six levels, 345. */
constexpr uint32_t kEdgeMargin = 512u;
/* Near the ordering table's limit the margin gives way down to this before the far seen does. */
constexpr uint32_t kLeastEdgeMargin = 256u;
constexpr uint32_t kPortalFarLoad = 0x8003A914u;
constexpr int64_t kMaximumSafeFade = 32767;
constexpr size_t kTimingHistory = 256u;

using Clock = std::chrono::steady_clock;

struct ActiveFrame {
    bool active = false;
    uint32_t primitive_start = 0u;
    Clock::time_point started{};

    uint32_t traversal_rooms = 0u;
    uint32_t traversal_max_depth = 0u;
    bool traversal_depth_seen = false;

    bool visible_spans_valid = false;
    uint32_t submitted_spans = 0u;

    bool packet_sequence_valid = true;
    bool saw_after_traversal = false;
    bool saw_before_visible_loop = false;
    bool saw_after_visible_loop = false;
    uint32_t primitive_after_traversal = 0u;
    uint32_t primitive_before_visible_loop = 0u;
    uint32_t primitive_after_visible_loop = 0u;

    uint32_t entrance_reach = 0u;
    uint32_t entrance_mesh_start = 0u;
    uint32_t entrance_saved_far = 0u;
    uint32_t entrance_tagged_end = 0u;
    double entrance_visibility = 0.0;
    bool rewrite_after_effects = false;
    bool extended = false;  /* written at some point of this frame: a save waits for its end */
    bool reach = false;  /* the walk's wedge widens and the rooms' spans open for the visible loop, see portal_reach.h */
};

enum class PendingDecisionKind : uint8_t {
    None,
    Portal,
    Object,
};

struct PendingFarDecision {
    PendingDecisionKind kind = PendingDecisionKind::None;
    uint32_t source_far = 0u;
    uint32_t effective_far = 0u;
    uint32_t expected_pc = 0u;
};

enum class DistanceKind : uint8_t {
    Far,
    Fade,
};

struct DistanceLoadSite {
    uint32_t pc;
    uint32_t opcode;
    DistanceKind kind;
    uint16_t saved_ra_offset;
    std::array<uint32_t, 3> allowed_return_addresses;
    uint8_t allowed_return_count;
};

/*
 * These are the reviewed draw-distance/fade loads in the live main-renderer
 * paths and their downstream world-billboard flush, observed for the
 * diagnostics.  Several underlying helpers are callable elsewhere, so each
 * load additionally validates the helper's saved $ra at its exact frame offset.
 */
constexpr auto kDistanceLoadSites = std::array<DistanceLoadSite, 13>{{
    {0x8003A914u, 0x8F8C04A0u, DistanceKind::Far, 164u,
     {0x800410E0u, 0x8003AE54u, 0x8003AF1Cu}, 3u},
    {0x8003B8C8u, 0x8F8304A0u, DistanceKind::Far, 60u,
     {0x800411ACu, 0u, 0u}, 1u},
    {0x8003BCE0u, 0x8F8204A0u, DistanceKind::Far, 60u,
     {0x800411B4u, 0u, 0u}, 1u},
    {0x8003C168u, 0x8F8304A0u, DistanceKind::Far, 84u,
     {0x800411BCu, 0u, 0u}, 1u},
    {0x8003CD64u, 0x8F8304A0u, DistanceKind::Far, 60u,
     {0x800411C4u, 0u, 0u}, 1u},
    {0x8003B2CCu, 0x8F820494u, DistanceKind::Fade, 156u,
     {0x8004106Cu, 0x8003A168u, 0u}, 2u},
    {0x8003BA30u, 0x8F820494u, DistanceKind::Fade, 60u,
     {0x800411ACu, 0u, 0u}, 1u},
    {0x8003BE40u, 0x8F820494u, DistanceKind::Fade, 60u,
     {0x800411B4u, 0u, 0u}, 1u},
    {0x8003C628u, 0x8F820494u, DistanceKind::Fade, 84u,
     {0x800411BCu, 0u, 0u}, 1u},
    {0x8003C878u, 0x8F820494u, DistanceKind::Fade, 84u,
     {0x800411BCu, 0u, 0u}, 1u},
    {0x8003D28Cu, 0x8F820494u, DistanceKind::Fade, 60u,
     {0x800411C4u, 0u, 0u}, 1u},
    {0x8003FB0Cu, 0x8F840494u, DistanceKind::Fade, 188u,
     {0x8004122Cu, 0u, 0u}, 1u},
    {0x80043184u, 0x8F820494u, DistanceKind::Fade, 68u,
     {0x80043FE0u, 0u, 0u}, 1u},
}};

/* Every fade load and how its code finds the range it fades by. Each subtracts the fade start from that range and
 * works in the game's 768 units: twelve palette rows, or a shade that goes out and the thing with it. */
enum class RangeFrom : uint8_t {
    Register,
    HalfRegister,  /* the backdrop halves a sum after the load */
    WeaponDepth,   /* (30 - v0) * 30 + 128, worked out after the load */
};
struct FogSite {
    uint32_t pc;
    RangeFrom from;
    uint8_t range_register;
    bool rows;                /* one sprite a packet, its row (range - start) / 64: the renderer blends its fraction */
    bool packet_in_register;  /* drawn after the renderer from a list, its packet in s5 */
};
constexpr uint32_t kSkyFogSite = 0x8003B2CCu; /* a sky opening's fade step */
constexpr auto kFogSites = std::array<FogSite, 8>{{
    {kSkyFogSite, RangeFrom::HalfRegister, 3u, false, false},
    {0x8003BA30u, RangeFrom::Register, 18u, true, false},
    {0x8003BE40u, RangeFrom::Register, 19u, true, false},
    {0x8003C628u, RangeFrom::Register, 23u, true, false},
    {0x8003C878u, RangeFrom::Register, 23u, false, false},
    {0x8003D28Cu, RangeFrom::Register, 23u, false, false},
    {0x8003FB0Cu, RangeFrom::WeaponDepth, 2u, false, false},
    {0x80043184u, RangeFrom::Register, 20u, true, true},
}};
constexpr uint32_t kListPacketRegister = 21u;
constexpr int32_t kFadeRows = 12;
constexpr int32_t kFadeRowDepth = 64;
constexpr int32_t kFadeDepth = kFadeRows * kFadeRowDepth;

struct ObjectDecisionSite {
    uint32_t load_pc;
    uint32_t decision_pc;
    uint32_t decision_opcode;
    uint8_t result_register;
    uint8_t threshold_register;
};

/* Each listed SLT is the direct far-distance rejection feeding the following
 * conditional branch.  The one-shot token armed at its matching reviewed
 * load prevents an unrelated invocation of the helper from being observed. */
constexpr auto kObjectDecisionSites = std::array<ObjectDecisionSite, 4>{{
    {0x8003B8C8u, 0x8003B8D8u, 0x0072182Au, 3u, 18u},
    {0x8003BCE0u, 0x8003BCECu, 0x0053102Au, 2u, 19u},
    {0x8003C168u, 0x8003C178u, 0x0077182Au, 3u, 23u},
    {0x8003CD64u, 0x8003CD70u, 0x0077182Au, 3u, 23u},
}};

struct SessionDiagnostics {
    uint64_t renderer_entries = 0u;
    uint64_t completed_frames = 0u;
    uint64_t substituted_loads = 0u;
    uint64_t far_load_substitutions = 0u;
    uint64_t fade_load_substitutions = 0u;
    uint64_t rejected_entries = 0u;
    uint64_t nested_entries = 0u;
    uint64_t missed_epilogues = 0u;
    uint64_t invalid_globals = 0u;
    uint64_t primitive_samples = 0u;

    uint64_t traversal_recursion_entries = 0u;
    uint64_t traversal_cap_hits = 0u;
    uint64_t portal_far_tests = 0u;
    uint64_t portal_shortcut_relaxations = 0u;
    uint64_t portal_final_decisions = 0u;
    uint64_t portal_final_effective_rejections = 0u;
    uint64_t portal_final_decision_flips = 0u;
    uint64_t object_far_decisions = 0u;
    uint64_t object_effective_rejections = 0u;
    uint64_t object_decision_flips = 0u;
    uint64_t observer_sequence_errors = 0u;
    uint64_t visibility_samples = 0u;
    uint64_t packet_stage_samples = 0u;

    uint32_t source_far = 0u;
    int32_t source_fade = 0;
    uint32_t effective_far = 0u;
    int32_t effective_fade = 0;
    uint32_t primitive_start = 0u;
    uint32_t primitive_end = 0u;
    uint32_t primitive_last_delta = 0u;
    uint32_t primitive_high_water = 0u;

    uint32_t traversal_rooms_last = 0u;
    uint32_t traversal_rooms_high_water = 0u;
    uint32_t submitted_spans_last = 0u;
    uint32_t submitted_spans_high_water = 0u;
    uint32_t traversal_max_depth_last = 0u;
    uint32_t traversal_max_depth_high_water = 0u;
    uint32_t packet_entry_to_traversal_end_last = 0u;
    uint32_t packet_traversal_to_visible_start_last = 0u;
    uint32_t packet_visible_loop_last = 0u;
    uint32_t packet_post_visible_loop_last = 0u;

    uint64_t sector_requests_held = 0u;
    uint64_t portal_model_mismatches = 0u;

    std::array<double, kTimingHistory> render_wall_us{};
    size_t render_wall_us_count = 0u;
    size_t render_wall_us_next = 0u;
    double render_wall_us_max = 0.0;
};

std::mutex g_mutex;
int g_preset = DISRUPTOR_FAR_RENDERING_RETAIL;
int g_depth_fade_mode = DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL;
ActiveFrame g_frame;
PendingFarDecision g_pending_decision;
SessionDiagnostics g_diagnostics;
disruptor::sector_reach::Walk g_walk;
disruptor::portal_reach::Spans g_spans;
disruptor::portal_reach::Sight g_sight;
/* The faces of the room being drawn that only the wider walk admits: a room the game's own walk does not list, or a face
 * its own span drops. Set and read on the emulation thread, at a room's mesh and at each face. */
struct FaceRule {
    bool active = false, whole = false;
    int32_t left = 0, right = 0;
};
FaceRule g_face_rule;
uint32_t g_admitted_from = 0u;
disruptor::portal_reach::Span g_admitted_band = disruptor::portal_reach::kWholeFace;
disruptor::portal_reach::Objects g_objects;
disruptor::portal_reach::Beside g_beside;
/* A room the wider walk listed, shown unlisted to the walk's test of a portal seen edge-on (0x8003AC14). */
struct Unlisted {
    uint32_t room = disruptor::sector_reach::kNoSector;
    uint8_t entry = 0u;
} g_unlisted;
std::array<Unlisted, disruptor::portal_reach::kMostRooms> g_unlisted_mesh_rooms;
uint32_t g_unlisted_mesh_count = 0u;

/* Active only inside an active frame: every way a frame ends gives the values back. */
struct WrittenDistances {
    bool active = false;
    uint32_t source_far = 0u;
    int32_t source_fade = 0;
    uint32_t effective_far = 0u;
    int32_t effective_fade = 0;
    uint32_t visual_far = 0u;  /* where the fog ends: the written far less the edge margin */
};
WrittenDistances g_written;

/* The fog of the last frame the renderer drew in play, kept to the next entry: the list of enemies is drawn after it. */
struct FrameFog {
    bool active = false;
    bool smooth = false;     /* the renderer's smooth fade draws it: the ramp is stretched and shaped, sprites blend rows */
    int32_t start = 0;       /* where the fog begins */
    int32_t length = 768;    /* how deep it is, the game's twelve rows spread over it */
    int exponent = 1;        /* the fog grows with this power of the depth into it */
};
FrameFog g_fog;
int g_fog_exponent = 1;
uint32_t g_fog_serial = 0u;
struct PacketFraction {
    uint32_t serial;
    int fade;
    bool drawn = false;       /* the packet's words hash to `signature` since its first draw */
    uint32_t signature = 0u;
    uint32_t arena = 0u;
};
std::unordered_map<uint32_t, PacketFraction> g_packet_fractions;
uint32_t g_list_shaped = 0u;  /* the list body whose fade load took the frame's fog, by its packet */

bool comparator_active() {
    /* The comparator invokes compiled RECORD and interpreted REPLAY halves.
     * Neither half may alter host state or guest RAM for this experiment. */
    return g_ls_mode != 0 || g_ls_replay_active != 0;
}

bool valid_main_ram_pointer(uint32_t pointer, uint32_t trailing_bytes) {
    return pointer >= kRamFirst && pointer <= kRamLast &&
           trailing_bytes <= kRamLast - pointer;
}

void repair_pipe_elbow(CPUState *cpu, uint32_t mesh) {
    constexpr uint32_t elbow_part = 0x4A8u, face = elbow_part + 0x7F4u;
    if ((mesh & 3u) || !valid_main_ram_pointer(mesh, face + 15u) || cpu->read_word(mesh) != 0x1CC0Au ||
        cpu->read_word(mesh + elbow_part) != 0xEB8FFu || cpu->read_word(mesh + elbow_part + 4u) != 0x160u) return;
    const uint32_t first = cpu->read_word(mesh + face), second = cpu->read_word(mesh + face + 4u);
    if (first != 0x0A43016Cu || second != 0x12827000u ||
        cpu->read_word(mesh + face + 8u) != 0x09C2A330u || cpu->read_word(mesh + face + 12u) != 0x1244A200u) return;
    // Transposing corners B and D aligns the ribs and keeps the same diagonal with the opposite winding.
    constexpr uint32_t corner_b = 0x003FF000u, pipe_tile = 205u;
    cpu->write_word(mesh + face, (first & 0xFFC00003u) | (second & corner_b) | (pipe_tile << 2));
    cpu->write_word(mesh + face + 4u, (second & ~corner_b) | (first & corner_b) |
                    disruptor::quad_visibility::kReverseWinding | disruptor::quad_visibility::kSecondTriangle);
}

struct BalconyCorner {
    uint16_t offset;
    uint32_t shipped;
    uint16_t x, z;
};

template <size_t N>
void trim_balcony(uint32_t sector, uint32_t origin_x, uint32_t part_offset, uint32_t part_header,
                  const std::array<BalconyCorner, N>& corners) {
    disruptor::Guest memory;
    const uint32_t room = memory.word(disruptor::sector_reach::kSectors + 4u * sector);
    if ((room & 3u) || !valid_main_ram_pointer(room, 35u) || memory.word(room + 24u) != origin_x ||
        memory.word(room + 28u) != 11616u || memory.word(room + 32u) != 1408u) return;
    const uint32_t mesh = memory.word(room + 16u);
    if ((mesh & 3u) || !valid_main_ram_pointer(mesh, corners.back().offset + 3u) ||
        memory.word(mesh + part_offset) != part_header) return;
    const auto clipped = [](const BalconyCorner& corner) {
        return (corner.shipped & 0xC00003FFu) | (uint32_t{corner.x} << 20) | (uint32_t{corner.z} << 10);
    };
    if (memory.word(mesh + corners.front().offset) != corners.front().shipped) return;
    for (const auto& corner : corners)
        if (memory.word(mesh + corner.offset) != corner.shipped) return;
    for (const auto& corner : corners) memory.write(mesh + corner.offset, clipped(corner));
}

void trim_triton_balcony() {
    if (psx_mod_read_byte(0x80071354u) != 5u) return;
    // Only these vertices of ceiling material 248 extend beyond the balcony beams.
    static constexpr std::array<BalconyCorner, 11> east{{
        {0x2C4, 0x0C008080, 192, 60}, {0x2D0, 0x0A008080, 160, 43}, {0x2FC, 0x10010080, 256, 96},
        {0x300, 0x4E010080, 224, 78}, {0x344, 0x08000080, 128, 25}, {0x348, 0x06000080, 112, 16},
        {0x34C, 0x04000080, 64, 16}, {0x350, 0x02000080, 32, 16}, {0x354, 0x00000080, 0, 16},
        {0x4CC, 0x0E008080, 224, 78}, {0x4D4, 0x0A000080, 160, 43}
    }};
    static constexpr std::array<BalconyCorner, 10> west{{
        {0x3C4, 0x06008080, 96, 43}, {0x3CC, 0x04008080, 64, 60}, {0x3D0, 0x02010080, 32, 78},
        {0x3D4, 0x00010080, 0, 96}, {0x410, 0x0E000080, 224, 16}, {0x414, 0x0C000080, 192, 16},
        {0x418, 0x0A000080, 144, 16}, {0x41C, 0x08000080, 128, 25}, {0x5A4, 0x02008080, 32, 78},
        {0x5AC, 0x06000080, 96, 43}
    }};
    trim_balcony(86, 12224, 0, 0xA94FFu, east);
    trim_balcony(87, 11776, 0x120, 0x9DCFFu, west);
}

static thread_local bool drawing_mesh_addition = false;

template <size_t N>
void draw_mesh_addition(CPUState *cpu, const std::array<uint32_t, N> &words) {
    constexpr uint32_t bytes = static_cast<uint32_t>(sizeof(words));
    const uint32_t scratch = cpu->gpr[29] - ((bytes + 15u) & ~15u);
    if ((scratch & 3u) || !valid_main_ram_pointer(scratch, bytes)) return;
    std::array<uint32_t, N> old{};
    for (size_t i = 0; i < words.size(); ++i) {
        const uint32_t address = scratch + static_cast<uint32_t>(i) * 4u;
        old[i] = cpu->read_word(address);
        cpu->write_word(address, words[i]);
    }
    std::array<uint32_t, 32> registers, gte_data, gte_ctrl;
    std::copy_n(cpu->gte_data, gte_data.size(), gte_data.begin());
    std::copy_n(cpu->gte_ctrl, gte_ctrl.size(), gte_ctrl.begin());
    std::copy_n(cpu->gpr, registers.size(), registers.begin());
    const uint32_t pc = cpu->pc, hi = cpu->hi, lo = cpu->lo;
    cpu->gpr[4] = cpu->gpr[29] = scratch;
    drawing_mesh_addition = true;
    func_override_guest_call(cpu, kDrawSector, registers[31]);
    drawing_mesh_addition = false;
    std::copy(registers.begin(), registers.end(), cpu->gpr);
    std::copy(gte_data.begin(), gte_data.end(), cpu->gte_data);
    std::copy(gte_ctrl.begin(), gte_ctrl.end(), cpu->gte_ctrl);
    cpu->pc = pc;
    cpu->hi = hi;
    cpu->lo = lo;
    for (size_t i = 0; i < old.size(); ++i) cpu->write_word(scratch + static_cast<uint32_t>(i) * 4u, old[i]);
}

void draw_upper_deck_floor(CPUState *cpu, uint32_t mesh) {
    if (cpu->gpr[31] != 0x800411A4u || comparator_active() || psx_netplay_active()) return;
    constexpr auto tile = [](uint32_t far_z, uint32_t material, uint32_t lights) {
        const auto vertex = [](uint32_t x, uint32_t z) { return (x << 20) | (z << 10); };
        const uint32_t near_z = far_z - 32u;
        return std::array<uint32_t, 12>{0x2CFFu, 4u, lights,
            vertex(96u, far_z), vertex(96u, near_z), vertex(64u, near_z), vertex(64u, far_z), 0xFFFFFFFFu,
            (1u << 12) | (material << 2), (2u << 22) | (3u << 12) | 0x240u, 0xFFFFFFFFu, 0xFFFFFFFFu};
    };
    if (valid_main_ram_pointer(mesh, 0xBC7u) && cpu->read_word(mesh) == 0xA00Du &&
        cpu->read_word(mesh + 0x4D0u) == 0xBD4FFu && cpu->read_word(mesh + 0xBC0u) == 0x110433CCu &&
        cpu->read_word(mesh + 0xBC4u) == 0x0F445240u) {
        draw_mesh_addition(cpu, tile(196u, 246u, 0x3A3A2121u));
    } else if (psx_mod_read_byte(0x80071354u) == 5u && psx_mod_read_byte(0x80071518u) == 5u &&
               valid_main_ram_pointer(mesh, 0xE6Fu) && cpu->read_word(mesh) == 0xF405u &&
               cpu->read_word(mesh + 0x52Cu) == 0xFC0FFu && cpu->read_word(mesh + 0xE68u) == 0x17C5D3C0u &&
               cpu->read_word(mesh + 0xE6Cu) == 0x15461240u) {
        // Sector 74 owns this tile, but its portal can be behind the eye while the tile is still visible in sector 71.
        draw_mesh_addition(cpu, tile(212u, 243u, 0x3A213A3Au));
    }
}

bool valid_primitive_pointer(uint32_t pointer, uint32_t trailing_bytes) {
    if (valid_main_ram_pointer(pointer, trailing_bytes)) return true;
    const uint32_t last = PSX_MOD_GPU_DMA_GUEST_BASE +
                          PSX_MOD_GPU_DMA_APERTURE_SIZE - 1u;
    return pointer >= PSX_MOD_GPU_DMA_GUEST_BASE && pointer <= last &&
           trailing_bytes <= last - pointer;
}

bool plausible_live_gameplay() {
    if (comparator_active() || psx_netplay_active() ||
        !psx_mod_game_started()) {
        return false;
    }

    const uint32_t maximum = psx_mod_read_word(kMaximumHealth);
    const uint32_t current = psx_mod_read_word(kCurrentHealth);
    if (maximum == 0u || maximum > 10000u ||
        current == 0u || current > maximum) {
        return false;
    }

    /* Mode 1 and an inactive player cover the known map, cutscene, death, and
     * level-transition renderer states. 0x800714E0 is Continue from mid mission's flag, no camera. */
    if (psx_mod_read_byte(kSceneMode) == 1u) return false;

    const uint32_t player = psx_mod_read_word(kPlayerPointer);
    const bool present = valid_main_ram_pointer(player, kPlayerInactiveOffset);
    const bool hands_empty = present && psx_mod_read_byte(player + kPlayerInactiveOffset) != 0u;
    return (present && !hands_empty) || disruptor::weapon_switching(hands_empty) || disruptor::unarmed();
}

bool plausible_far_globals(uint32_t far_distance, int32_t fade_start) {
    return far_distance >= kMinimumPlausibleFar &&
           far_distance <= kMaximumPlausibleSourceFar &&
           fade_start >= -static_cast<int32_t>(kMaximumPlausibleSourceFar) &&
           fade_start <= static_cast<int32_t>(far_distance);
}

const DistanceLoadSite *find_distance_load_site(uint32_t pc,
                                                uint32_t opcode) {
    for (const DistanceLoadSite& site : kDistanceLoadSites) {
        if (site.pc == pc && site.opcode == opcode) return &site;
    }
    return nullptr;
}

const ObjectDecisionSite *find_object_load_site(uint32_t pc) {
    for (const ObjectDecisionSite& site : kObjectDecisionSites) {
        if (site.load_pc == pc) return &site;
    }
    return nullptr;
}

const ObjectDecisionSite *find_object_decision_site(uint32_t pc,
                                                    uint32_t opcode) {
    for (const ObjectDecisionSite& site : kObjectDecisionSites) {
        if (site.decision_pc == pc && site.decision_opcode == opcode)
            return &site;
    }
    return nullptr;
}

bool traversal_return_address(uint32_t address) {
    return address == 0x800410E0u || address == 0x8003AE54u ||
           address == 0x8003AF1Cu;
}

bool signed_less(uint32_t lhs, uint32_t rhs) {
    return static_cast<int32_t>(lhs) < static_cast<int32_t>(rhs);
}

uint32_t mips_low_square(uint32_t value) {
    return static_cast<uint32_t>(static_cast<uint64_t>(value) *
                                 static_cast<uint64_t>(value));
}

bool renderer_ancestry_matches(const CPUState *cpu,
                               const DistanceLoadSite& site) {
    if (!cpu) return false;
    const uint32_t stack = cpu->gpr[29];
    if (!valid_main_ram_pointer(stack,
                                static_cast<uint32_t>(site.saved_ra_offset) +
                                    3u)) {
        return false;
    }
    const uint32_t saved_ra = psx_mod_read_word(
        stack + static_cast<uint32_t>(site.saved_ra_offset));
    for (uint8_t i = 0u; i < site.allowed_return_count; ++i) {
        if (saved_ra == site.allowed_return_addresses[i]) return true;
    }
    return false;
}

void preset_ratio(int preset, uint32_t *numerator, uint32_t *denominator) {
    /* Quarters of the game's own far plane, by preset. */
    constexpr uint32_t kQuarters[DISRUPTOR_FAR_RENDERING_PRESET_COUNT] = {4u, 5u, 6u, 8u};
    *numerator = preset >= 0 && preset < DISRUPTOR_FAR_RENDERING_PRESET_COUNT ? kQuarters[preset] : 4u;
    *denominator = 4u;
}

bool effective_distances(int preset, int depth_fade_mode,
                         uint32_t source_far,
                         int32_t source_fade, uint32_t *effective_far,
                         int32_t *effective_fade) {
    if (!effective_far || !effective_fade ||
        depth_fade_mode < DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL ||
        depth_fade_mode >= DISRUPTOR_FAR_RENDERING_DEPTH_FADE_MODE_COUNT ||
        !plausible_far_globals(source_far, source_fade)) {
        return false;
    }

    uint32_t numerator = 1u;
    uint32_t denominator = 1u;
    preset_ratio(preset, &numerator, &denominator);

    /* Integer nearest rounding is deterministic across hosts.  A script's far
     * plane at or beyond the ordering table's limit is not extended. */
    uint64_t scaled =
        (static_cast<uint64_t>(source_far) * numerator + denominator / 2u) /
        denominator;
    scaled = std::clamp<uint64_t>(scaled, source_far,
                                  std::max<uint64_t>(source_far, kMaximumSafeEffectiveFar));
    const uint64_t delta = scaled - source_far;
    const int64_t shifted_fade = static_cast<int64_t>(source_fade) +
                                 static_cast<int64_t>(delta);
    if (shifted_fade < -kMaximumSafeFade || shifted_fade > kMaximumSafeFade) {
        return false;
    }

    *effective_far = static_cast<uint32_t>(scaled);
    /* The renderer's depth fade spans [fade start, far plane].  Its eight
     * audited consumers subtract the start, clamp, and use fixed shifts; none
     * divides by (far - fade).  Equal endpoints are therefore safe and remove
     * the visible ramp while preserving the selected clip distance.  The far
     * value is already capped. */
    *effective_fade =
        depth_fade_mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED
            ? static_cast<int32_t>(scaled)
            : static_cast<int32_t>(shifted_fade);
    return true;
}

/* Caller owns g_mutex.  A global the game rewrote meanwhile keeps its new value. */
void restore_distances() {
    if (!g_written.active) return;
    if (psx_mod_read_word(kFarDistance) == g_written.effective_far)
        psx_mod_write_word(kFarDistance, g_written.source_far);
    if (psx_mod_read_word(kFadeStart) == static_cast<uint32_t>(g_written.effective_fade))
        psx_mod_write_word(kFadeStart, static_cast<uint32_t>(g_written.source_fade));
    g_written = WrittenDistances{};
}

bool portal_reach_enabled() {
    static const bool enabled = [] {
        const char *value = std::getenv("PSX_DISRUPTOR_PORTAL_REACH");
        return !value || std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

void restore_room_entry(const Unlisted &room) {
    if (room.room == disruptor::sector_reach::kNoSector) return;
    const uint32_t address = disruptor::portal_reach::kListedAt + room.room;
    if (psx_mod_read_byte(address) == 0xFFu) psx_mod_write_byte(address, room.entry);
}

/* Caller owns g_mutex. */
void close_spans() {
    while (g_unlisted_mesh_count != 0u)
        restore_room_entry(g_unlisted_mesh_rooms[--g_unlisted_mesh_count]);
    g_spans.close([](uint32_t address) { return psx_mod_read_word(address); },
                  [](uint32_t address, uint32_t word) { psx_mod_write_word(address, word); });
    g_beside.close(
        kVisibleEnd, [](uint32_t address) { return psx_mod_read_word(address); },
        [](uint32_t address) { return static_cast<uint32_t>(psx_mod_read_byte(address)); },
        [](uint32_t address, uint32_t word) { psx_mod_write_word(address, word); },
        [](uint32_t address, uint32_t byte) { psx_mod_write_byte(address, static_cast<uint8_t>(byte)); });
}

/* Caller owns g_mutex. */
void show_objects() {
    g_objects.show([](uint32_t address) { return psx_mod_read_word(address); },
                   [](uint32_t address, uint32_t word) { psx_mod_write_word(address, word); });
}

/* Caller owns g_mutex. */
void relist_room() {
    restore_room_entry(g_unlisted);
    g_unlisted = Unlisted{};
}

/* The walk skips a portal seen edge-on into a room it listed: the game's own walk, which has not listed it, goes in. */
void face_portal(const CPUState *cpu) {
    using disruptor::portal_reach::kListedAt;
    std::lock_guard<std::mutex> lock(g_mutex);
    relist_room();
    if (!g_frame.active || !g_frame.reach || !g_sight.judged()) return;
    const uint32_t gate = cpu->gpr[18]; /* $s2 */
    if (!disruptor::portal_reach::ram_word(gate)) return;
    const disruptor::sector_reach::Portal portal = disruptor::sector_reach::portal_at(disruptor::Guest{}, gate);
    const int32_t x = static_cast<int32_t>(psx_mod_read_word(disruptor::sector_reach::kCamera));
    const int32_t z = static_cast<int32_t>(psx_mod_read_word(disruptor::sector_reach::kCamera + 4u));
    if (disruptor::portal_reach::facing(portal, x, z) != 0) return;
    const uint32_t room = psx_mod_read_byte(gate + disruptor::sector_reach::kPortalSector);
    if (g_sight.seen(room)) return;
    const uint8_t entry = psx_mod_read_byte(kListedAt + room);
    if (entry == 0xFFu) return;
    psx_mod_write_byte(kListedAt + room, 0xFFu);
    g_unlisted = Unlisted{room, entry};
    g_frame.extended = true;
}

bool valid_visible_end(uint32_t end) {
    return valid_main_ram_pointer(end, 0u) && end >= kVisibleSpanBase && (end - kVisibleSpanBase) % kVisibleSpanStride == 0u;
}

void widen_walk(CPUState *cpu) {
    std::lock_guard<std::mutex> lock(g_mutex);
    relist_room();
    if (!g_frame.active || !g_frame.reach || !disruptor_render_arena_relocated()) return;
    using disruptor::portal_reach::Wedge;
    const Wedge wedge{{static_cast<int32_t>(cpu->gpr[4]), static_cast<int32_t>(cpu->gpr[5])},
                      {static_cast<int32_t>(cpu->gpr[6]), static_cast<int32_t>(cpu->gpr[7])}};
    const uint32_t portal = psx_mod_read_word(cpu->gpr[29] + disruptor::sector_reach::kPortalOnStack);
    const uint32_t room = disruptor::portal_reach::ram_word(portal)
                              ? psx_mod_read_byte(portal + disruptor::sector_reach::kPortalSector)
                              : disruptor::portal_reach::kMostRooms;
    const disruptor::portal_reach::View view = disruptor::portal_reach::view_at(disruptor::Guest{});
    const disruptor::portal_reach::Visit visit =
        room < disruptor::portal_reach::kMostRooms && room != disruptor::sector_reach::kNoSector
            ? disruptor::portal_reach::visit_at(disruptor::Guest{}, room)
            : disruptor::portal_reach::Visit{};
    const auto project = [](int32_t x) { return psx_ws_project_x(x); };
    const bool entered = g_sight.enter(
        cpu->gpr[19], room, wedge, visit, [&view, &project](const Wedge &own) { return disruptor::portal_reach::listed(own, view, project); },
        [&view, &project](const Wedge &own) { return disruptor::portal_reach::measured(own, view, project); });
    g_frame.extended = true;
    if (!entered && !visit.loaded) {
        cpu->gpr[19] = disruptor::sector_reach::kDepthCap;
        ++g_diagnostics.sector_requests_held;
        return;
    }
    const Wedge wider = disruptor::portal_reach::widened(wedge, disruptor::portal_reach::kWedgeSlack);
    cpu->gpr[4] = static_cast<uint32_t>(wider.left.x);
    cpu->gpr[5] = static_cast<uint32_t>(wider.left.z);
    cpu->gpr[6] = static_cast<uint32_t>(wider.right.x);
    cpu->gpr[7] = static_cast<uint32_t>(wider.right.z);
}

std::span<const uint8_t> rooftops_facades(const disruptor::Guest &guest) {
    namespace reach = disruptor::sector_reach;
    const uint32_t sector = guest.byte(kPlayerSectorAddress);
    if (guest.byte(0x80071354u) != 3u || guest.byte(0x80071518u) != 3u) return {};
    const bool first_bridge = sector >= 208u && sector <= 210u;
    if (!first_bridge && sector != 219u && sector != 220u && sector != 222u) return {};
    const uint32_t door = guest.word(reach::kSectors + 4u * (first_bridge ? 253u : 249u));
    if (!valid_main_ram_pointer(door, 36u) || guest.word(door + 24u) != (first_bridge ? 9008u : 10672u) ||
        guest.word(door + 28u) != 2528u || guest.word(door + 32u) != 2112u) return {};
    static constexpr std::array<uint8_t, 2> first_facades{214u, 217u};
    static constexpr std::array<uint8_t, 4> facades{224u, 226u, 227u, 228u};
    if (first_bridge) return first_facades;
    return facades;
}

void open_spans(CPUState *cpu) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_frame.active || !g_frame.reach || !g_frame.visible_spans_valid || !disruptor_render_arena_relocated()) return;
    const auto read = [](uint32_t address) { return psx_mod_read_word(address); };
    const auto write = [](uint32_t address, uint32_t word) { psx_mod_write_word(address, word); };
    g_spans.open(kVisibleSpanBase, cpu->gpr[2], kVisibleSpanStride, read, write);
    if (g_sight.judged() && valid_visible_end(cpu->gpr[2])) {
        const disruptor::Guest guest;
        const uint32_t end = g_beside.add(
            kVisibleSpanBase, cpu->gpr[2], kVisibleSpanStride, static_cast<int32_t>(psx_mod_read_word(disruptor::sector_reach::kCamera)),
            static_cast<int32_t>(psx_mod_read_word(disruptor::sector_reach::kCamera + 4u)),
            disruptor::portal_reach::view_at(guest), guest, write,
            [](uint32_t address, uint32_t byte) { psx_mod_write_byte(address, static_cast<uint8_t>(byte)); }, rooftops_facades(guest));
        if (end != cpu->gpr[2]) {
            psx_mod_write_word(kVisibleEnd, end);
            cpu->gpr[2] = end;
        }
    }
    const uint32_t last = std::min(cpu->gpr[2], kVisibleSpanBase + disruptor::portal_reach::kMostRooms * kVisibleSpanStride);
    if (g_sight.judged())
        for (uint32_t entry = kVisibleSpanBase; entry < last; entry += kVisibleSpanStride) {
            const uint32_t room = psx_mod_read_byte(entry + disruptor::portal_reach::kRoomByte);
            if (g_sight.seen(room)) continue;
            g_objects.hide(room, read, write);
            if (g_beside.added_at(entry)) continue;
            // Stand-in meshes use the original walk's room visibility.
            const uint32_t listed_at = disruptor::portal_reach::kListedAt + room;
            const uint8_t index = psx_mod_read_byte(listed_at);
            if (index == 0xFFu || g_unlisted_mesh_count == g_unlisted_mesh_rooms.size()) continue;
            g_unlisted_mesh_rooms[g_unlisted_mesh_count++] = {room, index};
            psx_mod_write_byte(listed_at, 0xFFu);
        }
    if (g_spans.open() || g_objects.hidden() || g_beside.added() != 0u) g_frame.extended = true;
}

/* The packets from `g_admitted_from` to `end` hold a face only the wider walk admits, outside `g_admitted_band`. */
void close_admitted(uint32_t end) {
    if (g_admitted_from != 0u && end > g_admitted_from) {
        if (g_admitted_band.left > g_admitted_band.right)
            gpu_supplemental_packets(g_admitted_from, end);
        else
            gpu_supplemental_band(g_admitted_from, end, g_admitted_band.left, g_admitted_band.right);
    }
    g_admitted_from = 0u;
}

bool visible_entry(uint32_t entry) {
    const uint32_t end = psx_mod_read_word(kVisibleEnd);
    return valid_visible_end(end) && entry >= kVisibleSpanBase && entry < end && (entry - kVisibleSpanBase) % kVisibleSpanStride == 0u;
}

struct PassageGate {
    uint32_t source, target, slot;
};

constexpr std::array<PassageGate, 5> kPassageGates{{{19, 15, 0}, {15, 14, 0}, {14, 13, 1}, {13, 0, 1}, {0, 186, 0}}};

bool entrance_mesh(uint32_t sector, uint32_t mesh) {
    for (const auto &gate : kPassageGates) {
        if (gate.target != sector) continue;
        const uint32_t room = psx_mod_read_word(disruptor::sector_reach::kSectors + 4u * sector);
        return valid_main_ram_pointer(room, 20u) && valid_main_ram_pointer(mesh, 4u) && psx_mod_read_word(room + 16u) == mesh;
    }
    return false;
}

uint32_t portal_far(uint32_t gate) {
    if (!g_frame.entrance_reach || !valid_main_ram_pointer(gate, 15u)) return g_written.effective_far;
    const uint32_t target = psx_mod_read_byte(gate + disruptor::sector_reach::kPortalSector);
    for (const auto &link : kPassageGates) {
        if (link.target != target) continue;
        const uint32_t source = psx_mod_read_word(disruptor::sector_reach::kSectors + 4u * link.source);
        if (!valid_main_ram_pointer(source, disruptor::portal_reach::kSectorPortals + 4u)) break;
        if (psx_mod_read_word(source + disruptor::portal_reach::kSectorPortals) != gate - link.slot * disruptor::portal_reach::kPortalStride) break;
        if (disruptor::portal_reach::ram_word(psx_mod_read_word(disruptor::sector_reach::kSectors + 4u * target)))
            return std::min(kMaximumSafeEffectiveFar, g_written.effective_far + g_frame.entrance_reach);
    }
    return g_written.effective_far;
}

void discard_reallocated_fog(uint32_t cursor) {
    if (cursor >= g_frame.entrance_tagged_end || !valid_primitive_pointer(cursor, 4u)) return;
    const uint32_t first = psx_mod_gpu_dma_resolve_address(cursor);
    std::erase_if(g_packet_fractions, [first](const auto &entry) {
        return entry.second.arena == g_frame.primitive_start && entry.first >= first;
    });
    g_frame.entrance_tagged_end = cursor;
}

void restore_entrance_far() {
    if (g_frame.entrance_saved_far && g_written.active) {
        if (psx_mod_read_word(kFarDistance) == g_written.effective_far)
            psx_mod_write_word(kFarDistance, g_frame.entrance_saved_far);
        g_written.effective_far = g_frame.entrance_saved_far;
    }
    g_frame.entrance_saved_far = 0u;
}

bool entrance_packet_range(uint32_t packet, uint32_t bytes) {
    const uint32_t root = g_frame.primitive_start;
    const uint32_t capacity = valid_main_ram_pointer(root, 0u) ? 0x11000u : PSX_MOD_GPU_DMA_APERTURE_SIZE / 2u;
    return packet >= root && (packet & 3u) == 0u && packet - root < capacity && bytes <= capacity - (packet - root) &&
           valid_primitive_pointer(packet, bytes);
}

void tag_entrance_mesh(uint32_t end) {
    uint32_t packet = g_frame.entrance_mesh_start;
    g_frame.entrance_mesh_start = 0u;
    if (!packet || end <= packet || !entrance_packet_range(packet, end - packet)) return;
    g_frame.entrance_tagged_end = end;
    const int detail = static_cast<int>(std::lround(std::min(g_frame.entrance_reach, 256u) * g_frame.entrance_visibility));
    while (end - packet >= 8u) {
        const uint32_t words = psx_mod_read_word(packet) >> 24;
        if (!words || words >= (end - packet) / 4u) break;
        const uint32_t op = psx_mod_read_word(packet + 4u) >> 24;
        const uint32_t corners = (op & 8u) ? 4u : 3u;
        const uint32_t textured_words = 1u + 2u * corners + ((op & 16u) ? corners - 1u : 0u);
        if ((op & 0xe6u) == 0x24u && words == textured_words) {
            std::array<uint32_t, 12> command{};
            for (uint32_t i = 0; i < words; ++i) command[i] = psx_mod_read_word(packet + 4u + 4u * i);
            const uint32_t key = psx_mod_gpu_dma_resolve_address(packet);
            if (key) g_packet_fractions[key] = {g_fog_serial, PSX_PACKET_FOG_DETAIL_BASE + detail, true,
                                                psx_gpu_command_signature(command.data(), static_cast<int>(words)), g_frame.primitive_start};
        }
        packet += 4u + 4u * words;
    }
}

/* A room's mesh begins: $s1 is the visible loop's entry. A room Beside adds is drawn as the game draws its own. */
void note_room(const CPUState *cpu) {
    using disruptor::portal_reach::turn_guarded_left, disruptor::portal_reach::turn_guarded_right;
    const uint32_t start = psx_mod_read_word(kPrimitivePointer);
    close_admitted(start);
    g_face_rule = FaceRule{};
    std::lock_guard<std::mutex> lock(g_mutex);
    discard_reallocated_fog(start);
    const uint32_t entry = cpu->gpr[17];
    restore_entrance_far();
    g_frame.entrance_mesh_start = 0;
    if (g_frame.active && g_written.active && g_frame.entrance_reach && cpu->gpr[31] == 0x800411A4u && visible_entry(entry) &&
        entrance_mesh(psx_mod_read_byte(entry + disruptor::portal_reach::kRoomByte), cpu->gpr[4]) &&
        entrance_packet_range(start, 4u) &&
        psx_mod_read_word(kFarDistance) == g_written.effective_far) {
        g_frame.entrance_mesh_start = start;
        g_frame.entrance_saved_far = g_written.effective_far;
        g_written.effective_far = std::min(kMaximumSafeEffectiveFar, g_written.effective_far + g_frame.entrance_reach);
        psx_mod_write_word(kFarDistance, g_written.effective_far);
    }
    if (!g_frame.active || !g_frame.reach || !g_sight.judged() || !visible_entry(entry) || g_beside.added_at(entry)) return;
    const uint32_t room = psx_mod_read_byte(entry + disruptor::portal_reach::kRoomByte);
    const disruptor::portal_reach::Span span = g_sight.span(room);
    g_face_rule = {true, !g_sight.seen(room), static_cast<int32_t>(turn_guarded_left(static_cast<uint32_t>(span.left))),
                   static_cast<int32_t>(turn_guarded_right(static_cast<uint32_t>(span.right)))};
}

/* After the drop test of a face: $t9 is where its packets begin, $at what its corners share, t0..t3 its corner records. */
void note_face(const CPUState *cpu) {
    const uint32_t packet = cpu->gpr[25];
    close_admitted(packet);
    if (!g_face_rule.active || cpu->gpr[1] != 0u || !cpu->read_word) return;
    const bool console_wall = disruptor::portal_reach::console_wall(cpu->gpr[6], cpu->gpr[7], cpu->gpr[4], cpu->read_word);
    if (console_wall && psx_mod_read_byte(kPlayerSectorAddress) == kConsole02Sector) return;
    const std::array<uint32_t, 4> screen = {cpu->read_word(cpu->gpr[8] + 4u), cpu->read_word(cpu->gpr[9] + 4u),
                                            cpu->read_word(cpu->gpr[10] + 4u), cpu->read_word(cpu->gpr[11] + 4u)};
    if (g_face_rule.whole || disruptor::portal_reach::beside_span(screen, g_face_rule.left, g_face_rule.right)) {
        g_admitted_from = packet;
        g_admitted_band = disruptor::portal_reach::kWholeFace;
    } else if (disruptor::portal_reach::room_band(g_face_rule.left, g_face_rule.right, g_admitted_band)) {
        if (console_wall) return;
        g_admitted_from = packet;
    }
}

/* After the visible loop the game reads the list for its own rooms (0x800240C4, 0x8002DAE0): the wider walk's go. */
void keep_game_rooms() {
    close_admitted(psx_mod_read_word(kPrimitivePointer));
    g_face_rule = FaceRule{};
    std::lock_guard<std::mutex> lock(g_mutex);
    show_objects();
    if (!g_frame.active || !g_frame.reach || !g_frame.visible_spans_valid || !g_sight.judged()) return;
    close_spans();
    const uint32_t end = psx_mod_read_word(kVisibleEnd);
    if (!valid_visible_end(end)) return;
    const uint32_t kept = disruptor::portal_reach::keep(
        kVisibleSpanBase, end, kVisibleSpanStride, g_sight, [](uint32_t address) { return psx_mod_read_word(address); },
        [](uint32_t address, uint32_t word) { psx_mod_write_word(address, word); });
    if (kept != end) psx_mod_write_word(kVisibleEnd, kept);
}

/* Caller owns g_mutex.  The renderer dissolves the world's edge only under its smooth fade, so only then are the meshes kept past it.
 * The margin is guest memory, so a peer or the comparator, whose renderer settings may differ, never gets it. */
bool edge_margin_active() {
    return gl_renderer_smooth_fade() != 0 && g_depth_fade_mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL &&
           !comparator_active() && !psx_netplay_active();
}

uint32_t fortress_entrance_reach(uint32_t far, double &visibility) {
    constexpr uint32_t mission_number = 0x80071354u, fortress = 13u, door_sector = 186u;
    if (g_preset == DISRUPTOR_FAR_RENDERING_RETAIL || psx_mod_read_byte(mission_number) != fortress) return 0;
    const disruptor::Guest guest;
    if (!disruptor::portal_reach::ram_word(guest.word(disruptor::sector_reach::kSectors + 4u * door_sector))) return 0;

    constexpr double door_x = 3328.0, door_z = 10592.0;
    constexpr double entrance_x = 3336.0, entrance_z = 9072.0, entrance_half_width = 208.0;
    constexpr double max_far = kMaximumSafeEffectiveFar - kLeastEdgeMargin;
    const double dx = door_x - static_cast<int32_t>(guest.word(disruptor::sector_reach::kCamera));
    const double dz = door_z - static_cast<int32_t>(guest.word(disruptor::sector_reach::kCamera + 4u));
    if (dz <= 0.0 || dz > max_far + kFadeDepth) return 0;

    const double distance = std::hypot(dx, dz);
    const double required = std::min(distance + kFadeDepth, max_far);
    if (required <= far) return 0;
    const auto view = disruptor::portal_reach::view_at(guest);
    const double facing = (dx * view.sine + dz * view.cosine) / (256.0 * distance);
    // Intersect the sightline to the door with the courtyard arch.
    const double crossing_x = door_x - dx * std::min(1.0, (door_z - entrance_z) / dz);
    const double through_arch = std::clamp((entrance_half_width - std::abs(crossing_x - entrance_x)) / kFadeRowDepth, 0.0, 1.0);
    const double in_view = std::clamp(2.0 * facing, 0.0, 1.0);
    const double arch_distance = distance * std::max(0.0, 1.0 - (door_z - entrance_z) / dz);
    const double remaining = std::clamp((far - arch_distance) / kFadeDepth, 0.0, 1.0);
    visibility = remaining * remaining * (3.0 - 2.0 * remaining);
    if (visibility == 0.0) return 0;
    return static_cast<uint32_t>(std::lround((required - far) * through_arch * in_view));
}

/* Caller owns g_mutex.  Near the ordering table's limit a far plane the preset lengthened gives the least margin its room, the game's own never shrinks. */
void make_room_for_margin(uint32_t source_far, uint32_t *effective_far, int32_t *effective_fade) {
    constexpr uint32_t kRoom = kMaximumSafeEffectiveFar - kLeastEdgeMargin;
    if (!edge_margin_active() || *effective_far <= kRoom) return;
    const uint32_t visual = std::max(source_far, kRoom);
    *effective_fade -= static_cast<int32_t>(*effective_far - visual);
    *effective_far = visual;
}

void write_distances(uint32_t source_far, int32_t source_fade, uint32_t effective_far, int32_t effective_fade) {
    const uint32_t written_far = edge_margin_active() && effective_far < kMaximumSafeEffectiveFar
        ? std::min(effective_far + kEdgeMargin, kMaximumSafeEffectiveFar) : effective_far;
    if (written_far == source_far && effective_fade == source_fade) return;
    psx_mod_write_word(kFarDistance, written_far);
    psx_mod_write_word(kFadeStart, static_cast<uint32_t>(effective_fade));
    g_written = WrittenDistances{true, source_far, source_fade, written_far, effective_fade, effective_far};
}

/* Caller owns g_mutex. Smooth fog begins at the game's own start. */
void set_frame_fog(int32_t game_start, uint32_t far_drawn, int32_t start_drawn) {
    const bool smooth = gl_renderer_smooth_fade() != 0 && g_depth_fade_mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL;
    const int64_t stretched = static_cast<int64_t>(far_drawn) - game_start;
    g_fog.active = true;
    g_fog.smooth = smooth && stretched >= kFadeDepth;
    g_fog.start = g_fog.smooth ? game_start : start_drawn;
    g_fog.length = g_fog.smooth ? static_cast<int32_t>(stretched) : kFadeDepth;
    g_fog.exponent = g_fog.smooth ? g_fog_exponent : 1;
}

void reset_comparison_metrics() {
    g_pending_decision = PendingFarDecision{};
    g_diagnostics.completed_frames = 0u;
    g_diagnostics.substituted_loads = 0u;
    g_diagnostics.far_load_substitutions = 0u;
    g_diagnostics.fade_load_substitutions = 0u;
    g_diagnostics.source_far = 0u;
    g_diagnostics.source_fade = 0;
    g_diagnostics.effective_far = 0u;
    g_diagnostics.effective_fade = 0;
    g_diagnostics.primitive_samples = 0u;
    g_diagnostics.primitive_start = 0u;
    g_diagnostics.primitive_end = 0u;
    g_diagnostics.primitive_last_delta = 0u;
    g_diagnostics.primitive_high_water = 0u;
    g_diagnostics.traversal_recursion_entries = 0u;
    g_diagnostics.traversal_cap_hits = 0u;
    g_diagnostics.portal_far_tests = 0u;
    g_diagnostics.portal_shortcut_relaxations = 0u;
    g_diagnostics.portal_final_decisions = 0u;
    g_diagnostics.portal_final_effective_rejections = 0u;
    g_diagnostics.portal_final_decision_flips = 0u;
    g_diagnostics.object_far_decisions = 0u;
    g_diagnostics.object_effective_rejections = 0u;
    g_diagnostics.object_decision_flips = 0u;
    g_diagnostics.observer_sequence_errors = 0u;
    g_diagnostics.visibility_samples = 0u;
    g_diagnostics.packet_stage_samples = 0u;
    g_diagnostics.traversal_rooms_last = 0u;
    g_diagnostics.traversal_rooms_high_water = 0u;
    g_diagnostics.submitted_spans_last = 0u;
    g_diagnostics.submitted_spans_high_water = 0u;
    g_diagnostics.traversal_max_depth_last = 0u;
    g_diagnostics.traversal_max_depth_high_water = 0u;
    g_diagnostics.packet_entry_to_traversal_end_last = 0u;
    g_diagnostics.packet_traversal_to_visible_start_last = 0u;
    g_diagnostics.packet_visible_loop_last = 0u;
    g_diagnostics.packet_post_visible_loop_last = 0u;
    g_diagnostics.render_wall_us.fill(0.0);
    g_diagnostics.render_wall_us_count = 0u;
    g_diagnostics.render_wall_us_next = 0u;
    g_diagnostics.render_wall_us_max = 0.0;
}

void record_timing(double elapsed_us) {
    if (!std::isfinite(elapsed_us) || elapsed_us < 0.0) return;
    g_diagnostics.render_wall_us[g_diagnostics.render_wall_us_next] =
        elapsed_us;
    g_diagnostics.render_wall_us_next =
        (g_diagnostics.render_wall_us_next + 1u) % kTimingHistory;
    g_diagnostics.render_wall_us_count = std::min(
        g_diagnostics.render_wall_us_count + 1u, kTimingHistory);
    g_diagnostics.render_wall_us_max =
        std::max(g_diagnostics.render_wall_us_max, elapsed_us);
}

void clear_pending_decision(bool sequence_error) {
    if (sequence_error &&
        g_pending_decision.kind != PendingDecisionKind::None) {
        ++g_diagnostics.observer_sequence_errors;
    }
    g_pending_decision = PendingFarDecision{};
}

void arm_pending_decision(uint32_t load_pc, uint32_t source_far,
                          uint32_t effective_far) {
    if (!g_frame.active) return;

    if (g_pending_decision.kind != PendingDecisionKind::None)
        clear_pending_decision(true);

    if (load_pc == 0x8003A914u) {
        g_pending_decision.kind = PendingDecisionKind::Portal;
        g_pending_decision.expected_pc = kPortalFirstCandidate;
    } else if (const ObjectDecisionSite *site =
                   find_object_load_site(load_pc)) {
        g_pending_decision.kind = PendingDecisionKind::Object;
        g_pending_decision.expected_pc = site->decision_pc;
    } else {
        return;
    }
    g_pending_decision.source_far = source_far;
    g_pending_decision.effective_far = effective_far;
}

void observe_portal_candidate(const CPUState *cpu, uint32_t pc) {
    if (!cpu || g_pending_decision.kind == PendingDecisionKind::None)
        return;
    if (g_pending_decision.kind != PendingDecisionKind::Portal ||
        g_pending_decision.expected_pc != pc) {
        clear_pending_decision(true);
        return;
    }

    const uint32_t effective_far_squared =
        mips_low_square(g_pending_decision.effective_far);
    if (cpu->gpr[11] != effective_far_squared) { /* $t3 */
        clear_pending_decision(true);
        return;
    }

    const uint32_t candidate_squared = cpu->gpr[2]; /* $v0 */
    const bool retail_rejects = signed_less(
        mips_low_square(g_pending_decision.source_far), candidate_squared);
    const bool effective_rejects =
        signed_less(effective_far_squared, candidate_squared);
    ++g_diagnostics.portal_far_tests;

    if (pc == kPortalFinalCandidate) {
        ++g_diagnostics.portal_final_decisions;
        if (effective_rejects)
            ++g_diagnostics.portal_final_effective_rejections;
        if (retail_rejects && !effective_rejects)
            ++g_diagnostics.portal_final_decision_flips;
        clear_pending_decision(false);
        return;
    }

    if (retail_rejects && !effective_rejects)
        ++g_diagnostics.portal_shortcut_relaxations;

    /* An accepted endpoint takes the guest shortcut around every later
     * distance test.  A rejection continues to the next reviewed candidate. */
    if (!effective_rejects) {
        clear_pending_decision(false);
    } else if (pc == kPortalFirstCandidate) {
        g_pending_decision.expected_pc = kPortalSecondCandidate;
    } else if (pc == kPortalSecondCandidate) {
        g_pending_decision.expected_pc = kPortalFinalCandidate;
    } else {
        clear_pending_decision(true);
    }
}

/* Caller owns g_mutex.  The portal is in $s2 at all three tests, the distance just squared in $v0. */
void check_portal_copy(const CPUState *cpu, uint32_t pc) {
    namespace reach = disruptor::sector_reach;
    const disruptor::Guest guest;
    const reach::Portal portal = reach::portal_at(guest, cpu->gpr[18]);
    const int32_t x = static_cast<int32_t>(guest.word(reach::kCamera));
    const int32_t z = static_cast<int32_t>(guest.word(reach::kCamera + 4u));
    const int32_t copy = pc == kPortalFirstCandidate    ? reach::first_distance(portal, x, z)
                         : pc == kPortalSecondCandidate ? reach::second_distance(portal, x, z)
                                                        : reach::offset_distance(portal, x, z, reach::scale_at(guest, portal));
    if (static_cast<uint32_t>(copy) != cpu->gpr[2]) ++g_diagnostics.portal_model_mismatches;
}

void observe_object_decision(const CPUState *cpu,
                             const ObjectDecisionSite& site) {
    if (!cpu || g_pending_decision.kind == PendingDecisionKind::None)
        return;
    if (g_pending_decision.kind != PendingDecisionKind::Object ||
        g_pending_decision.expected_pc != site.decision_pc) {
        clear_pending_decision(true);
        return;
    }

    const uint32_t threshold = cpu->gpr[site.threshold_register];
    const bool effective_rejects =
        signed_less(g_pending_decision.effective_far, threshold);
    const uint32_t guest_result = cpu->gpr[site.result_register];
    if (guest_result > 1u || (guest_result != 0u) != effective_rejects) {
        clear_pending_decision(true);
        return;
    }

    const bool retail_rejects =
        signed_less(g_pending_decision.source_far, threshold);
    ++g_diagnostics.object_far_decisions;
    if (effective_rejects) ++g_diagnostics.object_effective_rejections;
    if (retail_rejects && !effective_rejects)
        ++g_diagnostics.object_decision_flips;
    clear_pending_decision(false);
}

bool read_primitive_boundary(uint32_t *out) {
    if (!out) return false;
    *out = psx_mod_read_word(kPrimitivePointer);
    if (valid_primitive_pointer(*out, 0u)) return true;
    ++g_diagnostics.invalid_globals;
    return false;
}

void packet_sequence_error() {
    ++g_diagnostics.observer_sequence_errors;
    g_frame.packet_sequence_valid = false;
}

void observe_renderer_boundary(const CPUState *cpu, uint32_t pc) {
    if (!cpu) return;

    /* The screen effects called between these two seams (0x8003FE6C) read the fade start and store it back changed. */
    if (pc == kAfterTraversal && (g_preset != DISRUPTOR_FAR_RENDERING_RETAIL ||
                                  g_depth_fade_mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED || edge_margin_active())) {
        restore_distances();
        g_frame.rewrite_after_effects = true;
    } else if (pc == kBeforeVisibleLoop && g_frame.rewrite_after_effects) {
        g_frame.rewrite_after_effects = false;
        const uint32_t source_far = psx_mod_read_word(kFarDistance);
        const int32_t source_fade = static_cast<int32_t>(psx_mod_read_word(kFadeStart));
        uint32_t effective_far = 0u;
        int32_t effective_fade = 0;
        if (effective_distances(g_preset, g_depth_fade_mode, source_far, source_fade, &effective_far, &effective_fade)) {
            make_room_for_margin(source_far, &effective_far, &effective_fade);
            g_frame.entrance_reach = fortress_entrance_reach(effective_far, g_frame.entrance_visibility);
            write_distances(source_far, source_fade, effective_far, effective_fade);
            set_frame_fog(source_fade, effective_far, effective_fade);
        }
        g_frame.extended = g_frame.extended || g_written.active;
    }

    if (pc == kAfterTraversal) {
        if (g_frame.saw_after_traversal ||
            g_frame.saw_before_visible_loop ||
            g_frame.saw_after_visible_loop)
            packet_sequence_error();
        g_frame.saw_after_traversal = true;
        if (!read_primitive_boundary(
                &g_frame.primitive_after_traversal))
            g_frame.packet_sequence_valid = false;
        return;
    }

    if (pc == kBeforeVisibleLoop) {
        const bool in_order = g_frame.saw_after_traversal &&
                              !g_frame.saw_before_visible_loop &&
                              !g_frame.saw_after_visible_loop;
        if (!in_order) packet_sequence_error();
        g_frame.saw_before_visible_loop = true;
        if (!read_primitive_boundary(
                &g_frame.primitive_before_visible_loop))
            g_frame.packet_sequence_valid = false;

        const uint32_t end = cpu->gpr[2]; /* $v0 from $gp + 0x680 */
        const bool valid_end = valid_visible_end(end);
        if (in_order && valid_end) {
            g_frame.visible_spans_valid = true;
            g_frame.submitted_spans =
                (end - kVisibleSpanBase) / kVisibleSpanStride;
        } else {
            g_frame.visible_spans_valid = false;
            if (!valid_end) ++g_diagnostics.invalid_globals;
        }
        return;
    }

    if (pc == kAfterVisibleLoop) {
        if (!g_frame.saw_before_visible_loop ||
            g_frame.saw_after_visible_loop)
            packet_sequence_error();
        g_frame.saw_after_visible_loop = true;
        if (!read_primitive_boundary(
                &g_frame.primitive_after_visible_loop))
            g_frame.packet_sequence_valid = false;
    }
}

bool sample_primitive_end(uint32_t *sampled_end) {
    const uint32_t end = psx_mod_read_word(kPrimitivePointer);
    if (sampled_end) *sampled_end = end;
    g_diagnostics.primitive_start = g_frame.primitive_start;
    g_diagnostics.primitive_end = end;
    g_diagnostics.primitive_last_delta = 0u;

    if (!valid_primitive_pointer(g_frame.primitive_start, 0u) ||
        !valid_primitive_pointer(end, 0u) || end < g_frame.primitive_start) {
        ++g_diagnostics.invalid_globals;
        return false;
    }

    const uint32_t delta = end - g_frame.primitive_start;
    g_diagnostics.primitive_last_delta = delta;
    g_diagnostics.primitive_high_water =
        std::max(g_diagnostics.primitive_high_water, delta);
    ++g_diagnostics.primitive_samples;
    return true;
}

void publish_observer_frame(uint32_t primitive_end,
                            bool primitive_sample_valid) {
    if (g_frame.visible_spans_valid) {
        ++g_diagnostics.visibility_samples;
        g_diagnostics.traversal_rooms_last = g_frame.traversal_rooms;
        g_diagnostics.traversal_rooms_high_water = std::max(
            g_diagnostics.traversal_rooms_high_water,
            g_frame.traversal_rooms);
        g_diagnostics.submitted_spans_last = g_frame.submitted_spans;
        g_diagnostics.submitted_spans_high_water = std::max(
            g_diagnostics.submitted_spans_high_water,
            g_frame.submitted_spans);
        g_diagnostics.traversal_max_depth_last =
            g_frame.traversal_depth_seen ? g_frame.traversal_max_depth : 0u;
        g_diagnostics.traversal_max_depth_high_water = std::max(
            g_diagnostics.traversal_max_depth_high_water,
            g_diagnostics.traversal_max_depth_last);
    } else {
        g_diagnostics.traversal_rooms_last = 0u;
        g_diagnostics.submitted_spans_last = 0u;
        g_diagnostics.traversal_max_depth_last = 0u;
    }

    g_diagnostics.packet_entry_to_traversal_end_last = 0u;
    g_diagnostics.packet_traversal_to_visible_start_last = 0u;
    g_diagnostics.packet_visible_loop_last = 0u;
    g_diagnostics.packet_post_visible_loop_last = 0u;

    const bool any_boundary = g_frame.saw_after_traversal ||
                              g_frame.saw_before_visible_loop ||
                              g_frame.saw_after_visible_loop;
    const bool all_boundaries = g_frame.saw_after_traversal &&
                                g_frame.saw_before_visible_loop &&
                                g_frame.saw_after_visible_loop;
    if (any_boundary && !all_boundaries && g_frame.packet_sequence_valid)
        packet_sequence_error();

    if (!primitive_sample_valid || !all_boundaries ||
        !g_frame.packet_sequence_valid ||
        g_frame.primitive_start > g_frame.primitive_after_traversal ||
        g_frame.primitive_after_traversal >
            g_frame.primitive_before_visible_loop ||
        g_frame.primitive_before_visible_loop >
            g_frame.primitive_after_visible_loop ||
        g_frame.primitive_after_visible_loop > primitive_end) {
        if (all_boundaries && g_frame.packet_sequence_valid &&
            primitive_sample_valid)
            packet_sequence_error();
        return;
    }

    g_diagnostics.packet_entry_to_traversal_end_last =
        g_frame.primitive_after_traversal - g_frame.primitive_start;
    g_diagnostics.packet_traversal_to_visible_start_last =
        g_frame.primitive_before_visible_loop -
        g_frame.primitive_after_traversal;
    g_diagnostics.packet_visible_loop_last =
        g_frame.primitive_after_visible_loop -
        g_frame.primitive_before_visible_loop;
    g_diagnostics.packet_post_visible_loop_last =
        primitive_end - g_frame.primitive_after_visible_loop;
    ++g_diagnostics.packet_stage_samples;
}

/* Caller owns g_mutex.  Only the exact epilogue makes a valid timing/primitive
 * sample.  Every other transition abandons the partial wall-clock span. */
void abandon_active_frame(bool missed_epilogue) {
    restore_distances();
    close_spans();
    show_objects();
    relist_room();
    g_fog.active = false;
    clear_pending_decision(false);
    if (!g_frame.active) return;
    if (missed_epilogue) ++g_diagnostics.missed_epilogues;
    g_frame = ActiveFrame{};
}

void finish_active_frame() {
    restore_distances();
    close_spans();
    show_objects();
    relist_room();
    if (!g_frame.active) {
        clear_pending_decision(false);
        return;
    }
    ++g_diagnostics.completed_frames;

    uint32_t primitive_end = 0u;
    const bool primitive_sample_valid = sample_primitive_end(&primitive_end);
    publish_observer_frame(primitive_end, primitive_sample_valid);
    const double elapsed_us =
        std::chrono::duration<double, std::micro>(Clock::now() -
                                                  g_frame.started)
            .count();
    record_timing(elapsed_us);
    clear_pending_decision(true);
    g_frame = ActiveFrame{};
}

void force_retail_for_netplay() {
    if (!psx_netplay_active()) return;
    abandon_active_frame(false);
    if (g_preset != DISRUPTOR_FAR_RENDERING_RETAIL ||
        g_depth_fade_mode != DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL) {
        g_preset = DISRUPTOR_FAR_RENDERING_RETAIL;
        g_depth_fade_mode = DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL;
        reset_comparison_metrics();
    }
}

/* Caller owns g_mutex.  Netplay or the comparator began inside a written frame. */
bool retire_for_transition() {
    if (!g_frame.active || (!comparator_active() && !psx_netplay_active())) return false;
    if (psx_netplay_active())
        force_retail_for_netplay();
    else
        abandon_active_frame(true);
    return true;
}

/* Caller owns g_mutex.  The depth `into` the fog, pressed into the game's 768 units by the frame's ramp and power. */
int64_t pressed_into_fog(int64_t into) {
    if (into <= 0) return into;
    if (into >= g_fog.length) return kFadeDepth;
    int64_t numerator = kFadeDepth, denominator = 1;
    for (int power = 0; power < g_fog.exponent; ++power) {
        numerator *= into;
        denominator *= g_fog.length;
    }
    return numerator / denominator;
}

int fog_row(int64_t pressed) { return static_cast<int>(std::clamp<int64_t>(pressed, 0, kFadeDepth - 1) / kFadeRowDepth); }

int sprite_fog_level(int64_t pressed) {
    return static_cast<int>(std::clamp<int64_t>(pressed, 0, kFadeDepth)) * (256 / kFadeRowDepth);
}

/* The fade start a fade load hands its code: the one that makes the game's own arithmetic land on the frame's fog. */
void shape_fade_load(CPUState *cpu, const DistanceLoadSite& site) {
    const auto fog = std::find_if(kFogSites.begin(), kFogSites.end(), [&](const FogSite& one) { return one.pc == site.pc; });
    if (fog == kFogSites.end()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (fog->packet_in_register) g_list_shaped = 0u;
    if (!g_fog.active || comparator_active() || psx_netplay_active() || !renderer_ancestry_matches(cpu, site)) return;
    const uint32_t loaded = (site.opcode >> 16u) & 31u;
    if (loaded == 0u || cpu->gpr[loaded] != psx_mod_read_word(kFadeStart)) return;
    /* The sky lies past all that is drawn: it keeps the game's own fog, never clearer than a wall before it. */
    if (fog->pc == kSkyFogSite) {
        if (g_written.active) cpu->gpr[loaded] = static_cast<uint32_t>(g_written.source_fade);
        return;
    }
    const int64_t held = static_cast<int32_t>(cpu->gpr[fog->range_register]);
    const int64_t range = fog->from == RangeFrom::HalfRegister ? held >> 1
                          : fog->from == RangeFrom::WeaponDepth ? (30 - held) * 30 + 128
                                                                : held;
    const int64_t pressed = pressed_into_fog(range - g_fog.start);
    cpu->gpr[loaded] = static_cast<uint32_t>(range - pressed);
    const uint32_t packet = fog->packet_in_register ? cpu->gpr[kListPacketRegister] : psx_mod_read_word(kPrimitivePointer);
    if (fog->packet_in_register) g_list_shaped = packet;
    if (!fog->rows || !g_fog.smooth) return;
    if (const uint32_t key = psx_mod_gpu_dma_resolve_address(packet))
        g_packet_fractions[key] = PacketFraction{g_fog_serial, sprite_fog_level(pressed)};
}

void observe_distance_load(CPUState *cpu, const DistanceLoadSite& site) {
    if (!cpu) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_written.active) return;
    const bool far_load = site.kind == DistanceKind::Far;
    const uint32_t loaded = (site.opcode >> 16u) & 31u;
    const uint32_t written_value = far_load ? g_written.effective_far : static_cast<uint32_t>(g_written.effective_fade);
    const uint32_t game_value = far_load ? g_written.source_far : static_cast<uint32_t>(g_written.source_fade);
    if (retire_for_transition()) {
        if (loaded != 0u && cpu->gpr[loaded] == written_value) cpu->gpr[loaded] = game_value;
        return;
    }
    if (written_value == game_value || loaded == 0u || !renderer_ancestry_matches(cpu, site)) return;
    if (cpu->gpr[loaded] != written_value) {
        /* $gp did not address the canonical global, or the game rewrote it inside the renderer. */
        ++g_diagnostics.invalid_globals;
        return;
    }

    ++g_diagnostics.substituted_loads;
    if (far_load) {
        ++g_diagnostics.far_load_substitutions;
        /* Rooms are walked to the margin, objects stop where the fog ends. */
        const bool object = site.pc != kPortalFarLoad;
        if (object) cpu->gpr[loaded] = g_written.visual_far;
        else cpu->gpr[loaded] = portal_far(cpu->gpr[18]);
        arm_pending_decision(site.pc, g_written.source_far, cpu->gpr[loaded]);
    } else {
        ++g_diagnostics.fade_load_substitutions;
    }
}

bool observer_site(uint32_t pc, uint32_t opcode) {
    if ((pc == kTraversalDepthLoad &&
         opcode == kTraversalDepthLoadOpcode) ||
        (pc == kTraversalRoomMark &&
         opcode == kTraversalRoomMarkOpcode) ||
        (pc == kPortalFirstCandidate &&
         opcode == kPortalFirstCandidateOpcode) ||
        (pc == kPortalSecondCandidate &&
         opcode == kPortalSecondCandidateOpcode) ||
        (pc == kPortalFinalCandidate &&
         opcode == kPortalFinalCandidateOpcode) ||
        (pc == kAfterTraversal && opcode == kAfterTraversalOpcode) ||
        (pc == kBeforeVisibleLoop &&
         opcode == kBeforeVisibleLoopOpcode) ||
        (pc == kAfterVisibleLoop &&
         opcode == kAfterVisibleLoopOpcode)) {
        return true;
    }
    return find_object_decision_site(pc, opcode) != nullptr;
}

void observe_instruction(CPUState *cpu, uint32_t pc, uint32_t opcode) {
    if (!cpu || !observer_site(pc, opcode)) return;

    std::lock_guard<std::mutex> lock(g_mutex);
    if (comparator_active() || psx_netplay_active()) {
        retire_for_transition();
        return;
    }
    if (!g_frame.active) return;

    if (pc == kTraversalDepthLoad) {
        if (!traversal_return_address(cpu->gpr[31])) return;
        const uint32_t depth = cpu->gpr[19]; /* $s3, loaded as a byte */
        ++g_diagnostics.traversal_recursion_entries;
        if (depth >= kTraversalDepthCap)
            ++g_diagnostics.traversal_cap_hits;
        g_frame.traversal_depth_seen = true;
        g_frame.traversal_max_depth =
            std::max(g_frame.traversal_max_depth, depth);
        if (!g_written.active) return;
        const disruptor::sector_reach::Entry entry = disruptor::sector_reach::enter(
            disruptor::Guest{}, g_walk, cpu->gpr[29], depth, g_written.source_far,
            portal_far(psx_mod_read_word(cpu->gpr[29] + disruptor::sector_reach::kPortalOnStack)));
        if (!entry.agrees) ++g_diagnostics.portal_model_mismatches;
        if (entry.hold) {
            cpu->gpr[19] = disruptor::sector_reach::kDepthCap;
            ++g_diagnostics.sector_requests_held;
        }
        return;
    }

    if (pc == kTraversalRoomMark) {
        if (g_frame.traversal_rooms != UINT32_MAX)
            ++g_frame.traversal_rooms;
        return;
    }

    if (pc == kPortalFirstCandidate || pc == kPortalSecondCandidate ||
        pc == kPortalFinalCandidate) {
        check_portal_copy(cpu, pc);
        observe_portal_candidate(cpu, pc);
        return;
    }

    if (const ObjectDecisionSite *site =
            find_object_decision_site(pc, opcode)) {
        observe_object_decision(cpu, *site);
        return;
    }

    observe_renderer_boundary(cpu, pc);
}

double percentile(std::array<double, kTimingHistory> values, size_t count,
                  double fraction) {
    if (count == 0u) return 0.0;
    std::sort(values.begin(), values.begin() + count);
    const size_t index = static_cast<size_t>(std::ceil(
        fraction * static_cast<double>(count))) - 1u;
    return values[std::min(index, count - 1u)];
}

void room_objects_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kRoomObjects) return;
    if (g_frame.entrance_saved_far) {
        std::lock_guard<std::mutex> lock(g_mutex);
        const uint32_t end = psx_mod_read_word(kPrimitivePointer);
        restore_entrance_far();
        discard_reallocated_fog(end);
        tag_entrance_mesh(end);
    }
    close_admitted(psx_mod_read_word(kPrimitivePointer));
    g_face_rule = FaceRule{};
}

PSX_MOD_CONSTRUCTOR(register_disruptor_far_rendering) {
    if (!psx_mod_register_function_entry_plugin(
            "disruptor.far_rendering.renderer", kRendererEntry,
            disruptor_far_rendering_renderer_entry) ||
        !psx_mod_register_function_entry_plugin(
            "disruptor.far_rendering.sector_edges", kDrawSector,
            disruptor_far_rendering_sector_entry) ||
        !psx_mod_register_function_entry_plugin("disruptor.far_rendering.room_objects", kRoomObjects, room_objects_entry)) {
        std::fprintf(stderr,
                     "disruptor: failed to register far-rendering hook\n");
    }
}

/* t4..t7 hold the corner records' first words: a face with a marked corner is tested on its corners' real sides. */
void keep_marked_faces_on_screen(CPUState *cpu) {
    using namespace disruptor::marked_corners;
    if (!cpu || !cpu->read_word || cpu->gpr[1] == 0u) return;
    const std::array<uint32_t, 4> words = {cpu->gpr[12], cpu->gpr[13], cpu->gpr[14], cpu->gpr[15]};
    if (((words[0] | words[1] | words[2] | words[3]) & kMarked) == 0u) return;
    const std::array<uint32_t, 4> screen = {cpu->read_word(cpu->gpr[8] + 4u), cpu->read_word(cpu->gpr[9] + 4u),
                                            cpu->read_word(cpu->gpr[10] + 4u), cpu->read_word(cpu->gpr[11] + 4u)};
    cpu->gpr[1] = shared_sides(words, screen, static_cast<int32_t>(cpu->read_word(kSpanLeft)),
                               static_cast<int32_t>(cpu->read_word(kSpanRight)));
}

void keep_visible_quad_on_screen(CPUState *cpu) {
    if (!cpu || !cpu->read_word || !gpu_geometry_correction_enabled() || psx_netplay_active() || comparator_active() ||
        cpu->gpr[1] != 0u || cpu->gpr[23] != 0u || (cpu->gpr[30] & 1u) ||
        (cpu->gpr[7] & disruptor::quad_visibility::kTwoSided))
        return;
    cpu->gpr[7] = disruptor::ice_winding::culling_flags(cpu->gpr[6], cpu->gpr[7], cpu->read_word(0x1F800054u), cpu->read_word);
    for (int corner = 0; corner < 4; ++corner)
        if (cpu->gpr[12 + corner] & (disruptor::marked_corners::kMarked | disruptor::marked_corners::kNear)) return;
    const std::array<uint32_t, 4> screen = {cpu->read_word(cpu->gpr[8] + 4u), cpu->read_word(cpu->gpr[9] + 4u),
                                           cpu->read_word(cpu->gpr[10] + 4u), cpu->read_word(cpu->gpr[11] + 4u)};
    cpu->gpr[7] = disruptor::quad_visibility::culling_flags(screen, cpu->gpr[7]);
}

/* a1 at the middle point, corners packed in t0..t3, the camera in at, v0, v1. */
void put_split_centre_on_diagonal(CPUState *cpu) {
    if (!cpu || !cpu->write_word) return;
    const std::array<uint32_t, 2> centre = disruptor::near_split::centre_on_diagonal(
        cpu->gpr[9], cpu->gpr[11], {cpu->gpr[1], cpu->gpr[2], cpu->gpr[3]});
    cpu->write_word(cpu->gpr[5], centre[0]);
    cpu->write_word(cpu->gpr[5] + 4u, centre[1]);
}

/* t9 at the quarter's packet, t8 the tile table and a2 the face, both unchanged through the split. */
void meet_quarters_on_texel_16(CPUState *cpu, disruptor::near_split::Quarter quarter) {
    using namespace disruptor::near_split;
    if (!cpu || !cpu->read_word || !cpu->write_word) return;
    const uint32_t entry = uv_entry(cpu->gpr[24], cpu->gpr[6]);
    const uint32_t base = cpu->read_word(entry);
    if (!square_tile(base, cpu->read_word(entry + 4u))) return;
    const uint32_t packet = cpu->gpr[25];
    std::array<uint32_t, 4> words{};
    for (uint32_t slot = 0; slot < 4u; ++slot) words[slot] = cpu->read_word(packet + 12u + 12u * slot);
    words = quarter_uvs(quarter, words, base);
    for (uint32_t slot = 0; slot < 4u; ++slot) cpu->write_word(packet + 12u + 12u * slot, words[slot]);
}

}  // namespace

extern "C" void disruptor_far_rendering_trim_triton_balcony(void) {
    if (!comparator_active() && !psx_netplay_active()) trim_triton_balcony();
}

extern "C" void disruptor_far_rendering_force_from_environment(void) {
    const char *forced = std::getenv("PSX_DISRUPTOR_DRAW_DISTANCE");
    if (!forced) return;
    constexpr std::array<int, DISRUPTOR_FAR_RENDERING_PRESET_COUNT> kPercents = {100, 125, 150, 200};
    const auto found = std::find(kPercents.begin(), kPercents.end(), std::atoi(forced));
    if (found != kPercents.end()) (void)disruptor_far_rendering_set_preset(static_cast<int>(found - kPercents.begin()));
}

extern "C" int disruptor_far_rendering_set_preset(int preset) {
    if (preset < DISRUPTOR_FAR_RENDERING_RETAIL ||
        preset >= DISRUPTOR_FAR_RENDERING_PRESET_COUNT) {
        return DISRUPTOR_FAR_RENDERING_INVALID_PRESET;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    /* Retail is always a safe escape hatch, including a mid-frame netplay or
     * comparator transition.  New enhancements remain neutral in both
     * comparator halves. */
    if (preset == DISRUPTOR_FAR_RENDERING_RETAIL) {
        abandon_active_frame(false);
        if (g_preset != preset) reset_comparison_metrics();
        g_preset = preset;
        return DISRUPTOR_FAR_RENDERING_OK;
    }
    if (comparator_active()) return DISRUPTOR_FAR_RENDERING_INVALID_PRESET;

    if (psx_netplay_active()) {
        force_retail_for_netplay();
        return DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED;
    }

    /* Menu changes normally happen between render calls, but this also makes
     * direct/test callers safe when a toggle lands during an active frame. */
    abandon_active_frame(false);
    if (g_preset != preset) reset_comparison_metrics();
    g_preset = preset;
    return DISRUPTOR_FAR_RENDERING_OK;
}

extern "C" int disruptor_far_rendering_get_preset(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    force_retail_for_netplay();
    return g_preset;
}

extern "C" int disruptor_far_rendering_set_depth_fade_mode(int mode) {
    if (mode < DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL ||
        mode >= DISRUPTOR_FAR_RENDERING_DEPTH_FADE_MODE_COUNT) {
        return DISRUPTOR_FAR_RENDERING_INVALID_PRESET;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    /* Restoring the retail ramp is always permitted as the safe escape hatch,
     * even if the comparator became active after the diagnostic was enabled. */
    if (mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL) {
        abandon_active_frame(false);
        if (g_depth_fade_mode != mode) reset_comparison_metrics();
        g_depth_fade_mode = mode;
        return DISRUPTOR_FAR_RENDERING_OK;
    }
    if (comparator_active()) return DISRUPTOR_FAR_RENDERING_INVALID_PRESET;

    if (psx_netplay_active()) {
        force_retail_for_netplay();
        return DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED;
    }

    abandon_active_frame(false);
    if (g_depth_fade_mode != mode) reset_comparison_metrics();
    g_depth_fade_mode = mode;
    return DISRUPTOR_FAR_RENDERING_OK;
}

extern "C" int disruptor_far_rendering_get_depth_fade_mode(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    force_retail_for_netplay();
    return g_depth_fade_mode;
}

extern "C" int disruptor_far_rendering_gameplay_ready(void) {
    return plausible_live_gameplay() ? 1 : 0;
}

extern "C" int disruptor_far_rendering_netplay_blocked(void) {
    if (!psx_netplay_active()) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    force_retail_for_netplay();
    return 1;
}

extern "C" void disruptor_far_rendering_drawn_distances(uint32_t *far_distance, int32_t *fade_start) {
    if (!far_distance || !fade_start) return;
    *far_distance = psx_mod_read_word(kFarDistance);
    if (g_written.active && *far_distance == g_written.effective_far) *far_distance = g_written.visual_far;
    *fade_start = static_cast<int32_t>(psx_mod_read_word(kFadeStart));
}

extern "C" int disruptor_far_rendering_get_diagnostics(
        DisruptorFarRenderingDiagnostics *out, uint32_t capacity) {
    /* The version and full structure size occupy the first eight bytes.  A
     * smaller buffer cannot even identify the returned ABI safely. */
    if (!out || capacity < 2u * sizeof(uint32_t)) return 0;

    DisruptorFarRenderingDiagnostics snapshot{};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        force_retail_for_netplay();
        snapshot.abi_version =
            DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION;
        snapshot.struct_size = sizeof(snapshot);
        snapshot.preset = g_preset;
        snapshot.active = g_frame.active ? 1 : 0;

        snapshot.renderer_entries = g_diagnostics.renderer_entries;
        snapshot.completed_frames = g_diagnostics.completed_frames;
        snapshot.substituted_loads = g_diagnostics.substituted_loads;
        snapshot.rejected_entries = g_diagnostics.rejected_entries;
        snapshot.nested_entries = g_diagnostics.nested_entries;
        snapshot.missed_epilogues = g_diagnostics.missed_epilogues;
        snapshot.invalid_globals = g_diagnostics.invalid_globals;
        snapshot.primitive_samples = g_diagnostics.primitive_samples;

        snapshot.source_far_distance = g_diagnostics.source_far;
        snapshot.source_fade_start = g_diagnostics.source_fade;
        snapshot.effective_far_distance = g_diagnostics.effective_far;
        snapshot.effective_fade_start = g_diagnostics.effective_fade;
        snapshot.primitive_start = g_diagnostics.primitive_start;
        snapshot.primitive_end = g_diagnostics.primitive_end;
        snapshot.primitive_last_delta =
            g_diagnostics.primitive_last_delta;
        snapshot.primitive_high_water =
            g_diagnostics.primitive_high_water;

        snapshot.render_wall_us_p50 = percentile(
            g_diagnostics.render_wall_us,
            g_diagnostics.render_wall_us_count, 0.50);
        snapshot.render_wall_us_p95 = percentile(
            g_diagnostics.render_wall_us,
            g_diagnostics.render_wall_us_count, 0.95);
        snapshot.render_wall_us_max = g_diagnostics.render_wall_us_max;

        snapshot.depth_fade_mode = g_depth_fade_mode;
        snapshot.far_load_substitutions =
            g_diagnostics.far_load_substitutions;
        snapshot.fade_load_substitutions =
            g_diagnostics.fade_load_substitutions;

        snapshot.traversal_recursion_entries =
            g_diagnostics.traversal_recursion_entries;
        snapshot.traversal_cap_hits = g_diagnostics.traversal_cap_hits;
        snapshot.portal_far_tests = g_diagnostics.portal_far_tests;
        snapshot.portal_shortcut_relaxations =
            g_diagnostics.portal_shortcut_relaxations;
        snapshot.portal_final_decisions =
            g_diagnostics.portal_final_decisions;
        snapshot.portal_final_effective_rejections =
            g_diagnostics.portal_final_effective_rejections;
        snapshot.portal_final_decision_flips =
            g_diagnostics.portal_final_decision_flips;
        snapshot.object_far_decisions =
            g_diagnostics.object_far_decisions;
        snapshot.object_effective_rejections =
            g_diagnostics.object_effective_rejections;
        snapshot.object_decision_flips =
            g_diagnostics.object_decision_flips;
        snapshot.observer_sequence_errors =
            g_diagnostics.observer_sequence_errors;
        snapshot.visibility_samples = g_diagnostics.visibility_samples;
        snapshot.packet_stage_samples =
            g_diagnostics.packet_stage_samples;
        snapshot.traversal_rooms_last =
            g_diagnostics.traversal_rooms_last;
        snapshot.traversal_rooms_high_water =
            g_diagnostics.traversal_rooms_high_water;
        snapshot.submitted_spans_last =
            g_diagnostics.submitted_spans_last;
        snapshot.submitted_spans_high_water =
            g_diagnostics.submitted_spans_high_water;
        snapshot.traversal_max_depth_last =
            g_diagnostics.traversal_max_depth_last;
        snapshot.traversal_max_depth_high_water =
            g_diagnostics.traversal_max_depth_high_water;
        snapshot.packet_entry_to_traversal_end_last =
            g_diagnostics.packet_entry_to_traversal_end_last;
        snapshot.packet_traversal_to_visible_start_last =
            g_diagnostics.packet_traversal_to_visible_start_last;
        snapshot.packet_visible_loop_last =
            g_diagnostics.packet_visible_loop_last;
        snapshot.packet_post_visible_loop_last =
            g_diagnostics.packet_post_visible_loop_last;
        snapshot.sector_requests_held = g_diagnostics.sector_requests_held;
        snapshot.portal_model_mismatches = g_diagnostics.portal_model_mismatches;
    }

    /* Readiness is sampled outside the diagnostics lock because it performs
     * guest-memory reads and has no bearing on the internally coherent
     * counter snapshot above. */
    snapshot.gameplay_ready = plausible_live_gameplay() ? 1 : 0;
    snapshot.netplay_blocked = psx_netplay_active() ? 1 : 0;

    std::memcpy(out, &snapshot,
                std::min<size_t>(capacity, sizeof(snapshot)));
    return 1;
}

extern "C" void disruptor_far_rendering_reset_session(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    abandon_active_frame(false);
    g_fog = FrameFog{};
    g_fog_exponent = 1;
    g_packet_fractions.clear();
    g_preset = DISRUPTOR_FAR_RENDERING_RETAIL;
    g_depth_fade_mode = DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL;
    g_frame = ActiveFrame{};
    g_pending_decision = PendingFarDecision{};
    g_diagnostics = SessionDiagnostics{};
}

extern "C" void disruptor_far_rendering_abandon_metrics(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_written = WrittenDistances{};
    g_frame = ActiveFrame{};
    g_pending_decision = PendingFarDecision{};
    g_fog.active = false;
    g_packet_fractions.clear();
    g_spans.forget();
    g_beside.forget();
    g_objects.forget();
    g_sight.clear();
    g_unlisted = Unlisted{};
    g_unlisted_mesh_count = 0u;
    g_face_rule = FaceRule{};
    g_admitted_from = 0u;
}

extern "C" void disruptor_far_rendering_state_saving(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    restore_distances();
    close_spans();
    show_objects();
    relist_room();
}

extern "C" int disruptor_far_rendering_portal_reach(void) { return portal_reach_enabled() ? 1 : 0; }

extern "C" int disruptor_far_rendering_edge_margin(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return edge_margin_active() ? 1 : 0;
}

extern "C" void disruptor_far_rendering_fog_ramp(int32_t *start, int32_t *length, int *exponent) {
    if (!start || !length || !exponent) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_fog.active && g_fog.smooth) {
        *start = g_fog.start;
        *length = g_fog.length;
        *exponent = g_fog.exponent;
        return;
    }
    *start = static_cast<int32_t>(psx_mod_read_word(kFadeStart));
    *length = kFadeDepth;
    *exponent = 1;
}

extern "C" void disruptor_far_rendering_set_fog_exponent(int exponent) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fog_exponent = std::clamp(exponent, 1, 3);
}

extern "C" int disruptor_far_rendering_get_fog_exponent(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_fog_exponent;
}

extern "C" int disruptor_far_rendering_packet_fraction(uint32_t resolved_packet, uint32_t signature) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto found = g_packet_fractions.find(resolved_packet);
    if (found == g_packet_fractions.end()) return -1;
    PacketFraction& share = found->second;
    if (!share.drawn) {
        share.drawn = true;
        share.signature = signature;
    } else if (share.signature != signature) {
        g_packet_fractions.erase(found);
        return -1;
    }
    return share.fade;
}

extern "C" int disruptor_far_rendering_list_fade_rows(uint32_t packet, int32_t from, int32_t to, int32_t *rows) {
    if (!rows) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (packet == 0u || packet != g_list_shaped || !g_fog.active) return 0;
    const int64_t pressed = pressed_into_fog(int64_t{to} - g_fog.start);
    *rows = fog_row(pressed) - fog_row(pressed_into_fog(int64_t{from} - g_fog.start));
    const auto found = g_fog.smooth ? g_packet_fractions.find(psx_mod_gpu_dma_resolve_address(packet)) : g_packet_fractions.end();
    if (found != g_packet_fractions.end()) found->second.fade = sprite_fog_level(pressed);
    return 1;
}

extern "C" int disruptor_far_rendering_save_deferred(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_frame.active && g_frame.extended ? 1 : 0;
}

static void begin_renderer_frame(CPUState *cpu);

extern "C" void disruptor_far_rendering_renderer_entry(
        CPUState *cpu, uint32_t address) {
    if (!cpu || address != kRendererEntry) return;
    begin_renderer_frame(cpu);
    if (!comparator_active()) disruptor_render_arena_publish_fades();
}

static void draw_rooftops_house_sides(CPUState *cpu, uint32_t mesh, const disruptor::Guest &guest) {
    namespace houses = disruptor::rooftops_houses;
    if (cpu->gpr[31] != 0x800411A4u || !visible_entry(cpu->gpr[17]) ||
        guest.byte(0x80071354u) != 3u || guest.byte(0x80071518u) != 3u ||
        comparator_active() || psx_netplay_active() || !disruptor_render_arena_relocated()) return;
    const uint32_t sector = guest.byte(cpu->gpr[17] + disruptor::portal_reach::kRoomByte);
    if (sector < 214u || sector > 245u) return;
    struct House {
        uint32_t sector, x, z, y, header, lights;
        decltype(houses::south) *walls;
    };
    static constexpr std::array<House, 7> scenery{{
        {214u, 8992u, 1620u, 1600u, 0x4015u, 0x08u, &houses::inner_north},
        {224u, 10752u, 2720u, 1772u, 0x2C02u, 0x04u, &houses::outer_south},
        {226u, 10752u, 1860u, 1772u, 0x8C21u, 0x14u, &houses::outer_north},
        {242u, 8288u, 3360u, 1472u, 0x44CFFu, 0x80u, &houses::south},
        {243u, 8096u, 1680u, 1472u, 0x450FFu, 0x84u, &houses::north},
        {244u, 9728u, 3296u, 1520u, 0x44CFFu, 0x80u, &houses::south},
        {245u, 9922u, 1738u, 1640u, 0x444FFu, 0x78u, &houses::narrow_north},
    }};
    const auto house = std::find_if(scenery.begin(), scenery.end(), [sector](const House &side) { return side.sector == sector; });
    if (house == scenery.end()) return;
    const uint32_t room = guest.word(disruptor::sector_reach::kSectors + 4u * sector);
    if (!valid_main_ram_pointer(room, 36u) || guest.word(room + 16u) != mesh ||
        guest.word(room + 24u) != house->x || guest.word(room + 28u) != house->z || guest.word(room + 32u) != house->y ||
        !valid_main_ram_pointer(mesh, 8u) || guest.word(mesh) != house->header || guest.word(mesh + 4u) != house->lights) return;
    draw_mesh_addition(cpu, *house->walls);
}

extern "C" void disruptor_far_rendering_sector_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kDrawSector || !cpu->read_word || !cpu->write_word || drawing_mesh_addition) return;
    note_room(cpu);
    disruptor::Guest guest;
    repair_pipe_elbow(cpu, cpu->gpr[4]);
    disruptor::misplaced_riser::fixed_parts(guest, cpu->gpr[4]);
    if (gpu_geometry_correction_enabled()) {
        const uint32_t span = cpu->gpr[5];
        const uint32_t left = cpu->read_word(span + 12u), right = cpu->read_word(span + 16u);
        if (disruptor::portal_reach::turn_guarded_left(left) != left)
            cpu->write_word(span + 12u, disruptor::portal_reach::turn_guarded_left(left));
        if (disruptor::portal_reach::turn_guarded_right(right) != right)
            cpu->write_word(span + 16u, disruptor::portal_reach::turn_guarded_right(right));
    }
    draw_rooftops_house_sides(cpu, cpu->gpr[4], guest);
    draw_upper_deck_floor(cpu, cpu->gpr[4]);
}

static void begin_renderer_frame(CPUState *cpu) {
    if (comparator_active()) {
        /* A comparator transition cannot create a new sample, but it must
         * still retire a pre-existing override if its epilogue was missed. */
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_frame.active) abandon_active_frame(true);
        g_fog.active = false;
        g_packet_fractions.clear();
        return;
    }

    g_face_rule = FaceRule{};
    g_admitted_from = 0u;
    gpu_supplemental_frame();
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_diagnostics.renderer_entries;
    g_fog.active = false;
    ++g_fog_serial;
    std::erase_if(g_packet_fractions, [](const auto& entry) { return entry.second.serial + 1u < g_fog_serial; });

    if (psx_netplay_active()) {
        if (g_frame.active) {
            ++g_diagnostics.nested_entries;
            abandon_active_frame(true);
        }
        if (g_preset != DISRUPTOR_FAR_RENDERING_RETAIL ||
            g_depth_fade_mode !=
                DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL)
            ++g_diagnostics.rejected_entries;
        force_retail_for_netplay();
        return;
    }

    if (g_frame.active) {
        ++g_diagnostics.nested_entries;
        ++g_diagnostics.rejected_entries;
        abandon_active_frame(true);
        return;
    }

    if (!plausible_live_gameplay()) {
        if (g_preset != DISRUPTOR_FAR_RENDERING_RETAIL ||
            g_depth_fade_mode !=
                DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL)
            ++g_diagnostics.rejected_entries;
        return;
    }

    trim_triton_balcony();

    /* 0x80040ECC resets $gp+0x390 from *(renderer_context+0x70).
     * Reading that source at entry obtains this frame's real arena start,
     * rather than the prior frame's end still present in the global. */
    const uint32_t renderer_context = cpu->gpr[4]; /* $a0 */
    if (!valid_main_ram_pointer(renderer_context,
                                kRendererPrimitiveStartOffset + 3u)) {
        ++g_diagnostics.invalid_globals;
        ++g_diagnostics.rejected_entries;
        return;
    }
    const uint32_t primitive_start = psx_mod_read_word(
        renderer_context + kRendererPrimitiveStartOffset);
    if (!valid_primitive_pointer(primitive_start, 0u)) {
        ++g_diagnostics.invalid_globals;
        ++g_diagnostics.rejected_entries;
        return;
    }

    const uint32_t source_far = psx_mod_read_word(kFarDistance);
    const int32_t source_fade =
        static_cast<int32_t>(psx_mod_read_word(kFadeStart));
    uint32_t effective_far = 0u;
    int32_t effective_fade = 0;
    if (!effective_distances(g_preset, g_depth_fade_mode,
                             source_far, source_fade,
                             &effective_far, &effective_fade)) {
        ++g_diagnostics.invalid_globals;
        ++g_diagnostics.rejected_entries;
        return;
    }

    make_room_for_margin(source_far, &effective_far, &effective_fade);
    g_diagnostics.source_far = source_far;
    g_diagnostics.source_fade = source_fade;
    g_diagnostics.effective_far = effective_far;
    g_diagnostics.effective_fade = effective_fade;
    g_diagnostics.primitive_start = primitive_start;

    g_frame = ActiveFrame{};
    g_pending_decision = PendingFarDecision{};
    g_walk = disruptor::sector_reach::Walk{};
    g_frame.active = true;
    g_frame.primitive_start = primitive_start;
    std::erase_if(g_packet_fractions, [primitive_start](const auto &entry) { return entry.second.arena == primitive_start; });
    g_frame.started = Clock::now();
    g_frame.entrance_reach = fortress_entrance_reach(effective_far, g_frame.entrance_visibility);
    write_distances(source_far, source_fade, effective_far, effective_fade);
    g_frame.extended = g_written.active;
    g_frame.reach = portal_reach_enabled();
    g_sight.clear();
    set_frame_fog(source_fade, effective_far, effective_fade);
}

extern "C" void disruptor_far_rendering_instruction_hook(
        CPUState *cpu, uint32_t pc, uint32_t instruction, int phase) {
    if (phase != DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION) return;

    if (pc == kSharedSides && instruction == kSharedSidesOpcode) {
        keep_marked_faces_on_screen(cpu);
        note_face(cpu);
        keep_visible_quad_on_screen(cpu);
        return;
    }

    if (pc == kNearSplitCentre && instruction == kNearSplitCentreOpcode) {
        put_split_centre_on_diagonal(cpu);
        return;
    }

    if (instruction == kQuarterUvOpcode &&
        (pc == kQuarterCorner0Uv || pc == kQuarterCorner1Uv || pc == kQuarterCorner3Uv)) {
        using disruptor::near_split::Quarter;
        meet_quarters_on_texel_16(cpu, pc == kQuarterCorner0Uv   ? Quarter::corner0
                                       : pc == kQuarterCorner1Uv ? Quarter::corner1
                                                                 : Quarter::corner3);
        return;
    }

    if (pc == kRendererRestore && instruction == kRendererRestoreOpcode) {
        /* Cleanup must run even if netplay, comparator, or gameplay state
         * changed after entry. */
        std::lock_guard<std::mutex> lock(g_mutex);
        if (psx_netplay_active()) {
            force_retail_for_netplay();
        } else if (comparator_active()) {
            abandon_active_frame(false);
        } else {
            finish_active_frame();
        }
        return;
    }

    if (const DistanceLoadSite *site =
            find_distance_load_site(pc, instruction)) {
        observe_distance_load(cpu, *site);
        if (site->kind == DistanceKind::Fade) shape_fade_load(cpu, *site);
        return;
    }

    observe_instruction(cpu, pc, instruction);
    if (pc == kTraversalDepthLoad && instruction == kTraversalDepthLoadOpcode) widen_walk(cpu);
    if (pc == kPortalFirstCandidate && instruction == kPortalFirstCandidateOpcode) face_portal(cpu);
    if (pc == kAfterTraversal && instruction == kAfterTraversalOpcode) {
        std::lock_guard<std::mutex> lock(g_mutex);
        relist_room();
    }
    if (pc == kBeforeVisibleLoop && instruction == kBeforeVisibleLoopOpcode) open_spans(cpu);
    if (pc == kAfterVisibleLoop && instruction == kAfterVisibleLoopOpcode) keep_game_rooms();
    if (pc == kBeforeVisibleLoop && instruction == kBeforeVisibleLoopOpcode && !comparator_active())
        disruptor_render_arena_publish_fades();
}
