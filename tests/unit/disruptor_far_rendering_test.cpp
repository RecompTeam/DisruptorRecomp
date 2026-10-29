#include "cpu_state.h"
#include "mod_plugins.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
PSXModFunctionEntryCallback g_renderer_callback = nullptr;
std::uint32_t g_registered_address = 0u;
bool g_game_started = false;
bool g_netplay = false;
std::vector<std::pair<std::uint32_t, std::uint32_t>> g_writes;
int g_failures = 0;

std::size_t physical(std::uint32_t address) {
    return static_cast<std::size_t>(address & 0x001FFFFFu);
}

void seed_byte(std::uint32_t address, std::uint8_t value) {
    g_ram[physical(address)] = value;
}

void seed_word(std::uint32_t address, std::uint32_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
    g_ram[p + 2] = static_cast<std::uint8_t>(value >> 16);
    g_ram[p + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint32_t load_word(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint32_t>(g_ram[p + 0]) |
           (static_cast<std::uint32_t>(g_ram[p + 1]) << 8) |
           (static_cast<std::uint32_t>(g_ram[p + 2]) << 16) |
           (static_cast<std::uint32_t>(g_ram[p + 3]) << 24);
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" {

int g_ls_replay_active = 0;
int g_ls_mode = 0;

PSXModFunctionEntryCallback g_sector_callback = nullptr;
std::uint32_t g_sector_address = 0u;

int psx_mod_register_function_entry_plugin(
        const char *id, std::uint32_t address,
        PSXModFunctionEntryCallback callback) {
    if (id && std::string(id) == "disruptor.far_rendering.sector_edges" && address && callback && !g_sector_callback) {
        g_sector_address = address;
        g_sector_callback = callback;
        return 1;
    }
    if (!id || std::string(id) != "disruptor.far_rendering.renderer" ||
        address == 0u || !callback || g_renderer_callback) {
        return 0;
    }
    g_registered_address = address;
    g_renderer_callback = callback;
    return 1;
}

int psx_mod_game_started(void) {
    return g_game_started ? 1 : 0;
}

int psx_netplay_active(void) {
    return g_netplay ? 1 : 0;
}

std::uint8_t psx_mod_read_byte(std::uint32_t address) {
    return g_ram[physical(address)];
}

void psx_mod_write_byte(std::uint32_t address, std::uint8_t value) {
    seed_byte(address, value);
}

std::uint32_t psx_mod_gpu_dma_resolve_address(std::uint32_t address) { return address; }

std::uint16_t psx_mod_read_half(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint16_t>(g_ram[p + 0]) |
           (static_cast<std::uint16_t>(g_ram[p + 1]) << 8);
}

void psx_mod_write_half(std::uint32_t address, std::uint16_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::uint32_t psx_mod_read_word(std::uint32_t address) {
    return load_word(address);
}

void psx_mod_write_word(std::uint32_t address, std::uint32_t value) {
    g_writes.emplace_back(address, value);
    seed_word(address, value);
}

}  // extern "C"

#include "../../src/game/render/far_rendering.cpp"

namespace {
int g_smooth_fade = 0;
int g_arena_relocated = 0;
int g_published = 0;
std::uint32_t g_published_far = 0u;
std::int32_t g_published_fade = 0;
}  // namespace

extern "C" int gl_renderer_smooth_fade(void) { return g_smooth_fade; }
int g_geometry_correction = 1;
extern "C" int gpu_geometry_correction_enabled(void) { return g_geometry_correction; }
extern "C" int psx_ws_project_x(int x) { return x; }
extern "C" int disruptor_render_arena_relocated(void) { return g_arena_relocated; }

extern "C" void disruptor_render_arena_publish_fades(void) {
    ++g_published;
    disruptor_far_rendering_drawn_distances(&g_published_far, &g_published_fade);
}

namespace {

constexpr std::uint32_t kTestPlayer = 0x80010000u;
constexpr std::uint32_t kWeaponLoad = 0x80071324u, kWantedWeapon = 0x80077627u, kHeldWeapon = 0x80071484u;
constexpr std::uint32_t kRendererContext = 0x80020000u;
constexpr std::uint32_t kTestStack = 0x80030000u;
constexpr std::uint32_t kPrimitiveBuffer = 0x80040000u;
constexpr std::uint32_t kSprite = 0x5EA5u;  /* what the words of a drawn sprite hash to */
constexpr std::uint32_t kReviewedVisibleSpanBase = 0x80079D18u;

static_assert(kVisibleSpanBase == kReviewedVisibleSpanBase,
              "runtime visible-span base must match the reviewed guest arena");

struct DiagnosticsV1Prefix {
    std::uint32_t abi_version;
    std::uint32_t struct_size;
    std::int32_t preset;
    std::int32_t active;
    std::int32_t gameplay_ready;
    std::int32_t netplay_blocked;
    std::uint64_t renderer_entries;
    std::uint64_t completed_frames;
    std::uint64_t substituted_loads;
    std::uint64_t rejected_entries;
    std::uint64_t nested_entries;
    std::uint64_t missed_epilogues;
    std::uint64_t invalid_globals;
    std::uint64_t primitive_samples;
    std::uint32_t source_far_distance;
    std::int32_t source_fade_start;
    std::uint32_t effective_far_distance;
    std::int32_t effective_fade_start;
    std::uint32_t primitive_start;
    std::uint32_t primitive_end;
    std::uint32_t primitive_last_delta;
    std::uint32_t primitive_high_water;
    double render_wall_us_p50;
    double render_wall_us_p95;
    double render_wall_us_max;
};

static_assert(sizeof(DiagnosticsV1Prefix) ==
                  offsetof(DisruptorFarRenderingDiagnostics,
                           depth_fade_mode),
              "diagnostics ABI v2 must append to the complete v1 prefix");

struct DiagnosticsV2Prefix {
    DiagnosticsV1Prefix v1;
    std::int32_t depth_fade_mode;
    std::uint32_t reserved_v2;
    std::uint64_t far_load_substitutions;
    std::uint64_t fade_load_substitutions;
};

static_assert(sizeof(DiagnosticsV2Prefix) ==
                  offsetof(DisruptorFarRenderingDiagnostics,
                           traversal_recursion_entries),
              "diagnostics ABI v3 must append to the complete v2 prefix");

DisruptorFarRenderingDiagnostics diagnostics() {
    DisruptorFarRenderingDiagnostics value{};
    expect(disruptor_far_rendering_get_diagnostics(
               &value, static_cast<std::uint32_t>(sizeof(value))) == 1,
           "diagnostics snapshot succeeds");
    return value;
}

void reset_state() {
    disruptor_far_rendering_reset_session();
    g_ram.fill(0u);
    g_game_started = false;
    g_netplay = false;
    g_ls_mode = 0;
    g_ls_replay_active = 0;
    g_writes.clear();
    g_smooth_fade = 0;
    g_arena_relocated = 0;
}

void seed_live_game(CPUState *cpu, std::uint32_t far_distance = 1024u,
                    std::int32_t fade_start = 256) {
    g_game_started = true;
    seed_word(kMaximumHealth, 100u);
    seed_word(kCurrentHealth, 80u);
    seed_byte(kSceneMode, 0u);
    seed_byte(kScriptedCamera, 0u);
    seed_word(kPlayerPointer, kTestPlayer);
    seed_byte(kTestPlayer + kPlayerInactiveOffset, 0u);
    seed_word(kFarDistance, far_distance);
    seed_word(kFadeStart, static_cast<std::uint32_t>(fade_start));
    seed_word(kRendererContext + kRendererPrimitiveStartOffset,
              kPrimitiveBuffer);
    seed_word(kPrimitivePointer, kPrimitiveBuffer);
    if (cpu) {
        cpu->gpr[4] = kRendererContext;
        cpu->gpr[29] = kTestStack;
    }
}

void enter(CPUState *cpu) {
    g_renderer_callback(cpu, kRendererEntry);
}

void exit_renderer(CPUState *cpu) {
    disruptor_far_rendering_instruction_hook(
        cpu, kRendererRestore, kRendererRestoreOpcode,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
}

void hook_after(CPUState *cpu, std::uint32_t pc, std::uint32_t opcode) {
    disruptor_far_rendering_instruction_hook(
        cpu, pc, opcode, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
}

void invoke_load(CPUState *cpu, const DistanceLoadSite& site,
                 std::uint32_t saved_ra) {
    seed_word(cpu->gpr[29] + site.saved_ra_offset, saved_ra);
    const std::uint32_t destination = (site.opcode >> 16u) & 31u;
    cpu->gpr[destination] =
        site.kind == DistanceKind::Far
            ? load_word(kFarDistance)
            : load_word(kFadeStart);
    disruptor_far_rendering_instruction_hook(
        cpu, site.pc, site.opcode,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
}

void invoke_first_far(CPUState *cpu) {
    invoke_load(cpu, kDistanceLoadSites[0], 0x800410E0u);
}

void invoke_first_fade(CPUState *cpu) {
    invoke_load(cpu, kDistanceLoadSites[5], 0x8004106Cu);
}

bool only_distance_writes() {
    for (const auto& [address, value] : g_writes)
        if (address != kFarDistance && address != kFadeStart) return false;
    return true;
}

void test_registration_and_written_distances() {
    expect(g_renderer_callback != nullptr, "renderer callback registered");
    expect(g_registered_address == kRendererEntry,
           "renderer entry is address pinned");

    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_EXTENDED) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "1.25x preset accepted");
    enter(&cpu);
    expect(load_word(kFarDistance) == 1280u && load_word(kFadeStart) == 512u,
           "renderer entry writes the 1.25x far plane and the fade start shifted by the same delta");

    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1280u, "an exact far load reads the written value");
    invoke_first_fade(&cpu);
    expect(static_cast<std::int32_t>(cpu.gpr[2]) == 512, "an exact fade load reads the written value");

    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x240u);
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u && g_writes.size() == 4u &&
               only_distance_writes(),
           "the epilogue puts the game's own values back, and only the two globals are ever written");
    const auto d = diagnostics();
    expect(d.completed_frames == 1u && d.substituted_loads == 2u,
           "completed frame and exact substituted loads are counted");
    expect(d.far_load_substitutions == 1u &&
               d.fade_load_substitutions == 1u,
           "far and fade substitutions are counted separately");
    expect(d.primitive_samples == 1u &&
               d.primitive_last_delta == 0x240u &&
               d.primitive_high_water == 0x240u,
           "primitive pressure uses context start and epilogue end");
    expect(d.render_wall_us_p50 >= 0.0 &&
               d.render_wall_us_p95 >= d.render_wall_us_p50 &&
               d.render_wall_us_max >= d.render_wall_us_p95,
           "renderer wall-span percentiles are ordered");
}

void test_independent_depth_fade_modes() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);

    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "no-depth-fade diagnostic mode is accepted");
    expect(disruptor_far_rendering_get_preset() ==
               DISRUPTOR_FAR_RENDERING_RETAIL &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED,
           "fade mode is independent of the draw-distance preset");

    enter(&cpu);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u,
           "retail draw distance leaves the far plane as it is");
    invoke_first_fade(&cpu);
    expect(cpu.gpr[2] == 1024u,
           "no-depth-fade moves fade start to the retail far plane");
    auto d = diagnostics();
    expect(d.depth_fade_mode ==
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED &&
               d.source_fade_start == 256 &&
               d.effective_far_distance == 1024u &&
               d.effective_fade_start == 1024,
           "diagnostics expose the zero-width fade interval");
    expect(d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "fade-only diagnostic counts only the audited fade load");
    exit_renderer(&cpu);

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    expect(disruptor_far_rendering_get_depth_fade_mode() ==
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED,
           "distance changes preserve the independently selected fade mode");
    enter(&cpu);
    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    d = diagnostics();
    expect(cpu.gpr[12] == 1536u && cpu.gpr[2] == 1536u,
           "no-depth-fade follows the selected effective far plane");
    expect(d.substituted_loads == 2u &&
               d.far_load_substitutions == 1u &&
               d.fade_load_substitutions == 1u,
           "combined extension reports one far and one fade load");
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "no-depth-fade and extended distance give the game's values back");

    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "retail fade ramp remains the safe escape hatch");
    enter(&cpu);
    invoke_first_fade(&cpu);
    expect(cpu.gpr[2] == 768u,
           "retail ramp still shifts with an extended far plane");
    d = diagnostics();
    expect(d.depth_fade_mode ==
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL &&
               d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "fade-mode changes reset comparison metrics without changing preset");
    exit_renderer(&cpu);
}

void test_drawn_distances_are_the_globals() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    std::uint32_t far_distance = 0u;
    std::int32_t fade_start = 0;
    const auto drawn = [&] { disruptor_far_rendering_drawn_distances(&far_distance, &fade_start); };

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    drawn();
    expect(far_distance == 1024u && fade_start == 256, "outside a written frame they are the game's own values");
    g_published = 0;
    enter(&cpu);
    expect(g_published == 1 && g_published_far == 1536u && g_published_fade == 768,
           "a written entry publishes the preset's values to the renderer's fades");
    drawn();
    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    expect(far_distance == 1536u && fade_start == 768 && cpu.gpr[12] == far_distance &&
               static_cast<std::int32_t>(cpu.gpr[2]) == fade_start,
           "inside the renderer they are the written ones, not scaled again");
    exit_renderer(&cpu);
    drawn();
    expect(far_distance == 1024u && fade_start == 256, "after the epilogue they are the game's again");

    cpu.gpr[4] = 0u;
    enter(&cpu);
    expect(g_published == 2 && g_published_far == 1024u && g_published_fade == 256 && load_word(kFarDistance) == 1024u,
           "a rejected entry publishes the game's values, which the renderer draws with");
    exit_renderer(&cpu);
    cpu.gpr[4] = kRendererContext;
    g_ls_mode = 1;
    enter(&cpu);
    g_ls_mode = 0;
    expect(g_published == 2, "the comparator's entry publishes nothing");
    seed_word(kFarDistance, 1u);
    drawn();
    expect(far_distance == 1u && fade_start == 256, "implausible globals are handed over as they are");
    disruptor_far_rendering_drawn_distances(nullptr, &fade_start);
    disruptor_far_rendering_drawn_distances(&far_distance, nullptr);
}

void test_live_billboard_fade_site() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);

    const DistanceLoadSite& site = kDistanceLoadSites.back();
    expect(site.pc == 0x80043184u && site.opcode == 0x8F820494u &&
               site.kind == DistanceKind::Fade &&
               site.saved_ra_offset == 68u &&
               site.allowed_return_count == 1u &&
               site.allowed_return_addresses[0] == 0x80043FE0u,
           "live billboard fade site retains its exact ancestry contract");

    enter(&cpu);
    invoke_load(&cpu, site, 0x80043FE0u);
    auto d = diagnostics();
    expect(cpu.gpr[2] == 256u && d.substituted_loads == 0u &&
               d.fade_load_substitutions == 0u && g_writes.empty(),
           "Retail writes nothing and leaves the live billboard fade load as it is");
    exit_renderer(&cpu);

    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_FAR) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "1.5x preset is accepted for the billboard fade test");
    enter(&cpu);
    invoke_load(&cpu, site, 0x80043FE0u);
    d = diagnostics();
    expect(cpu.gpr[2] == 768u && d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "1.5x shifts the live billboard retail fade with one fade count");

    invoke_load(&cpu, site, 0x80001234u);
    d = diagnostics();
    expect(d.substituted_loads == 1u && d.fade_load_substitutions == 1u,
           "a wrong billboard saved RA is not counted");
    exit_renderer(&cpu);

    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "nearest-row mode is accepted for the billboard fade test");
    enter(&cpu);
    invoke_load(&cpu, site, 0x80043FE0u);
    d = diagnostics();
    expect(cpu.gpr[2] == 1536u && d.substituted_loads == 1u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 1u,
           "nearest-row mode moves the live billboard fade to the 1.5x far plane");
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u && only_distance_writes(),
           "the billboard fade path gives the game's values back");
}

void test_all_sites_and_ancestry_contracts() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);

    for (const DistanceLoadSite& site : kDistanceLoadSites) {
        const std::uint32_t destination = (site.opcode >> 16u) & 31u;
        const std::uint32_t expected =
            site.kind == DistanceKind::Far ? 1536u : 768u;
        for (std::uint8_t i = 0; i < site.allowed_return_count; ++i) {
            invoke_load(&cpu, site, site.allowed_return_addresses[i]);
            expect(cpu.gpr[destination] == expected,
                   "every audited saved-RA ancestry reads the written value");
        }

        seed_word(cpu.gpr[29] + site.saved_ra_offset, 0x80001234u);
        cpu.gpr[destination] = expected;
        disruptor_far_rendering_instruction_hook(
            &cpu, site.pc, site.opcode,
            DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    }
    auto d = diagnostics();
    expect(d.substituted_loads == 16u &&
               d.far_load_substitutions == 7u &&
               d.fade_load_substitutions == 9u,
           "all exact sites count under the right split counter, other callers not at all");
    exit_renderer(&cpu);

    for (const DistanceLoadSite& site : kDistanceLoadSites) {
        if (site.kind != DistanceKind::Far) continue;
        invoke_load(&cpu, site, site.allowed_return_addresses[0]);
        expect(cpu.gpr[(site.opcode >> 16u) & 31u] == 1024u, "outside the renderer every far load reads the game's own value");
    }
    cpu.gpr[2] = 1024u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x800247C4u, 0x8F8204A0u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 1024u && diagnostics().substituted_loads == 16u,
           "an unreviewed load is neither changed nor counted");
}

void test_retail_baseline_and_metric_reset() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    enter(&cpu);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u,
           "Retail preset leaves exact load destination unchanged");
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x500u);
    exit_renderer(&cpu);
    auto d = diagnostics();
    expect(d.completed_frames == 1u && d.substituted_loads == 0u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 0u &&
               d.primitive_high_water == 0x500u,
           "Retail collects a non-substituted A/B baseline");

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    d = diagnostics();
    expect(d.completed_frames == 0u && d.substituted_loads == 0u &&
               d.far_load_substitutions == 0u &&
               d.fade_load_substitutions == 0u &&
               d.primitive_samples == 0u && d.primitive_high_water == 0u &&
               d.render_wall_us_max == 0.0,
           "preset change resets comparison metrics");
    expect(d.source_far_distance == 0u &&
               d.effective_far_distance == 0u &&
               d.renderer_entries == 1u,
           "preset switch clears old labels but retains safety counters");
}

void test_current_globals_and_signed_fade() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu, 1200u, 432);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    expect(cpu.gpr[12] == 1500u &&
               static_cast<std::int32_t>(cpu.gpr[2]) == 732,
           "each entry scales the current script-provided globals");
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 1200u && load_word(kFadeStart) == 432u,
           "script globals are the game's again after the renderer");

    seed_word(kFarDistance, 512u);
    seed_word(kFadeStart, static_cast<std::uint32_t>(-256));
    enter(&cpu);
    invoke_first_far(&cpu);
    invoke_first_fade(&cpu);
    expect(cpu.gpr[12] == 640u &&
               static_cast<std::int32_t>(cpu.gpr[2]) == -128,
           "negative fade remains signed while shifting with far");
    const auto d = diagnostics();
    expect(d.source_fade_start == -256 &&
               d.effective_fade_start == -128,
           "signed fade is preserved in public diagnostics");
    exit_renderer(&cpu);
    expect(static_cast<std::int32_t>(load_word(kFadeStart)) == -256, "a negative fade start is given back as it was");

    enter(&cpu);
    seed_word(kFarDistance, 900u);
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 900u && static_cast<std::int32_t>(load_word(kFadeStart)) == -256,
           "a global the game rewrote inside the renderer keeps its new value, the other is given back");
    enter(&cpu);
    seed_word(kFadeStart, 100u);
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 900u && load_word(kFadeStart) == 100u,
           "a fade start the game rewrote inside the renderer keeps its new value");
}

void test_guards_and_exact_dispatch() {
    reset_state();
    CPUState cpu{};
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    seed_word(kFarDistance, 1024u);
    seed_word(kFadeStart, 256u);
    cpu.gpr[4] = kRendererContext;
    cpu.gpr[29] = kTestStack;
    seed_word(kRendererContext + kRendererPrimitiveStartOffset, kPrimitiveBuffer);
    enter(&cpu);
    exit_renderer(&cpu);
    expect(g_writes.empty(), "before the game has started nothing is written");

    seed_live_game(&cpu);
    const auto rejected = [&](const char *message) {
        enter(&cpu);
        expect(g_writes.empty() && load_word(kFarDistance) == 1024u, message);
        exit_renderer(&cpu);
    };
    seed_byte(kSceneMode, 1u);
    rejected("map/cutscene scene mode writes nothing");
    seed_byte(kSceneMode, 0u);
    seed_byte(kScriptedCamera, 1u);
    rejected("a scripted camera writes nothing");
    seed_byte(kScriptedCamera, 0u);
    seed_word(kCurrentHealth, 0u);
    rejected("a dead player writes nothing");
    seed_word(kCurrentHealth, 80u);
    seed_word(kFadeStart, 2048u);
    rejected("a fade beyond far writes nothing");
    seed_word(kFadeStart, 256u);
    seed_word(kFarDistance, 0u);
    enter(&cpu);
    expect(g_writes.empty() && load_word(kFarDistance) == 0u, "an invalid distance global writes nothing");
    exit_renderer(&cpu);
    seed_word(kFarDistance, 1024u);

    enter(&cpu);
    expect(load_word(kFarDistance) == 1280u, "a live frame writes the extended far plane");
    seed_word(kTestStack + 164u, 0x800410E0u);
    cpu.gpr[12] = 999u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x8003A914u, 0x8F8C04A0u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[12] == 999u && diagnostics().substituted_loads == 0u && diagnostics().invalid_globals >= 1u,
           "a load that did not read the written value is reported, not changed");

    cpu.gpr[12] = 1280u;
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x8003A914u, 0x8F8C04A0u,
        DISRUPTOR_FAR_RENDERING_BEFORE_INSTRUCTION);
    disruptor_far_rendering_instruction_hook(
        &cpu, 0x8003A914u, 0x8F8C04A1u,
        DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(diagnostics().substituted_loads == 0u, "phase/opcode mismatch is an exact no-op");
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1280u && diagnostics().substituted_loads == 1u, "the exact AFTER hook counts the load");
    exit_renderer(&cpu);
}

void test_weapon_load_keeps_distance() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    seed_word(kPlayerPointer, 0u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1024u, "without the player pointer the retail distance stays");
    exit_renderer(&cpu);
    seed_byte(kWeaponLoad, 1u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1280u, "a weapon's load keeps the distance without the player pointer");
    exit_renderer(&cpu);
    seed_word(kPlayerPointer, kTestPlayer);
    seed_byte(kTestPlayer + kPlayerInactiveOffset, 1u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1280u, "a weapon's load keeps the distance while the player is marked");
    exit_renderer(&cpu);
    seed_byte(kWeaponLoad, 2u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1024u, "another load keeps the retail distance");
    exit_renderer(&cpu);
    seed_byte(kWeaponLoad, 0u);
    seed_byte(kWantedWeapon, 3u);
    seed_byte(kHeldWeapon, 1u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1280u, "empty hands with another weapon wanted are a switch before its load");
    exit_renderer(&cpu);
    seed_byte(kWeaponLoad, 2u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1024u, "not while another load runs");
    exit_renderer(&cpu);
    seed_byte(kWeaponLoad, 0u);
    seed_byte(kHeldWeapon, 3u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1024u, "empty hands with the wanted weapon held are not");
    exit_renderer(&cpu);
}

void test_nested_toggle_netplay_and_lockstep() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 24u);
    enter(&cpu);
    auto d = diagnostics();
    expect(d.nested_entries == 1u && d.missed_epilogues == 1u &&
               d.rejected_entries == 1u,
           "nested/missed entry fails closed for diagnostics");
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "a missed epilogue gives the game's values back at the next entry");

    enter(&cpu);
    expect(load_word(kFarDistance) == 1280u, "the next clean entry writes again");
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "Retail toggle always succeeds");
    expect(diagnostics().active == 0 && load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "a mid-frame toggle abandons the frame and gives the game's values back");
    exit_renderer(&cpu);

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    disruptor_far_rendering_set_depth_fade_mode(
        DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED);
    enter(&cpu);
    expect(load_word(kFarDistance) == 1536u && load_word(kFadeStart) == 1536u, "1.5x without a ramp is written");
    g_netplay = true;
    expect(disruptor_far_rendering_get_preset() ==
               DISRUPTOR_FAR_RENDERING_RETAIL &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL,
           "netplay transition forces distance and fade controls to Retail");
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "the netplay transition gives the game's values back");
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_EXTENDED) ==
               DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED,
           "netplay rejects non-Retail preset");
    expect(disruptor_far_rendering_set_preset(
               DISRUPTOR_FAR_RENDERING_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "Retail remains selectable during netplay");
    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED,
           "netplay rejects no-depth-fade diagnostic mode");
    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL) ==
               DISRUPTOR_FAR_RENDERING_OK,
           "retail fade ramp remains selectable during netplay");

    exit_renderer(&cpu);
    g_netplay = false;
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    g_ls_mode = 1;
    expect(disruptor_far_rendering_set_depth_fade_mode(
               DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED) ==
               DISRUPTOR_FAR_RENDERING_INVALID_PRESET &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL,
           "lockstep comparator rejects no-depth-fade mode");
    exit_renderer(&cpu);
    expect(diagnostics().completed_frames == 0u && load_word(kFarDistance) == 1024u,
           "a comparator transition abandons the frame and gives the game's values back");
    const std::size_t before = g_writes.size();
    enter(&cpu);
    expect(g_writes.size() == before, "the lockstep recorder writes nothing");
    g_ls_mode = 0;
    g_ls_replay_active = 1;
    enter(&cpu);
    expect(g_writes.size() == before, "the lockstep replay writes nothing");
    g_ls_replay_active = 0;
    expect(only_distance_writes(), "netplay and lockstep paths touch no other guest RAM");
}

void test_savestates_keep_the_game_values() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    expect(diagnostics().active == 1 && load_word(kFarDistance) == 1536u, "metrics window and writes start at entry");
    disruptor_far_rendering_state_saving();
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "a savestate taken inside the renderer captures the game's own values");
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u && diagnostics().substituted_loads == 0u,
           "the audited loads after the save read the game's distance (registers already loaded keep theirs)");
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 1024u, "the epilogue after a save writes nothing back");

    enter(&cpu);
    expect(load_word(kFarDistance) == 1536u, "the next frame writes again");
    const std::size_t before = g_writes.size();
    seed_word(kFarDistance, 1536u);
    seed_word(kFadeStart, 768u);
    disruptor_far_rendering_abandon_metrics();
    expect(diagnostics().active == 0, "savestate load abandons the stale wall-span window");
    exit_renderer(&cpu);
    expect(g_writes.size() == before && load_word(kFarDistance) == 1536u,
           "after a savestate load the restored RAM is left as the state holds it");
    seed_word(kFarDistance, 1024u);
    seed_word(kFadeStart, 256u);
    enter(&cpu);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1536u, "the frame after a load writes from the restored globals");
    exit_renderer(&cpu);
}

void test_screen_effects_see_the_game_values() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1536u, "the traversal reads the written far plane");
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "the screen effects after the traversal see the game's own values");
    seed_word(kFadeStart, load_word(kFadeStart) - 4u);
    cpu.gpr[2] = kReviewedVisibleSpanBase;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    expect(load_word(kFarDistance) == 1536u && load_word(kFadeStart) == 764u,
           "the visible loop gets the preset's values again, from the fade the effects left");
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 252u,
           "after the frame the game keeps its own fade, moved only by the effect's own step");

    seed_word(kFarDistance, 4000u);
    seed_word(kFadeStart, 3232u);
    enter(&cpu);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    seed_word(kFarDistance, 1024u);
    seed_word(kFadeStart, 256u);
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    expect(load_word(kFarDistance) == 1536u && load_word(kFadeStart) == 768u,
           "an effect that brings an unextendable far plane down gets it extended for the visible loop");
    exit_renderer(&cpu);

    enter(&cpu);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    g_netplay = true;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    expect(!g_frame.active && g_preset == DISRUPTOR_FAR_RENDERING_RETAIL && load_word(kFarDistance) == 1024u,
           "netplay beginning while the effects run retires the frame and writes nothing again");
    g_netplay = false;
    exit_renderer(&cpu);
}

void test_fades_follow_the_effects() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    enter(&cpu);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    seed_word(kFadeStart, 252u);
    g_published = 0;
    cpu.gpr[2] = kReviewedVisibleSpanBase;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    expect(g_published == 1 && g_published_far == 1024u && g_published_fade == 252,
           "at the original distance the fades take the start the effects left in the same frame");
    exit_renderer(&cpu);

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    seed_word(kFadeStart, 248u);
    g_published = 0;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    expect(g_published == 1 && g_published_far == 1536u && g_published_fade == 760,
           "at 1.5x the fades take the distances written again after the effects");
    g_ls_mode = 1;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    g_ls_mode = 0;
    expect(g_published == 1, "the comparator publishes nothing to the renderer");
    exit_renderer(&cpu);
}

void test_saves_wait_for_the_epilogue() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    enter(&cpu);
    expect(disruptor_far_rendering_save_deferred() == 0, "at the original distance a save is never held back");
    exit_renderer(&cpu);

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    expect(disruptor_far_rendering_save_deferred() == 0, "outside the renderer a save is taken at once");
    enter(&cpu);
    expect(disruptor_far_rendering_save_deferred() == 1, "inside a written frame a save waits");
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    expect(disruptor_far_rendering_save_deferred() == 1, "while the effects run on the game's values a save still waits");
    cpu.gpr[2] = kReviewedVisibleSpanBase;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    exit_renderer(&cpu);
    expect(disruptor_far_rendering_save_deferred() == 0, "after the epilogue the save is taken");
    enter(&cpu);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    seed_word(kFadeStart, 4000u);
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    expect(load_word(kFarDistance) == 1024u && disruptor_far_rendering_save_deferred() == 1,
           "a frame written at its entry holds a save back to its end even when the rewrite after the effects fails");
    exit_renderer(&cpu);
    seed_word(kFadeStart, 256u);
    enter(&cpu);
    disruptor_far_rendering_abandon_metrics();
    expect(disruptor_far_rendering_save_deferred() == 0, "a loaded state holds nothing back");
    exit_renderer(&cpu);
}

void test_fog_for_sprites() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    g_smooth_fade = 1;
    enter(&cpu);
    std::int32_t start = 0, length = 0;
    int power = 0;
    disruptor_far_rendering_fog_ramp(&start, &length, &power);
    expect(start == 256 && length == 1280 && power == 1,
           "the smooth fade's fog spans from the game's own start to the 1.5x far plane, evenly");
    const DistanceLoadSite& sprites = kDistanceLoadSites[6];
    const DistanceLoadSite& enemies = kDistanceLoadSites[12];
    expect(sprites.pc == 0x8003BA30u && enemies.pc == 0x80043184u, "the sprite and enemy fade loads are where they are taken to be");
    const auto pressed = [&](const DistanceLoadSite& site, std::uint32_t range_register, std::int32_t range,
                             std::uint32_t saved_ra) {
        cpu.gpr[range_register] = static_cast<std::uint32_t>(range);
        invoke_load(&cpu, site, saved_ra);
        return range - static_cast<std::int32_t>(cpu.gpr[(site.opcode >> 16u) & 31u]);
    };
    expect(pressed(sprites, 18u, 900, 0x800411ACu) == 386 && disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == 8,
           "a sprite gets row 6 of the stretched fog and a share of 2/64 of row 7, by its packet");
    expect(pressed(sprites, 18u, 200, 0x800411ACu) == -56, "short of the fog's start a sprite gets the nearest row");
    expect(pressed(sprites, 18u, 1535, 0x800411ACu) == 767 && disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == 0,
           "the last row blends with nothing past it");
    expect(pressed(sprites, 18u, 5000, 0x800411ACu) == 768, "past the far plane the game clamps to its last row itself");
    cpu.gpr[kListPacketRegister] = 0x80050000u;
    expect(pressed(enemies, 20u, 1000, 0x80043FE0u) == 446 && disruptor_far_rendering_packet_fraction(0x80050000u, kSprite) == 248,
           "an enemy from the list gets the same fog, its share by the packet in s5");
    exit_renderer(&cpu);
    cpu.gpr[kListPacketRegister] = 0x80050100u;
    expect(pressed(enemies, 20u, 1000, 0x80043FE0u) == 446 && disruptor_far_rendering_packet_fraction(0x80050100u, kSprite) == 248,
           "the list drawn after the renderer's epilogue gets the frame's fog from the game's own fade start");
    disruptor_far_rendering_fog_ramp(&start, &length, &power);
    expect(start == 256 && length == 1280, "until the next entry the renderer keeps the frame's fog");

    disruptor_far_rendering_set_fog_exponent(2);
    enter(&cpu);
    expect(disruptor_far_rendering_packet_fraction(0x80050100u, kSprite) == 248, "the last frame's shares hold through the next frame");
    expect(pressed(sprites, 18u, 900, 0x800411ACu) == 194, "a squared fog stays thinner near the viewer");
    disruptor_far_rendering_fog_ramp(&start, &length, &power);
    expect(power == 2, "the renderer gets the power too");
    exit_renderer(&cpu);
    enter(&cpu);
    expect(disruptor_far_rendering_packet_fraction(0x80050100u, kSprite) == -1, "shares older than the frame before are gone");
    exit_renderer(&cpu);

    const DistanceLoadSite& backdrop = kDistanceLoadSites[5];
    const DistanceLoadSite& shadow = kDistanceLoadSites[10];
    const DistanceLoadSite& weapon = kDistanceLoadSites[11];
    disruptor_far_rendering_set_fog_exponent(1);
    enter(&cpu);
    expect(backdrop.pc == 0x8003B2CCu && shadow.pc == 0x8003D28Cu && weapon.pc == 0x8003FB0Cu, "the other fade loads are where they are taken to be");
    expect(pressed(backdrop, 3u, 1800, 0x8004106Cu) - 900 == 386, "the backdrop fades by half the sum it holds");
    expect(pressed(shadow, 23u, 900, 0x800411C4u) == 386, "a shadow fades out over the stretched fog too");
    cpu.gpr[2] = 10u;
    invoke_load(&cpu, weapon, 0x8004122Cu);
    expect(728 - static_cast<std::int32_t>(cpu.gpr[4]) == 283, "the weapon's fade takes the depth it works out from its byte");
    cpu.gpr[4] = kRendererContext;
    exit_renderer(&cpu);

    g_smooth_fade = 0;
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    expect(pressed(sprites, 18u, 900, 0x800411ACu) == 132 && disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == -1,
           "without the smooth fade a sprite keeps the fog moved out with the far plane, and no share");
    disruptor_far_rendering_fog_ramp(&start, &length, &power);
    expect(start == 768 && length == 768 && power == 1, "and so does the renderer's fade");
    exit_renderer(&cpu);
    expect(pressed(enemies, 20u, 1000, 0x80043FE0u) == 232, "the list after the epilogue gets the moved fog, not the game's word");

    g_smooth_fade = 1;
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_RETAIL);
    enter(&cpu);
    expect(pressed(sprites, 18u, 900, 0x800411ACu) == 644 && disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == 16,
           "at the original distance a sprite blends rows under the smooth fade too");
    exit_renderer(&cpu);
}

void test_fog_shares_belong_to_their_packet() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    g_smooth_fade = 1;
    const DistanceLoadSite& sprites = kDistanceLoadSites[6];
    const DistanceLoadSite& enemies = kDistanceLoadSites[12];
    const auto fade = [&](const DistanceLoadSite& site, std::uint32_t range_register, std::int32_t range, std::uint32_t saved_ra) {
        cpu.gpr[range_register] = static_cast<std::uint32_t>(range);
        invoke_load(&cpu, site, saved_ra);
    };
    enter(&cpu);
    fade(sprites, 18u, 900, 0x800411ACu);
    expect(disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == 8 &&
               disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == 8,
           "the sprite's packet keeps its share for every draw of the same words");
    expect(disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite + 1u) == -1 &&
               disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == -1,
           "other words at the address are another primitive, and the share is gone");

    cpu.gpr[kListPacketRegister] = 0x80050000u;
    fade(enemies, 20u, 1000, 0x80043FE0u);
    std::int32_t rows = 0;
    expect(disruptor_far_rendering_list_fade_rows(0x80050000u, 1000, 1300, &rows) == 1 && rows == 3,
           "a body faded by its led key moves by the stretched fog's rows to its own key");
    expect(disruptor_far_rendering_packet_fraction(0x80050000u, kSprite) == 200, "and its share is its own key's");
    expect(disruptor_far_rendering_list_fade_rows(0x80050100u, 1000, 1300, &rows) == 0,
           "another body's packet is not the one the fog shaped");
    invoke_load(&cpu, enemies, 0x80041000u);
    expect(disruptor_far_rendering_list_fade_rows(0x80050000u, 1000, 1300, &rows) == 0,
           "a list load from another caller, left to the game, leaves no body shaped");
    exit_renderer(&cpu);

    g_smooth_fade = 0;
    enter(&cpu);
    cpu.gpr[kListPacketRegister] = 0x80050000u;
    fade(enemies, 20u, 1000, 0x80043FE0u);
    expect(disruptor_far_rendering_list_fade_rows(0x80050000u, 1000, 1100, &rows) == 1 && rows == 2,
           "without the smooth fade the rows are those of the fog moved out with the far plane");
    exit_renderer(&cpu);

    g_smooth_fade = 1;
    enter(&cpu);
    fade(sprites, 18u, 900, 0x800411ACu);
    exit_renderer(&cpu);
    disruptor_far_rendering_abandon_metrics();
    expect(disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == -1, "a loaded state's packets have no shares");
    expect(disruptor_far_rendering_list_fade_rows(0x80050000u, 1000, 1100, &rows) == 0, "nor a shaped body");
    cpu.gpr[kListPacketRegister] = 0x80050000u;
    cpu.gpr[20] = 1000u;
    invoke_load(&cpu, enemies, 0x80043FE0u);
    expect(cpu.gpr[(enemies.opcode >> 16u) & 31u] == 256u, "the list drawn right after a load takes the game's own fade start");

    enter(&cpu);
    fade(sprites, 18u, 900, 0x800411ACu);
    exit_renderer(&cpu);
    g_ls_replay_active = 1;
    enter(&cpu);
    g_ls_replay_active = 0;
    expect(disruptor_far_rendering_packet_fraction(kPrimitiveBuffer, kSprite) == -1, "a comparator frame hands out no shares from before");
}

void test_transitions_inside_a_written_frame() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    g_netplay = true;
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u && load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u,
           "netplay beginning inside a frame gives the load and the globals the game's values at once");
    g_netplay = false;
    exit_renderer(&cpu);

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    g_ls_mode = 1;
    cpu.gpr[31] = 0x800410E0u;
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    expect(load_word(kFarDistance) == 1024u && diagnostics().active == 0,
           "the comparator beginning inside a frame abandons it and gives the globals back");
    g_ls_mode = 0;
    exit_renderer(&cpu);
}

void test_presets_up_to_twice() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    const std::uint32_t expected[] = {1024u, 1280u, 1536u, 2048u};
    for (int preset = DISRUPTOR_FAR_RENDERING_RETAIL; preset < DISRUPTOR_FAR_RENDERING_PRESET_COUNT; ++preset) {
        expect(disruptor_far_rendering_set_preset(preset) == DISRUPTOR_FAR_RENDERING_OK, "every preset is accepted");
        enter(&cpu);
        expect(load_word(kFarDistance) == expected[preset] &&
                   load_word(kFadeStart) == 256u + (expected[preset] - 1024u),
               "each preset writes its multiple of the far plane and moves the fade start by as much");
        exit_renderer(&cpu);
    }
    seed_word(kFarDistance, 2000u);
    seed_word(kFadeStart, 1232u);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_DOUBLE);
    enter(&cpu);
    expect(load_word(kFarDistance) == 3688u, "2x of a long far plane stops at the ordering table's limit");
    exit_renderer(&cpu);
    expect(disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_PRESET_COUNT) == DISRUPTOR_FAR_RENDERING_INVALID_PRESET,
           "no preset past 2x");
}

void test_edge_margin_under_the_smooth_fade() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_RETAIL);
    enter(&cpu);
    expect(g_writes.empty() && disruptor_far_rendering_edge_margin() == 0, "without the smooth fade the original distance writes nothing");
    exit_renderer(&cpu);

    g_smooth_fade = 1;
    expect(disruptor_far_rendering_edge_margin() == 1, "the smooth fade keeps the meshes past the fog's end");
    enter(&cpu);
    expect(load_word(kFarDistance) == 1024u + kEdgeMargin && load_word(kFadeStart) == 256u,
           "the meshes are kept a margin past the far plane, the fade start stays");
    invoke_first_far(&cpu);
    expect(cpu.gpr[12] == 1024u + kEdgeMargin, "rooms are walked to the margin");
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    expect(cpu.gpr[3] == 1024u, "an object is drawn no further than the fog's end");
    std::uint32_t far_distance = 0u;
    std::int32_t fade_start = 0;
    disruptor_far_rendering_drawn_distances(&far_distance, &fade_start);
    expect(far_distance == 1024u && fade_start == 256, "the fades end at the far plane seen, not at the margin");
    std::int32_t start = 0, length = 0;
    int power = 0;
    disruptor_far_rendering_fog_ramp(&start, &length, &power);
    expect(start == 256 && length == 768, "the fog ends at the far plane seen");
    exit_renderer(&cpu);
    expect(load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u, "the epilogue gives the game its own far plane back");

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_DOUBLE);
    seed_word(kFarDistance, 1800u);
    seed_word(kFadeStart, 1032u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 3688u && load_word(kFadeStart) == 2664u,
           "near the ordering table's limit the margin gives way to 256 before the far seen does, the fade start moves with it");
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    expect(cpu.gpr[3] == 3432u, "objects stop where the fog now ends");
    std::int32_t far_start = 0, far_length = 0;
    disruptor_far_rendering_fog_ramp(&far_start, &far_length, &power);
    expect(far_start == 1032 && far_length == 2400, "and so does the fog");
    exit_renderer(&cpu);
    seed_word(kFarDistance, 1650u);
    seed_word(kFadeStart, 882u);
    enter(&cpu);
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    expect(load_word(kFarDistance) == 3688u && load_word(kFadeStart) == 2532u && cpu.gpr[3] == 3300u,
           "short of the least margin's room the far seen stays whole, the margin takes what the limit leaves");
    exit_renderer(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_RETAIL);
    seed_word(kFarDistance, 3600u);
    seed_word(kFadeStart, 2832u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 3688u, "the game's own long far plane keeps what room the limit leaves");
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    expect(cpu.gpr[3] == 3600u, "and is never shortened");
    exit_renderer(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_EXTENDED);
    seed_word(kFarDistance, 3500u);
    seed_word(kFadeStart, 2732u);
    enter(&cpu);
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    expect(load_word(kFarDistance) == 3688u && cpu.gpr[3] == 3500u, "a lengthened far plane gives way no further than the game's own");
    exit_renderer(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_DOUBLE);
    seed_word(kFarDistance, 1800u);
    seed_word(kFadeStart, 1032u);
    enter(&cpu);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    expect(load_word(kFarDistance) == 1800u, "the screen effects see the game's own far plane under the margin too");
    cpu.gpr[2] = kReviewedVisibleSpanBase;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    expect(load_word(kFarDistance) == 3688u && load_word(kFadeStart) == 2664u && cpu.gpr[3] == 3432u,
           "the visible loop gets the margin again, with the room made at the limit");
    exit_renderer(&cpu);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_RETAIL);
    seed_word(kFarDistance, 3800u);
    seed_word(kFadeStart, 3032u);
    const std::size_t written = g_writes.size();
    enter(&cpu);
    expect(g_writes.size() == written && load_word(kFarDistance) == 3800u, "a script's far plane past the limit is neither extended nor cut");
    exit_renderer(&cpu);

    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_RETAIL);
    seed_word(kFarDistance, 1024u);
    seed_word(kFadeStart, 256u);
    disruptor_far_rendering_set_depth_fade_mode(DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED);
    expect(disruptor_far_rendering_edge_margin() == 0, "with the depth fade off nothing dissolves, so no margin");
    disruptor_far_rendering_set_depth_fade_mode(DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL);
    g_netplay = true;
    expect(disruptor_far_rendering_edge_margin() == 0, "a network game never gets the margin");
    g_netplay = false;
    g_ls_mode = 1;
    expect(disruptor_far_rendering_edge_margin() == 0, "neither does the comparator");
    g_ls_mode = 0;
}

void test_far_plane_kept_inside_the_ordering_table() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu, 3000u, 2232);
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    enter(&cpu);
    expect(load_word(kFarDistance) == 3688u && load_word(kFadeStart) == 2920u,
           "a far plane is extended no further than the ordering table reaches, the fade by the same amount");
    exit_renderer(&cpu);
    seed_word(kFarDistance, 4000u);
    seed_word(kFadeStart, 3232u);
    enter(&cpu);
    expect(load_word(kFarDistance) == 4000u && load_word(kFadeStart) == 3232u && g_writes.size() == 4u,
           "a far plane already past that limit is not extended");
    exit_renderer(&cpu);
}

void walk_with(CPUState *cpu, std::int32_t left_x, std::int32_t left_z, std::int32_t right_x, std::int32_t right_z) {
    cpu->gpr[31] = 0x800410E0u;
    cpu->gpr[19] = 0u;
    cpu->gpr[4] = static_cast<std::uint32_t>(left_x);
    cpu->gpr[5] = static_cast<std::uint32_t>(left_z);
    cpu->gpr[6] = static_cast<std::uint32_t>(right_x);
    cpu->gpr[7] = static_cast<std::uint32_t>(right_z);
    hook_after(cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
}

void reach_visible_loop(CPUState *cpu, std::uint32_t rooms) {
    cpu->gpr[4] = kRendererContext;
    hook_after(cpu, kAfterTraversal, kAfterTraversalOpcode);
    cpu->gpr[2] = kReviewedVisibleSpanBase + rooms * kVisibleSpanStride;
    hook_after(cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
}

void test_rooms_reach_past_their_portals() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    constexpr std::uint32_t kNarrow = (100u << 16) | 200u, kWhole = 320u, kPast = 0xDEADBEEFu;
    seed_word(kReviewedVisibleSpanBase, kNarrow);
    seed_word(kReviewedVisibleSpanBase + kVisibleSpanStride, kWhole);
    seed_word(kReviewedVisibleSpanBase + 2u * kVisibleSpanStride, kPast);

    enter(&cpu);
    walk_with(&cpu, -1448, 1448, 1448, 1448);
    expect(cpu.gpr[4] == static_cast<std::uint32_t>(-1448), "into the retail arena the walk is the game's");
    reach_visible_loop(&cpu, 2u);
    expect(load_word(kReviewedVisibleSpanBase) == kNarrow, "and so are the spans");
    exit_renderer(&cpu);

    g_arena_relocated = 1;
    enter(&cpu);
    reach_visible_loop(&cpu, 2u);
    expect(load_word(kReviewedVisibleSpanBase) == kWhole && load_word(kReviewedVisibleSpanBase + kVisibleSpanStride) == kWhole,
           "the visible loop draws every listed room over the whole screen");
    expect(load_word(kReviewedVisibleSpanBase + 2u * kVisibleSpanStride) == kPast, "the word past the list is not touched");
    expect(disruptor_far_rendering_save_deferred() == 1, "a save waits while the spans are open");
    exit_renderer(&cpu);
    expect(load_word(kReviewedVisibleSpanBase) == kNarrow && disruptor_far_rendering_save_deferred() == 0,
           "the epilogue puts the spans back and lets a save through");

    enter(&cpu);
    walk_with(&cpu, -1448, 1448, 1448, 1448);
    const disruptor::portal_reach::Wedge turned{{static_cast<std::int32_t>(cpu.gpr[4]), static_cast<std::int32_t>(cpu.gpr[5])},
                                                {static_cast<std::int32_t>(cpu.gpr[6]), static_cast<std::int32_t>(cpu.gpr[7])}};
    expect(turned.left.x < -1448 && turned.right.x > 1448 && turned.left.z < 1448 && turned.right.z < 1448,
           "both of the walk's rays turn outward");
    expect(disruptor_far_rendering_save_deferred() == 1, "from the first turned ray a save waits for the frame's end");
    reach_visible_loop(&cpu, 1u);
    disruptor_far_rendering_state_saving();
    expect(load_word(kReviewedVisibleSpanBase) == kNarrow, "a save taken inside the frame anyway finds the game's spans");
    exit_renderer(&cpu);
    expect(load_word(kReviewedVisibleSpanBase) == kNarrow, "and the epilogue has nothing left to put back");

    enter(&cpu);
    reach_visible_loop(&cpu, 1u);
    seed_word(kReviewedVisibleSpanBase, kWhole);
    disruptor_far_rendering_abandon_metrics();
    exit_renderer(&cpu);
    expect(load_word(kReviewedVisibleSpanBase) == kWhole, "after a state load the loaded list is not overwritten");

    seed_word(kReviewedVisibleSpanBase, kNarrow);
    enter(&cpu);
    reach_visible_loop(&cpu, 1u);
    enter(&cpu);
    expect(load_word(kReviewedVisibleSpanBase) == kNarrow, "a frame abandoned by a nested entry puts the spans back");
    exit_renderer(&cpu);

    enter(&cpu);
    cpu.gpr[2] = kReviewedVisibleSpanBase + kVisibleSpanStride;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    expect(load_word(kReviewedVisibleSpanBase) == kNarrow, "a visible loop the traversal's end did not precede opens nothing");
    exit_renderer(&cpu);

    g_netplay = true;
    enter(&cpu);
    walk_with(&cpu, -1448, 1448, 1448, 1448);
    expect(cpu.gpr[4] == static_cast<std::uint32_t>(-1448), "netplay walks the game's way");
    reach_visible_loop(&cpu, 1u);
    expect(load_word(kReviewedVisibleSpanBase) == kNarrow, "and draws the game's way");
    exit_renderer(&cpu);
    g_netplay = false;

    for (const auto& [address, value] : g_writes)
        expect(address == kFarDistance || address == kFadeStart ||
                   (address >= kReviewedVisibleSpanBase && address < kReviewedVisibleSpanBase + 2u * kVisibleSpanStride),
               "the reach writes nothing but the listed spans");
}

using disruptor::portal_reach::Wedge;
constexpr Wedge kScreenWedge{{-1448, 1448}, {1448, 1448}};
constexpr Wedge kInnerWedge{{-500, 1448}, {500, 1448}};
constexpr Wedge kMarginWedge{{1500, 1448}, {1726, 1448}};
constexpr std::uint32_t kPortals = 0x80050000u, kSectorData = 0x80060000u;

void seed_room(std::uint32_t room, bool loaded) {
    const std::uint32_t sector = kSectorData + 0x100u * room;
    seed_word(disruptor::sector_reach::kSectors + 4u * room, loaded ? sector : 0u);
    seed_byte(sector + disruptor::portal_reach::kSectorDoor, 0xFFu);
    seed_byte(sector + disruptor::portal_reach::kSectorCompanions, 0xFFu);
    seed_byte(kPortals + 16u * room + disruptor::sector_reach::kPortalSector, static_cast<std::uint8_t>(room));
}

std::uint32_t portal_of(std::uint32_t room) { return kPortals + 16u * room; }

void walk_into(CPUState *cpu, std::uint32_t depth, std::uint32_t portal, const Wedge &wedge) {
    seed_word(cpu->gpr[29] + disruptor::sector_reach::kPortalOnStack, portal);
    cpu->gpr[19] = depth;
    cpu->gpr[31] = depth == 0u ? 0x800410E0u : 0x8003AE54u;
    cpu->gpr[4] = static_cast<std::uint32_t>(wedge.left.x);
    cpu->gpr[5] = static_cast<std::uint32_t>(wedge.left.z);
    cpu->gpr[6] = static_cast<std::uint32_t>(wedge.right.x);
    cpu->gpr[7] = static_cast<std::uint32_t>(wedge.right.z);
    hook_after(cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
}

void test_rooms_only_the_wider_walk_reaches() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    g_arena_relocated = 1;
    seed_word(disruptor::portal_reach::kCosines, 256u);
    for (std::uint32_t room = 1u; room <= 3u; ++room) seed_room(room, true);
    constexpr std::uint32_t kListed[] = {1u, 3u, 2u};
    for (std::uint32_t index = 0; index < 3u; ++index) {
        seed_word(kReviewedVisibleSpanBase + kVisibleSpanStride * index, (10u * index << 16) | (200u + index));
        seed_word(kReviewedVisibleSpanBase + kVisibleSpanStride * index + 4u, 0x7700u | kListed[index]);
    }
    seed_word(kVisibleEnd, kReviewedVisibleSpanBase + 3u * kVisibleSpanStride);
    const std::uint32_t far_head = disruptor::portal_reach::kObjectTables[2] + 4u * 3u;
    const std::uint32_t sector_head = kSectorData + 0x300u + disruptor::portal_reach::kSectorObjects;
    const std::uint32_t near_head = disruptor::portal_reach::kObjectTables[2] + 4u * 2u;
    seed_word(far_head, 0x80081000u);
    seed_word(sector_head, 0x80082000u);
    seed_word(near_head, 0x80083000u);

    const auto walk = [&cpu] {
        walk_into(&cpu, 0u, portal_of(1u), kScreenWedge);
        walk_into(&cpu, 1u, portal_of(3u), kMarginWedge);
        walk_into(&cpu, 1u, portal_of(2u), kInnerWedge);
    };
    enter(&cpu);
    walk();
    reach_visible_loop(&cpu, 3u);
    expect(load_word(far_head) == 0u && load_word(sector_head) == 0u && load_word(near_head) == 0x80083000u,
           "the room only the wider walk reached draws none of its objects, the others draw theirs");
    hook_after(&cpu, kAfterVisibleLoop, kAfterVisibleLoopOpcode);
    expect(load_word(far_head) == 0x80081000u && load_word(sector_head) == 0x80082000u, "after the visible loop its lists are back");
    expect(load_word(kVisibleEnd) == kReviewedVisibleSpanBase + 2u * kVisibleSpanStride &&
               load_word(kReviewedVisibleSpanBase + 4u) == 0x7701u &&
               load_word(kReviewedVisibleSpanBase + kVisibleSpanStride + 4u) == 0x7702u,
           "and the list the game reads afterwards holds the rooms of its own walk");
    expect(load_word(kReviewedVisibleSpanBase) == 200u && load_word(kReviewedVisibleSpanBase + kVisibleSpanStride) == ((20u << 16) | 202u),
           "each with the span the walk gave it");
    exit_renderer(&cpu);
    expect(load_word(kVisibleEnd) == kReviewedVisibleSpanBase + 2u * kVisibleSpanStride && disruptor_far_rendering_save_deferred() == 0,
           "the epilogue leaves the game's list as it is");

    seed_word(kVisibleEnd, kReviewedVisibleSpanBase + 3u * kVisibleSpanStride);
    for (std::uint32_t index = 0; index < 3u; ++index)
        seed_word(kReviewedVisibleSpanBase + kVisibleSpanStride * index + 4u, 0x7700u | kListed[index]);
    enter(&cpu);
    walk();
    reach_visible_loop(&cpu, 3u);
    disruptor_far_rendering_state_saving();
    expect(load_word(far_head) == 0x80081000u, "a save inside the visible loop finds every list");
    exit_renderer(&cpu);

    enter(&cpu);
    walk();
    reach_visible_loop(&cpu, 3u);
    enter(&cpu);
    expect(load_word(far_head) == 0x80081000u, "a frame abandoned by a nested entry gives the lists back");
    exit_renderer(&cpu);

    enter(&cpu);
    walk();
    reach_visible_loop(&cpu, 3u);
    exit_renderer(&cpu);
    expect(load_word(far_head) == 0x80081000u, "an epilogue without the loop's end gives them back too");

    enter(&cpu);
    reach_visible_loop(&cpu, 3u);
    expect(load_word(far_head) == 0x80081000u, "a frame without a walk of its own hides nothing");
    exit_renderer(&cpu);

    enter(&cpu);
    walk();
    reach_visible_loop(&cpu, 3u);
    disruptor_far_rendering_abandon_metrics();
    exit_renderer(&cpu);
    expect(load_word(far_head) == 0u, "after a state load the loaded lists are not overwritten");
    seed_word(far_head, 0x80081000u);

    enter(&cpu);
    walk_into(&cpu, 0u, portal_of(1u), kScreenWedge);
    walk_into(&cpu, 1u, 0u, kInnerWedge);
    reach_visible_loop(&cpu, 3u);
    expect(load_word(far_head) == 0x80081000u, "a room the walk enters unread hides nothing");
    exit_renderer(&cpu);

    seed_room(4u, false);
    seed_room(5u, false);
    enter(&cpu);
    walk_into(&cpu, 0u, portal_of(1u), kScreenWedge);
    walk_into(&cpu, 1u, portal_of(4u), kMarginWedge);
    expect(cpu.gpr[19] == disruptor::sector_reach::kDepthCap && cpu.gpr[4] == static_cast<std::uint32_t>(kMarginWedge.left.x),
           "the wider walk turns back at once from a room out of memory the game's walk does not enter");
    walk_into(&cpu, 1u, portal_of(5u), kInnerWedge);
    expect(cpu.gpr[19] == 1u, "one the game's walk enters asks for its area as it does");
    exit_renderer(&cpu);
}

void face(CPUState *cpu, std::uint32_t gate) {
    cpu->gpr[18] = gate;
    hook_after(cpu, kPortalFirstCandidate, kPortalFirstCandidateOpcode);
}

void test_portals_seen_edge_on() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    g_arena_relocated = 1;
    seed_word(disruptor::portal_reach::kCosines, 256u);
    for (std::uint32_t room = 1u; room <= 3u; ++room) seed_room(room, true);
    const std::uint32_t edge_on = portal_of(20u), slanted = portal_of(21u), back_home = portal_of(22u);
    const auto seed_portal = [](std::uint32_t gate, std::uint32_t room, std::uint16_t second_z) {
        seed_word(gate, 100u);
        seed_word(gate + 4u, 200u | static_cast<std::uint32_t>(second_z) << 16);
        seed_byte(gate + disruptor::sector_reach::kPortalSector, static_cast<std::uint8_t>(room));
    };
    seed_portal(edge_on, 3u, 0u);
    seed_portal(slanted, 3u, 10u);
    seed_portal(back_home, 1u, 0u);
    const std::uint32_t listed_at = disruptor::portal_reach::kListedAt;
    const std::uint32_t far_head = disruptor::portal_reach::kObjectTables[2] + 4u * 3u;
    constexpr std::uint32_t kListed[] = {1u, 3u, 2u};
    const auto list = [&] {
        for (std::uint32_t index = 0; index < 3u; ++index) {
            seed_word(kReviewedVisibleSpanBase + kVisibleSpanStride * index, 320u);
            seed_word(kReviewedVisibleSpanBase + kVisibleSpanStride * index + 4u, kListed[index]);
            seed_byte(listed_at + kListed[index], static_cast<std::uint8_t>(index));
        }
        seed_word(kVisibleEnd, kReviewedVisibleSpanBase + 3u * kVisibleSpanStride);
        seed_word(far_head, 0x80081000u);
    };

    list();
    enter(&cpu);
    walk_into(&cpu, 0u, portal_of(1u), kScreenWedge);
    walk_into(&cpu, 1u, portal_of(3u), kMarginWedge);
    face(&cpu, slanted);
    expect(load_word(listed_at) == 0x01020000u, "a portal not seen edge-on leaves the listed rooms alone");
    face(&cpu, back_home);
    expect(psx_mod_read_byte(listed_at + 1u) == 0u, "so does one into a room the game's walk listed");
    face(&cpu, edge_on);
    expect(psx_mod_read_byte(listed_at + 3u) == 0xFFu,
           "the walk's test of a portal seen edge-on finds unlisted a room only the wider walk listed");
    walk_into(&cpu, 1u, edge_on, kScreenWedge);
    expect(psx_mod_read_byte(listed_at + 3u) == 1u, "the walk that goes in finds it listed again");
    walk_into(&cpu, 1u, portal_of(2u), kInnerWedge);
    reach_visible_loop(&cpu, 3u);
    expect(load_word(far_head) == 0x80081000u, "the room the game's walk entered edge-on draws its objects");
    hook_after(&cpu, kAfterVisibleLoop, kAfterVisibleLoopOpcode);
    expect(load_word(kVisibleEnd) == kReviewedVisibleSpanBase + 3u * kVisibleSpanStride &&
               load_word(kReviewedVisibleSpanBase + kVisibleSpanStride + 4u) == 3u,
           "and stays in the list the game reads");
    exit_renderer(&cpu);

    for (const auto &[name, close] : {std::pair<const char *, int>{"the next portal", 0}, {"the traversal's end", 1},
                                      {"the epilogue", 2}, {"a save", 3},
                                      {"a nested entry", 4}}) {
        list();
        cpu.gpr[4] = kRendererContext;
        enter(&cpu);
        walk_into(&cpu, 0u, portal_of(1u), kScreenWedge);
        walk_into(&cpu, 1u, portal_of(3u), kMarginWedge);
        face(&cpu, edge_on);
        expect(psx_mod_read_byte(listed_at + 3u) == 0xFFu, "the room is shown unlisted to the edge-on test");
        if (close == 0) face(&cpu, slanted);
        if (close == 1) hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
        if (close == 3) disruptor_far_rendering_state_saving();
        if (close == 4) {
            cpu.gpr[4] = kRendererContext;
            enter(&cpu);
        }
        if (close != 2) expect(psx_mod_read_byte(listed_at + 3u) == 1u, name);
        exit_renderer(&cpu);
        expect(psx_mod_read_byte(listed_at + 3u) == 1u, "a portal the walk then passed by leaves the room listed");
    }

    list();
    cpu.gpr[4] = kRendererContext;
    enter(&cpu);
    walk_into(&cpu, 0u, portal_of(1u), kScreenWedge);
    walk_into(&cpu, 1u, portal_of(3u), kMarginWedge);
    face(&cpu, edge_on);
    disruptor_far_rendering_abandon_metrics();
    exit_renderer(&cpu);
    expect(psx_mod_read_byte(listed_at + 3u) == 0xFFu, "after a state load the loaded table is not overwritten");
}

void test_visibility_recursion_and_packet_stages() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(
        DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);

    cpu.gpr[31] = 0x800410E0u;
    cpu.gpr[19] = 0u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    cpu.gpr[31] = 0x8003AE54u;
    cpu.gpr[19] = 10u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    cpu.gpr[31] = 0x8003AF1Cu;
    cpu.gpr[19] = 11u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    cpu.gpr[31] = 0x80001234u;
    cpu.gpr[19] = 99u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);

    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);

    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x20u);
    hook_after(&cpu, kAfterTraversal, kAfterTraversalOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x40u);
    cpu.gpr[2] = kReviewedVisibleSpanBase + 3u * kVisibleSpanStride;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0xA0u);
    hook_after(&cpu, kAfterVisibleLoop, kAfterVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x120u);
    exit_renderer(&cpu);

    auto d = diagnostics();
    expect(d.traversal_recursion_entries == 3u &&
               d.traversal_cap_hits == 1u &&
               d.traversal_max_depth_last == 11u &&
               d.traversal_max_depth_high_water == 11u,
           "recursion observer records valid ancestry, attempted max and cap");
    expect(d.visibility_samples == 1u &&
               d.traversal_rooms_last == 3u &&
               d.traversal_rooms_high_water == 3u &&
               d.submitted_spans_last == 3u &&
               d.submitted_spans_high_water == 3u,
           "completed visibility sample reports reached rooms and span records");
    expect(d.packet_stage_samples == 1u &&
               d.packet_entry_to_traversal_end_last == 0x20u &&
               d.packet_traversal_to_visible_start_last == 0x20u &&
               d.packet_visible_loop_last == 0x60u &&
               d.packet_post_visible_loop_last == 0x80u,
           "ordered primitive boundaries publish exact stage deltas");
    expect(d.observer_sequence_errors == 0u,
           "valid observer sequence has no errors");

    enter(&cpu);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x10u);
    cpu.gpr[2] = kReviewedVisibleSpanBase + kVisibleSpanStride;
    hook_after(&cpu, kBeforeVisibleLoop, kBeforeVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x30u);
    hook_after(&cpu, kAfterVisibleLoop, kAfterVisibleLoopOpcode);
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x40u);
    exit_renderer(&cpu);
    d = diagnostics();
    expect(d.packet_stage_samples == 1u &&
               d.visibility_samples == 1u &&
               d.traversal_rooms_last == 0u &&
               d.submitted_spans_last == 0u &&
               d.observer_sequence_errors >= 1u,
           "out-of-order boundaries fail closed without publishing stale data");
}

void test_portal_and_object_decision_observers() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    disruptor_far_rendering_set_preset(
        DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    const std::size_t entry_writes = g_writes.size();

    constexpr std::uint32_t kSourceSquared = 1024u * 1024u;
    constexpr std::uint32_t kEffectiveSquared = 1280u * 1280u;
    static_assert(kSourceSquared < 1200000u &&
                      1200000u < kEffectiveSquared,
                  "portal flip candidate must lie between both far planes");

    invoke_first_far(&cpu);
    cpu.gpr[11] = kEffectiveSquared;
    cpu.gpr[2] = 1200000u;
    hook_after(&cpu, kPortalFirstCandidate,
               kPortalFirstCandidateOpcode);

    invoke_first_far(&cpu);
    cpu.gpr[11] = kEffectiveSquared;
    cpu.gpr[2] = 1700000u;
    hook_after(&cpu, kPortalFirstCandidate,
               kPortalFirstCandidateOpcode);
    cpu.gpr[2] = 1700000u;
    hook_after(&cpu, kPortalSecondCandidate,
               kPortalSecondCandidateOpcode);
    cpu.gpr[2] = 1200000u;
    hook_after(&cpu, kPortalFinalCandidate,
               kPortalFinalCandidateOpcode);

    invoke_first_far(&cpu);
    cpu.gpr[11] = kEffectiveSquared;
    cpu.gpr[2] = 1700000u;
    hook_after(&cpu, kPortalFirstCandidate,
               kPortalFirstCandidateOpcode);
    hook_after(&cpu, kPortalSecondCandidate,
               kPortalSecondCandidateOpcode);
    hook_after(&cpu, kPortalFinalCandidate,
               kPortalFinalCandidateOpcode);

    for (std::size_t i = 0u; i < kObjectDecisionSites.size(); ++i) {
        const ObjectDecisionSite& object = kObjectDecisionSites[i];
        const DistanceLoadSite& load = kDistanceLoadSites[i + 1u];
        invoke_load(&cpu, load, load.allowed_return_addresses[0]);
        cpu.gpr[object.threshold_register] = 1100u;
        cpu.gpr[object.result_register] = 0u;
        hook_after(&cpu, object.decision_pc, object.decision_opcode);
    }

    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    cpu.gpr[18] = 1400u;
    cpu.gpr[3] = 1u;
    hook_after(&cpu, 0x8003B8D8u, 0x0072182Au);

    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    cpu.gpr[18] = 1100u;
    cpu.gpr[3] = 1u; /* impossible result: token must fail closed */
    hook_after(&cpu, 0x8003B8D8u, 0x0072182Au);

    expect(g_writes.size() == entry_writes && load_word(kFarDistance) == 1280u,
           "visibility decision observers never write guest state");
    seed_word(kPrimitivePointer, kPrimitiveBuffer + 0x80u);
    exit_renderer(&cpu);
    const auto d = diagnostics();
    expect(d.portal_far_tests == 7u &&
               d.portal_shortcut_relaxations == 1u &&
               d.portal_final_decisions == 2u &&
               d.portal_final_effective_rejections == 1u &&
               d.portal_final_decision_flips == 1u,
           "portal observer separates shortcut relaxations from final flips");
    expect(d.object_far_decisions == 5u &&
               d.object_effective_rejections == 1u &&
               d.object_decision_flips == 4u,
           "all object observers report direct retail-reject/effective-accept flips");
    expect(d.observer_sequence_errors == 1u,
           "impossible post-SLT result invalidates exactly one pending token");
}

void test_observer_safety_guards_and_abandon() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    cpu.gpr[31] = 0x800410E0u;
    cpu.gpr[19] = 11u;
    hook_after(&cpu, kTraversalDepthLoad, kTraversalDepthLoadOpcode);
    expect(diagnostics().traversal_recursion_entries == 0u,
           "observer is inert outside an active renderer frame");

    disruptor_far_rendering_set_preset(
        DISRUPTOR_FAR_RENDERING_EXTENDED);
    enter(&cpu);
    invoke_load(&cpu, kDistanceLoadSites[1], 0x800411ACu);
    disruptor_far_rendering_abandon_metrics();
    cpu.gpr[18] = 1100u;
    cpu.gpr[3] = 0u;
    hook_after(&cpu, 0x8003B8D8u, 0x0072182Au);
    auto d = diagnostics();
    expect(d.object_far_decisions == 0u &&
               d.observer_sequence_errors == 0u,
           "savestate abandon clears pending decisions without false errors");

    enter(&cpu);
    g_ls_mode = 1;
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    g_ls_mode = 0;
    g_netplay = true;
    hook_after(&cpu, kTraversalRoomMark, kTraversalRoomMarkOpcode);
    g_netplay = false;
    exit_renderer(&cpu);
    d = diagnostics();
    expect(d.visibility_samples == 0u &&
               d.traversal_rooms_high_water == 0u,
           "comparator and netplay transitions suppress passive observers");
}

void test_reset_and_versioned_snapshot() {
    reset_state();
    CPUState cpu{};
    seed_live_game(&cpu);
    expect(disruptor_far_rendering_set_preset(99) ==
               DISRUPTOR_FAR_RENDERING_INVALID_PRESET,
           "invalid preset rejected");
    expect(disruptor_far_rendering_set_depth_fade_mode(99) ==
               DISRUPTOR_FAR_RENDERING_INVALID_PRESET,
           "invalid depth-fade mode rejected");
    disruptor_far_rendering_set_preset(DISRUPTOR_FAR_RENDERING_FAR);
    disruptor_far_rendering_set_depth_fade_mode(
        DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED);
    enter(&cpu);
    disruptor_far_rendering_reset_session();
    const auto d = diagnostics();
    expect(disruptor_far_rendering_get_preset() ==
               DISRUPTOR_FAR_RENDERING_RETAIL &&
               disruptor_far_rendering_get_depth_fade_mode() ==
                   DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL &&
               d.renderer_entries == 0u && load_word(kFarDistance) == 1024u && load_word(kFadeStart) == 256u &&
               disruptor_far_rendering_save_deferred() == 0,
           "session reset clears both controls and gives the game's values back before a save is let in");
    expect(d.abi_version ==
               DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               d.struct_size == sizeof(d),
           "diagnostics report versioned ABI size");

    struct SmallSnapshot {
        std::uint32_t abi_version;
        std::uint32_t struct_size;
        std::uint32_t canary;
    } small{0u, 0u, 0xDEADBEEFu};
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(&small),
               8u) == 1 &&
               small.abi_version ==
                   DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               small.struct_size == sizeof(DisruptorFarRenderingDiagnostics) &&
               small.canary == 0xDEADBEEFu,
           "versioned diagnostics honor caller capacity");
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(&small),
               7u) == 0 &&
               small.canary == 0xDEADBEEFu,
           "diagnostics reject buffers too small for ABI version and size");

    struct V1SnapshotWithCanary {
        DiagnosticsV1Prefix snapshot;
        std::uint32_t canary;
    } v1{{}, 0xA5A55A5Au};
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(
                   &v1.snapshot),
               static_cast<std::uint32_t>(sizeof(v1.snapshot))) == 1 &&
               v1.snapshot.abi_version ==
                   DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               v1.snapshot.struct_size ==
                   sizeof(DisruptorFarRenderingDiagnostics) &&
               v1.canary == 0xA5A55A5Au,
           "ABI v3 preserves and honors a complete v1 caller capacity");

    struct V2SnapshotWithCanary {
        DiagnosticsV2Prefix snapshot;
        std::uint32_t canary;
    } v2{{}, 0x5AA5A55Au};
    expect(disruptor_far_rendering_get_diagnostics(
               reinterpret_cast<DisruptorFarRenderingDiagnostics *>(
                   &v2.snapshot),
               static_cast<std::uint32_t>(sizeof(v2.snapshot))) == 1 &&
               v2.snapshot.v1.abi_version ==
                   DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION &&
               v2.snapshot.v1.struct_size ==
                   sizeof(DisruptorFarRenderingDiagnostics) &&
               v2.canary == 0x5AA5A55Au,
           "ABI v3 appends fields and honors a complete v2 caller capacity");
}

}  // namespace

/* A sector span's end at the frame's edge reaches past it by the turn guard, an end inside the frame stays. */
void test_a_sector_span_reaches_past_the_frame_edge() {
    constexpr std::uint32_t kSpan = 0x801FFD20u;
    expect(g_sector_callback && g_sector_address == 0x80046134u, "the mesh renderer's entry is hooked");
    CPUState cpu{};
    cpu.read_word = psx_mod_read_word;
    cpu.write_word = psx_mod_write_word;
    cpu.gpr[5] = kSpan;
    seed_word(kSpan + 12u, 0u);
    seed_word(kSpan + 16u, 320u);
    g_sector_callback(&cpu, 0x80046134u);
    expect(load_word(kSpan + 12u) == static_cast<std::uint32_t>(-8) && load_word(kSpan + 16u) == 328u,
           "a span over the whole frame reaches eight pixels past both edges");
    seed_word(kSpan + 12u, 105u);
    seed_word(kSpan + 16u, 210u);
    g_sector_callback(&cpu, 0x80046134u);
    expect(load_word(kSpan + 12u) == 105u && load_word(kSpan + 16u) == 210u, "a doorway's span inside the frame stays");
    seed_word(kSpan + 12u, 0u);
    g_sector_callback(&cpu, 0x80040E68u);
    expect(load_word(kSpan + 12u) == 0u, "another entry leaves the span alone");
    CPUState blind{};
    blind.gpr[5] = kSpan;
    g_sector_callback(&blind, 0x80046134u);
    expect(load_word(kSpan + 12u) == 0u, "without memory access nothing is written");
    g_geometry_correction = 0;
    g_sector_callback(&cpu, 0x80046134u);
    expect(load_word(kSpan + 12u) == 0u, "without geometry correction no turn is shown and the game keeps its span");
    g_geometry_correction = 1;
}

void test_the_split_middle_moves_to_the_diagonal() {
    constexpr std::uint32_t kMiddle = 0x1F8000C0u;
    CPUState cpu{};
    cpu.read_word = psx_mod_read_word;
    cpu.write_word = psx_mod_write_word;
    cpu.gpr[5] = kMiddle;
    cpu.gpr[9] = 10u << 20 | 20u << 10 | 30u;
    cpu.gpr[11] = 30u << 20 | 40u << 10 | 50u;
    cpu.gpr[1] = 100u;
    cpu.gpr[2] = 1000u;
    cpu.gpr[3] = 2000u;
    seed_word(kMiddle, 0x11111111u);
    seed_word(kMiddle + 4u, 0x22222222u);
    disruptor_far_rendering_instruction_hook(&cpu, 0x8004791Cu, 0xACAD0000u, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(load_word(kMiddle) == 0x11111111u, "another instruction at that address leaves the point");
    disruptor_far_rendering_instruction_hook(&cpu, 0x8004791Cu, 0xACAD0004u, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(load_word(kMiddle) == 220u && load_word(kMiddle + 4u) == (520u | 1360u << 16),
           "the split's middle point is the midpoint of corners 1 and 3");
}

void test_a_face_with_a_marked_corner_is_tested_on_its_real_sides() {
    constexpr std::uint32_t kRecords = 0x8005BACCu;
    CPUState cpu{};
    cpu.read_word = psx_mod_read_word;
    const std::uint32_t words[4] = {0x80000000u | 1u << 16 | 63u, 56u << 16 | 32u, 56u << 16 | 32u,
                                    0x80000000u | 16u << 16 | 63u};
    const std::uint32_t screen[4] = {695u << 16 | 1023u, 244u << 16 | 260u, 244u << 16 | 260u, 81u << 16 | 552u};
    for (std::uint32_t corner = 0; corner < 4u; corner++) {
        cpu.gpr[8 + corner] = kRecords + 12u * corner;
        cpu.gpr[12 + corner] = words[corner];
        seed_word(kRecords + 12u * corner, words[corner]);
        seed_word(kRecords + 12u * corner + 4u, screen[corner]);
    }
    seed_word(0x1F80004Cu, static_cast<std::uint32_t>(-8));
    seed_word(0x1F800050u, 328u);
    const auto drop = [&cpu](std::uint32_t pc, std::uint32_t instruction) {
        cpu.gpr[1] = 32u;
        disruptor_far_rendering_instruction_hook(&cpu, pc, instruction, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
        return cpu.gpr[1];
    };
    expect(drop(0x80046C18u, 0x3021003Fu) == 0u, "the floor under the wall button is kept");
    expect(drop(0x80046C18u, 0x30210000u) == 32u, "another instruction at the address changes nothing");
    seed_word(0x1F800050u, 200u);
    cpu.gpr[15] = 0x80000000u | 100u << 16 | 63u;
    expect(drop(0x80046C18u, 0x3021003Fu) == 0u, "the room's span is read from the scratch pad");
    cpu.gpr[12] = 0x80000000u | 100u << 16 | 63u;
    expect(drop(0x80046C18u, 0x3021003Fu) == 0u, "both marked corners right of a narrower span, the others below");
    cpu.gpr[13] = cpu.gpr[14] = 56u << 16 | 8u;
    expect(drop(0x80046C18u, 0x3021003Fu) == 8u, "four corners right of the span: the face is dropped on that side");
    const auto place = [&](std::uint32_t corner, std::uint32_t word, std::uint32_t xy) {
        cpu.gpr[12 + corner] = word;
        seed_word(kRecords + 12u * corner, word);
        seed_word(kRecords + 12u * corner + 4u, xy);
    };
    place(0u, 0x80000000u | 100u << 16 | 63u, 100u << 16 | 0xFED4u);
    place(3u, 0x80000000u | 100u << 16 | 63u, 81u << 16 | 552u);
    seed_word(0x1F800050u, 328u);
    expect(drop(0x80046C18u, 0x3021003Fu) == 0u, "a marked corner left of the span is read from its record's place");
    place(0u, 0x80000000u | 100u << 16 | 63u, 695u << 16 | 1023u);
    cpu.gpr[13] = cpu.gpr[14] = 56u << 16 | 40u;
    seed_word(0x1F800050u, 600u);
    expect(drop(0x80046C18u, 0x3021003Fu) == 0u, "a corner inside a span reaching 600 is on no side");
    cpu.gpr[12] = cpu.gpr[15] = 56u << 16 | 8u;
    cpu.gpr[1] = 32u;
    disruptor_far_rendering_instruction_hook(&cpu, 0x80046C18u, 0x3021003Fu, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[1] == 32u, "a face with no marked corner keeps the game's answer");
    cpu.gpr[12] = 0x80000000u | 1u << 16 | 63u;
    cpu.gpr[1] = 0u;
    disruptor_far_rendering_instruction_hook(&cpu, 0x80046C18u, 0x3021003Fu, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
    expect(cpu.gpr[1] == 0u, "a face the game keeps stays kept");
}

void test_the_split_quarters_meet_at_texel_16() {
    constexpr std::uint32_t kTable = 0x80100000u;
    constexpr std::uint32_t kPacket = 0x80120000u;
    CPUState cpu{};
    cpu.read_word = psx_mod_read_word;
    cpu.write_word = psx_mod_write_word;
    cpu.gpr[6] = 7u << 22 | 9u << 12 | 4u;
    cpu.gpr[24] = kTable;
    cpu.gpr[25] = kPacket;
    seed_word(kTable + 8u, 0x39142040u);
    seed_word(kTable + 12u, 0x008E205Fu);
    const auto quarter = [&cpu](std::uint32_t pc, std::uint32_t instruction, std::array<std::uint32_t, 4> words) {
        for (std::uint32_t slot = 0; slot < 4u; slot++) seed_word(kPacket + 12u + 12u * slot, words[slot]);
        disruptor_far_rendering_instruction_hook(&cpu, pc, instruction, DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION);
        std::array<std::uint32_t, 4> out{};
        for (std::uint32_t slot = 0; slot < 4u; slot++) out[slot] = load_word(kPacket + 12u + 12u * slot);
        return out;
    };
    const std::array<std::uint32_t, 4> first = {0x39142040u, 0x008E204Fu, 0x00002F40u, 0x00002F4Fu};
    expect(quarter(0x80047AD0u, 0xAF290030u, first) ==
               std::array<std::uint32_t, 4>{0x39142040u, 0x008E2050u, 0x00003040u, 0x00003050u},
           "the corner-0 quarter");
    expect(quarter(0x80047B90u, 0xAF290030u, {0x39142050u, 0x008E205Fu, 0x00002F50u, 0x00002F5Fu}) ==
               std::array<std::uint32_t, 4>{0x39142050u, 0x008E205Fu, 0x00003050u, 0x0000305Fu},
           "the corner-1 quarter");
    expect(quarter(0x80047D18u, 0xAF290030u, {0x39143040u, 0x008E304Fu, 0x00003F40u, 0x00003F4Fu}) ==
               std::array<std::uint32_t, 4>{0x39143040u, 0x008E3050u, 0x00003F40u, 0x00003F50u},
           "the corner-3 quarter");
    expect(quarter(0x80047AD0u, 0xAF290000u, first) == first, "another instruction at the address changes nothing");
    expect(quarter(0x80047C54u, 0xAF290030u, first) == first, "nor does the corner-2 quarter's, which meets already");
    seed_word(kTable + 12u, 0x008E204Fu);
    expect(quarter(0x80047AD0u, 0xAF290030u, first) == first, "a tile that is not 32 by 32 keeps the game's quarters");
    seed_word(kTable + 12u, 0x008E205Fu);
}

int main() {
    test_a_sector_span_reaches_past_the_frame_edge();
    test_the_split_middle_moves_to_the_diagonal();
    test_a_face_with_a_marked_corner_is_tested_on_its_real_sides();
    test_the_split_quarters_meet_at_texel_16();
    test_registration_and_written_distances();
    test_independent_depth_fade_modes();
    test_drawn_distances_are_the_globals();
    test_live_billboard_fade_site();
    test_all_sites_and_ancestry_contracts();
    test_retail_baseline_and_metric_reset();
    test_current_globals_and_signed_fade();
    test_guards_and_exact_dispatch();
    test_weapon_load_keeps_distance();
    test_nested_toggle_netplay_and_lockstep();
    test_savestates_keep_the_game_values();
    test_screen_effects_see_the_game_values();
    test_fades_follow_the_effects();
    test_saves_wait_for_the_epilogue();
    test_fog_for_sprites();
    test_fog_shares_belong_to_their_packet();
    test_transitions_inside_a_written_frame();
    test_presets_up_to_twice();
    test_edge_margin_under_the_smooth_fade();
    test_far_plane_kept_inside_the_ordering_table();
    test_rooms_reach_past_their_portals();
    test_rooms_only_the_wider_walk_reaches();
    test_portals_seen_edge_on();
    test_visibility_recursion_and_packet_stages();
    test_portal_and_object_decision_observers();
    test_observer_safety_guards_and_abandon();
    test_reset_and_versioned_snapshot();

    expect(only_distance_writes(), "far rendering writes no guest RAM but the two distance globals");
    if (g_failures != 0) {
        std::cerr << g_failures << " far-rendering test(s) failed\n";
        return 1;
    }
    std::cout << "Disruptor far-rendering contracts: PASS\n";
    return 0;
}
