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

#include "cpu_state.h"
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
extern "C" int gl_renderer_smooth_fade(void);

namespace {

constexpr uint32_t kRendererEntry = 0x80040E68u;
constexpr uint32_t kRendererRestore = 0x8004279Cu;
constexpr uint32_t kRendererRestoreOpcode = 0x8FBF016Cu;

constexpr uint32_t kFarDistance = 0x800715ECu;
constexpr uint32_t kFadeStart = 0x800715E0u;
constexpr uint32_t kPrimitivePointer = 0x800714DCu; /* $gp + 0x390 */
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

constexpr uint32_t kCurrentHealth = 0x80077660u;
constexpr uint32_t kMaximumHealth = 0x80077664u;
constexpr uint32_t kSceneMode = 0x8007145Cu;
constexpr uint32_t kScriptedCamera = 0x800714E0u;
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

    bool rewrite_after_effects = false;
    bool extended = false;  /* written at some point of this frame: a save waits for its end */
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
constexpr auto kFogSites = std::array<FogSite, 8>{{
    {0x8003B2CCu, RangeFrom::HalfRegister, 3u, false, false},
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
    uint8_t fraction;
    bool drawn = false;       /* the packet's words hash to `signature` since its first draw */
    uint32_t signature = 0u;
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

    /* Mode 1, a scripted camera, and an inactive player cover the known map,
     * cutscene, death, and level-transition renderer states. */
    if (psx_mod_read_byte(kSceneMode) == 1u ||
        psx_mod_read_byte(kScriptedCamera) != 0u) {
        return false;
    }

    const uint32_t player = psx_mod_read_word(kPlayerPointer);
    return valid_main_ram_pointer(player, kPlayerInactiveOffset) &&
           psx_mod_read_byte(player + kPlayerInactiveOffset) == 0u;
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

/* Caller owns g_mutex.  The renderer dissolves the world's edge only under its smooth fade, so only then are the meshes kept past it.
 * The margin is guest memory, so a peer or the comparator, whose renderer settings may differ, never gets it. */
bool edge_margin_active() {
    return gl_renderer_smooth_fade() != 0 && g_depth_fade_mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL &&
           !comparator_active() && !psx_netplay_active();
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

/* Caller owns g_mutex.  Under the renderer's smooth fade the fog begins at the game's own start and spreads to the far
 * plane drawn with, shaped by the chosen power; else it is the game's own ramp from the start drawn with. */
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
        const bool valid_end = valid_main_ram_pointer(end, 0u) &&
                               end >= kVisibleSpanBase &&
                               (end - kVisibleSpanBase) %
                                       kVisibleSpanStride ==
                                   0u;
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
    g_fog.active = false;
    clear_pending_decision(false);
    if (!g_frame.active) return;
    if (missed_epilogue) ++g_diagnostics.missed_epilogues;
    g_frame = ActiveFrame{};
}

void finish_active_frame() {
    restore_distances();
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

uint8_t next_row_share(int64_t pressed) {
    const int64_t within = std::clamp<int64_t>(pressed, 0, kFadeDepth - 1);
    return fog_row(pressed) >= kFadeRows - 1 ? 0u : static_cast<uint8_t>(within % kFadeRowDepth * 4);
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
        g_packet_fractions[key] = PacketFraction{g_fog_serial, next_row_share(pressed)};
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
        arm_pending_decision(site.pc, g_written.source_far, object ? g_written.visual_far : g_written.effective_far);
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
        return;
    }

    if (pc == kTraversalRoomMark) {
        if (g_frame.traversal_rooms != UINT32_MAX)
            ++g_frame.traversal_rooms;
        return;
    }

    if (pc == kPortalFirstCandidate || pc == kPortalSecondCandidate ||
        pc == kPortalFinalCandidate) {
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

PSX_MOD_CONSTRUCTOR(register_disruptor_far_rendering) {
    if (!psx_mod_register_function_entry_plugin(
            "disruptor.far_rendering.renderer", kRendererEntry,
            disruptor_far_rendering_renderer_entry)) {
        std::fprintf(stderr,
                     "disruptor: failed to register far-rendering hook\n");
    }
}

}  // namespace

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
}

extern "C" void disruptor_far_rendering_state_saving(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    restore_distances();
}

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
    return share.fraction;
}

extern "C" int disruptor_far_rendering_list_fade_rows(uint32_t packet, int32_t from, int32_t to, int32_t *rows) {
    if (!rows) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (packet == 0u || packet != g_list_shaped || !g_fog.active) return 0;
    const int64_t pressed = pressed_into_fog(int64_t{to} - g_fog.start);
    *rows = fog_row(pressed) - fog_row(pressed_into_fog(int64_t{from} - g_fog.start));
    const auto found = g_fog.smooth ? g_packet_fractions.find(psx_mod_gpu_dma_resolve_address(packet)) : g_packet_fractions.end();
    if (found != g_packet_fractions.end()) found->second.fraction = next_row_share(pressed);
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
    g_frame.active = true;
    g_frame.primitive_start = primitive_start;
    g_frame.started = Clock::now();
    write_distances(source_far, source_fade, effective_far, effective_fade);
    g_frame.extended = g_written.active;
    set_frame_fog(source_fade, effective_far, effective_fade);
}

extern "C" void disruptor_far_rendering_instruction_hook(
        CPUState *cpu, uint32_t pc, uint32_t instruction, int phase) {
    if (phase != DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION) return;

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
    if (pc == kBeforeVisibleLoop && instruction == kBeforeVisibleLoopOpcode && !comparator_active())
        disruptor_render_arena_publish_fades();
}
