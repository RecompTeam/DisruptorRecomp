#include "game/settings/options.h"

#include "game/frame/present_rate.h"
#include "game/settings/resolution.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <type_traits>

namespace disruptor::settings {
namespace {
using Saved = bool (*)(const Settings&);
using Get = int (*)(const Settings&);
using Set = void (*)(Settings&, int);

template <auto Has> bool saved(const Settings& settings) { return settings.*Has; }
template <auto Value> int read(const Settings& settings) { return static_cast<int>(settings.*Value); }
template <auto Has, auto Value> void write(Settings& settings, int value) {
    settings.*Has = true;
    settings.*Value = static_cast<std::remove_reference_t<decltype(settings.*Value)>>(value);
}

constexpr int aspect_widths[] = {4, 16, 21, 32};
constexpr int aspect_heights[] = {3, 9, 9, 9};
constexpr int widest_aspect = 3, window_aspect = 4;

bool aspect_saved(const Settings& settings) { return settings.has_aspect_ratio || settings.has_adaptive_view; }

int aspect(const Settings& settings) {
    if (settings.has_adaptive_view && settings.adaptive_view) return window_aspect;
    for (int index = 0; index <= widest_aspect; ++index)
        if (settings.aspect_num * aspect_heights[index] == settings.aspect_den * aspect_widths[index]) return index;
    return -1;
}

void set_aspect(Settings& settings, int index) {
    const int fixed = index == window_aspect ? widest_aspect : index;
    settings.has_aspect_ratio = settings.has_adaptive_view = true;
    settings.adaptive_view = index == window_aspect;
    settings.aspect_num = aspect_widths[fixed];
    settings.aspect_den = aspect_heights[fixed];
}

int scale(const Settings& settings) { return settings.supersampling - 1; }
void set_scale(Settings& settings, int index) { settings.has_supersampling = true; settings.supersampling = index + 1; }
// One list over two saved keys: the samples a pixel, then the same counts with every sample shaded.
constexpr int sample_levels = 3;
bool antialiasing_saved(const Settings& settings) { return settings.has_multisampling || settings.has_sample_shading; }
int antialiasing(const Settings& settings) {
    const auto found = std::ranges::find(PSXRecompV4::kDisruptorSampleCounts, settings.multisampling);
    const int samples = found == std::end(PSXRecompV4::kDisruptorSampleCounts) ? 0
        : static_cast<int>(found - std::begin(PSXRecompV4::kDisruptorSampleCounts));
    return samples > 0 && settings.sample_shading ? samples + sample_levels : samples;
}
void set_antialiasing(Settings& settings, int index) {
    index = std::clamp(index, 0, 2 * sample_levels);
    settings.has_multisampling = settings.has_sample_shading = true;
    settings.sample_shading = index > sample_levels;
    settings.multisampling = PSXRecompV4::kDisruptorSampleCounts[settings.sample_shading ? index - sample_levels : index];
}
int draw_distance(const Settings& settings) {
    const auto found = std::ranges::find(PSXRecompV4::kDisruptorDrawDistances, settings.draw_distance);
    return found == std::end(PSXRecompV4::kDisruptorDrawDistances) ? -1
        : static_cast<int>(found - std::begin(PSXRecompV4::kDisruptorDrawDistances));
}
void set_draw_distance(Settings& settings, int index) {
    settings.has_draw_distance = true;
    settings.draw_distance = PSXRecompV4::kDisruptorDrawDistances[std::clamp(
        index, 0, static_cast<int>(std::size(PSXRecompV4::kDisruptorDrawDistances)) - 1)];
}
int vsync(const Settings& settings) { return std::clamp(settings.vsync + 1, 0, 2); }
void set_vsync(Settings& settings, int index) { settings.has_vsync = true; settings.vsync = index - 1; }
int resolution(const Settings& settings) { return disruptor::resolution::index_of(settings.resolution); }
void set_resolution(Settings& settings, int index) {
    settings.has_resolution = true;
    settings.resolution = std::string(disruptor::resolution::at(index));
}
// settings.toml keeps the dead zone in the stick's raw units, 0 to 32767: shown as a share of the stick's travel.
constexpr int runtime_deadzone = 12000;
int percent_of(int raw) { return static_cast<int>(std::lround(raw * 100.0 / 32767.0)); }
int deadzone_percent(const Settings& settings) { return percent_of(settings.deadzone); }
void set_deadzone_percent(Settings& settings, int percent) {
    settings.has_deadzone = true;
    // The runtime's own 12000 is no whole share: its share, the reset value, writes it back exactly.
    settings.deadzone = percent == percent_of(runtime_deadzone) ? runtime_deadzone
                                                                : static_cast<int>(std::lround(percent * 32767.0 / 100.0));
}
int scheme(const Settings& settings) { return settings.modern_controls ? 1 : 0; }
void set_scheme(Settings& settings, int index) {
    settings.has_modern_controls = true;
    settings.modern_controls = index == 1;
}

template <auto Value> int thousandths(const Settings& settings) {
    return static_cast<int>(std::lround(settings.*Value * 1000.0));
}
template <auto Has, auto Value> void set_thousandths(Settings& settings, int value) {
    settings.*Has = true;
    settings.*Value = value / 1000.0;
}

constexpr const char* display_modes[] = {"Windowed", "Borderless fullscreen", "Exclusive fullscreen"};
constexpr const char* aspects[] = {"4:3", "16:9", "21:9", "32:9", "Match window (up to 32:9)"};
constexpr const char* scales[] = {"1x (native)", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
constexpr const char* vsyncs[] = {"Adaptive", "Immediate", "Synchronised"};
constexpr const char* schemes[] = {"Classic", "Modern"};
constexpr const char* antialiasing_levels[] = {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "2x SSAA", "4x SSAA", "8x SSAA"};
constexpr const char* draw_distances[] = {"Original", "1.25x", "1.5x", "2x"};
constexpr const char* fog_shapes[] = {"Original", "Soft", "Late"};
constexpr int distances_offered = 2;  /* Original and 1.25x for now */
constexpr int fog_shapes_offered = 1;  /* Original only for now */
constexpr const char* bass_cuts[] = {"Original", "Reduced", "TV speaker"};
constexpr const char* movie_filters[] = {"Original", "Smooth"};
constexpr const char* popup_times[] = {"3 seconds", "5 seconds", "8 seconds", "12 seconds", "15 seconds", "30 seconds"};
constexpr const char* popup_corners[] = {"Top right", "Top left", "Bottom right", "Bottom left"};

constexpr const char* geometry_key = "disruptor.geometry_correction";
constexpr const char* frames_key = "video.frame_interpolation";
constexpr const char* rate_key = "video.frame_interpolation_fps";
constexpr const char* occlusion_key = "disruptor.ambient_occlusion";
constexpr const char* aim_key = "disruptor.mouse_aim";
constexpr const char* vertical_key = "disruptor.vertical_look";
constexpr const char* separate_key = "disruptor.separate_vertical_speed";
constexpr const char* smooth_fade_key = "disruptor.smooth_fade";
constexpr const char* number_keys_key = "disruptor.number_keys";
constexpr const char* probes_key = "disruptor.probe_lighting";
constexpr const char* shadows_key = "disruptor.dynamic_shadows";
constexpr const char* shots_key = "disruptor.shot_lights";
constexpr const char* sun_key = "disruptor.sun_lighting";
constexpr const char* needs_probes[] = {probes_key};
constexpr const char* needs_shadows[] = {shadows_key};
constexpr const char* needs_shots[] = {shots_key};
constexpr const char* needs_sun[] = {sun_key};
constexpr const char* needs_geometry[] = {geometry_key};
constexpr const char* needs_frames[] = {frames_key};
constexpr const char* needs_occlusion[] = {occlusion_key};
constexpr const char* needs_aim_and_geometry[] = {aim_key, geometry_key};
constexpr const char* needs_vertical[] = {vertical_key};
constexpr const char* needs_separate[] = {separate_key};
constexpr const char* needs_smooth_fade[] = {smooth_fade_key};
constexpr const char* needs_number_keys[] = {number_keys_key};
constexpr const char* popups_key = "disruptor.achievements_popups";
constexpr const char* needs_popups[] = {popups_key};
// Classic is the list's first, the one a needing row reads as off.
constexpr const char* stick_layouts[] = {"Classic", "Dual analog", "Southpaw"};
constexpr const char* layout_key = "disruptor.stick_layout";
constexpr const char* needs_layout[] = {layout_key};
constexpr const char* needs_layout_and_vertical[] = {layout_key, vertical_key};
constexpr int default_sensitivity = 80;

// A file from before the switch keeps the vertical speed it set where that differs from the horizontal one.
bool vertical_differs(const Settings& settings) {
    const long horizontal =
        settings.has_horizontal_sensitivity ? std::lround(settings.horizontal_sensitivity * 1000.0) : default_sensitivity;
    return settings.has_vertical_sensitivity && std::lround(settings.vertical_sensitivity * 1000.0) != horizontal;
}
bool separate_saved(const Settings& settings) { return settings.has_separate_vertical_speed || vertical_differs(settings); }
int separate(const Settings& settings) {
    return settings.has_separate_vertical_speed ? settings.separate_vertical_speed : vertical_differs(settings);
}
// Writing a speed first writes down the switch such a file implies, so that a later speed cannot turn it over.
template <auto Has, auto Value> void set_speed(Settings& settings, int value) {
    if (!settings.has_separate_vertical_speed) {
        settings.separate_vertical_speed = vertical_differs(settings);
        settings.has_separate_vertical_speed = true;
    }
    set_thousandths<Has, Value>(settings, value);
}

template <auto Has, auto Value>
constexpr Option toggle(const char* key, const char* label, int initial, const char* hint,
                        std::span<const char* const> needs = {}) {
    return {key, Kind::toggle, label, saved<Has>, read<Value>, write<Has, Value>, initial, 0, 1, {}, needs,
            Unit::none, hint};
}

constexpr Option toggle(const char* key, const char* label, int initial, const char* hint, Saved saved, Get get, Set set,
                        std::span<const char* const> needs = {}) {
    return {key, Kind::toggle, label, saved, get, set, initial, 0, 1, {}, needs, Unit::none, hint};
}

constexpr Option choice(const char* key, const char* label, int initial, const char* hint,
                        std::span<const char* const> choices, Saved saved, Get get, Set set,
                        std::span<const char* const> needs = {}, int offered = 0, const char* notice = nullptr) {
    return {key, Kind::choice, label, saved, get, set, initial, 0,
            (offered > 0 ? offered : static_cast<int>(choices.size())) - 1, choices, needs, Unit::none, hint, notice};
}

constexpr Option slider(const char* key, const char* label, int initial, const char* hint,
                        int lowest, int highest, Unit unit, Saved saved, Get get, Set set,
                        std::span<const char* const> needs = {}) {
    return {key, Kind::slider, label, saved, get, set, initial, lowest, highest, {}, needs, unit, hint};
}

constexpr Option option_list[] = {
    choice("video.fullscreen", "Display mode", 0,
           "Exclusive fullscreen: the screen is the game's alone\n"
           "Alt+Enter switches between a window and fullscreen in the game", display_modes,
           saved<&Settings::has_fullscreen>, read<&Settings::fullscreen>,
           write<&Settings::has_fullscreen, &Settings::fullscreen>),
    choice("video.aspect_ratio", "Aspect ratio", 0,
           "4:3: the original picture\nWider ratios show more of the world at the sides\nMovies stay 4:3", aspects,
           aspect_saved, aspect, set_aspect),
    choice("video.supersampling", "Internal resolution scale", 3,
           "Higher is sharper\nDoubling the scale makes four times the graphics card's work", scales,
           saved<&Settings::has_supersampling>, scale, set_scale),
    toggle<&Settings::has_dynamic_resolution, &Settings::dynamic_resolution>(
        "disruptor.dynamic_resolution", "Dynamic resolution", 0,
        "Lowers the internal resolution while the graphics card cannot keep up\nRaises it back to the scale above when it can\n"
        "Meant for weaker graphics cards, integrated ones above all"),
    choice("disruptor.resolution", "Resolution", 0,
           "An exclusive fullscreen's display mode, the nearest the display has\nAn ordinary window's size\nBorderless fullscreen keeps the desktop's",
           disruptor::resolution::kChoices, saved<&Settings::has_resolution>, resolution, set_resolution),
    toggle<&Settings::has_render_thread, &Settings::render_thread>(
        "disruptor.render_thread", "Render thread", 0,
        "Can make the game smoother on slower processors and integrated graphics\n"
        "Sends the drawing to the graphics card from a thread of its own\n"
        "Has no effect with in-between frames or lighting on"),
    choice("video.vsync", "VSync", 2,
           "Synchronised: waits for the display, the picture never tears\n"
           "Immediate: does not wait\n"
           "Adaptive: waits only while the game keeps up with the display",
           vsyncs, saved<&Settings::has_vsync>, vsync, set_vsync),
    slider("disruptor.hud_scale", "HUD size", 100,
           "Shrinks the health and ammunition displays toward the edges\nThe weapon keeps its size\nSmaller text is drawn bolder, with a dark edge\nWidescreen only", 50, 100, Unit::percent,
           saved<&Settings::has_hud_scale>, read<&Settings::hud_scale>,
           write<&Settings::has_hud_scale, &Settings::hud_scale>),

    toggle<&Settings::has_geometry_correction, &Settings::geometry_correction>(
        "disruptor.geometry_correction", "Exact-provenance geometry", 1,
        "Steadies the PlayStation's wobbling edges"),
    toggle<&Settings::has_perspective_textures, &Settings::perspective_textures>(
        "disruptor.perspective_textures", "Perspective-correct world textures", 1,
        "Draws wall and floor textures in true perspective\n"
        "The PlayStation's own way bends them near the viewer", needs_geometry),
    toggle<&Settings::has_gap_filling, &Settings::gap_filling>(
        "disruptor.gap_filling", "Gap filling", 1,
        "Closes hairline gaps between polygons", needs_geometry),
    toggle<&Settings::has_depth_sorted_world, &Settings::depth_sorted_world>(
        "disruptor.depth_sorted_world", "Depth-sorted world", 1,
        "Keeps nearer world surfaces in front of farther ones", needs_geometry),
    toggle<&Settings::has_smooth_fade, &Settings::smooth_fade>(
        "disruptor.smooth_fade", "Smooth distance fade", 1,
        "Fades walls and floors evenly into the distance\n"
        "The world's far edge dissolves instead of appearing in squares", needs_geometry),
    choice("disruptor.movie_filter", "Movie filter", 1,
           "Smooth takes away the movies' square blocks and grain",
           movie_filters, saved<&Settings::has_movie_filter>, read<&Settings::movie_filter>,
           write<&Settings::has_movie_filter, &Settings::movie_filter>),
    toggle<&Settings::has_ambient_occlusion, &Settings::ambient_occlusion>(
        "disruptor.ambient_occlusion", "Ambient occlusion", 0,
        "Shades corners and the floor under things", needs_geometry),
    slider("disruptor.occlusion_reach", "Occlusion reach", 160,
           "How far the shade spreads from corners and objects", 50, 160, Unit::none,
           saved<&Settings::has_occlusion_reach>, read<&Settings::occlusion_reach>,
           write<&Settings::has_occlusion_reach, &Settings::occlusion_reach>, needs_occlusion),
    slider("disruptor.occlusion_strength", "Occlusion strength", 100,
           "", 0, 100, Unit::percent,
           saved<&Settings::has_occlusion_strength>, read<&Settings::occlusion_strength>,
           write<&Settings::has_occlusion_strength, &Settings::occlusion_strength>, needs_occlusion),
    toggle<&Settings::has_frame_unlock, &Settings::frame_unlock>(
        "disruptor.frame_unlock", "60 FPS gameplay", 1,
        "Up to 60 frames a second instead of the original rate"),
    toggle<&Settings::has_frame_interpolation, &Settings::frame_interpolation>(
        "video.frame_interpolation", "In-between frames", 0,
        "Extra pictures between the game's frames, for smoother motion", needs_geometry),
    slider("video.frame_interpolation_fps", "In-between frame rate", 0,
           "Pictures shown per second while in-between frames are on",
           kDisruptorPresentRateLowest, kDisruptorPresentRateHighest, Unit::frames,
           saved<&Settings::has_frame_interpolation_fps>, read<&Settings::frame_interpolation_fps>,
           write<&Settings::has_frame_interpolation_fps, &Settings::frame_interpolation_fps>, needs_frames),
    toggle<&Settings::has_skip_intro, &Settings::skip_intro>(
        "disruptor.skip_intro", "Skip the logos and the title movie", 0,
        "Escape skips a logo or a movie either way"),

    toggle<&Settings::has_mouse_aim, &Settings::mouse_aim>(
        "disruptor.mouse_aim", "Horizontal mouse aim", 1,
        "In the game a middle-click captures the mouse\nA middle-click or Escape releases it"),
    choice("disruptor.modern_controls", "Control scheme", 1,
           "Classic: The original keys\n"
           "  The mouse only turns\n"
           "Modern: WASD moves\n"
           "  Left click fires, right click uses psionics\n"
           "  Side buttons choose a weapon or a psionic\n"
           "Keys you set stay either way",
           schemes, saved<&Settings::has_modern_controls>, scheme, set_scheme),
    toggle<&Settings::has_number_keys, &Settings::number_keys>(
        "disruptor.number_keys", "Weapon and psionic keys", 0,
        "A key puts a weapon or a psionic you own in hand\n"
        "Weapons: 1 to 9 and 0 at first, on the keypad too\n"
        "Powers: no keys at first"),
    toggle<&Settings::has_psionic_keys_use, &Settings::psionic_keys_use>(
        "disruptor.psionic_keys_use", "Psionic keys use the power", 0,
        "A power's key also uses the power once it is in hand\nThe power comes to hand twice as fast\nHolding the key keeps using it, like Psionic attack", needs_number_keys),
    toggle<&Settings::has_selection_wheels, &Settings::selection_wheels>(
        "disruptor.selection_wheels", "Weapon and psionic wheels", 0,
        "Hold Choose weapon or Choose psionic, point with the mouse or the left stick, let go to equip\n"
        "Escape or a right click cancels\n"
        "The game keeps running while a wheel is open"),
    slider("disruptor.horizontal_sensitivity", "Horizontal sensitivity", 80,
           "", 5, 2000, Unit::thousandths,
           saved<&Settings::has_horizontal_sensitivity>, thousandths<&Settings::horizontal_sensitivity>,
           set_speed<&Settings::has_horizontal_sensitivity, &Settings::horizontal_sensitivity>),
    toggle<&Settings::has_invert_horizontal, &Settings::invert_horizontal>(
        "disruptor.invert_horizontal", "Invert horizontal mouse", 0,
        ""),
    toggle<&Settings::has_high_precision_camera, &Settings::high_precision_camera>(
        "disruptor.high_precision_camera", "Smooth mouse turning", 1,
        "Turns the view smoothly between the game's 256 steps\nAlso needs Exact-provenance geometry", needs_aim_and_geometry),
    toggle<&Settings::has_vertical_look, &Settings::vertical_look>(
        "disruptor.vertical_look", "Vertical mouse look", 0,
        "Looks up and down, about 31 degrees each way"),
    toggle("disruptor.separate_vertical_speed", "Separate vertical speed", 0,
           "Off: tilts at the horizontal speed", separate_saved,
           separate, write<&Settings::has_separate_vertical_speed, &Settings::separate_vertical_speed>, needs_vertical),
    slider("disruptor.vertical_sensitivity", "Vertical sensitivity", 80,
           "", 5, 2000, Unit::thousandths,
           saved<&Settings::has_vertical_sensitivity>, thousandths<&Settings::vertical_sensitivity>,
           set_speed<&Settings::has_vertical_sensitivity, &Settings::vertical_sensitivity>, needs_separate),
    toggle<&Settings::has_invert_vertical, &Settings::invert_vertical>(
        "disruptor.invert_vertical", "Invert vertical mouse", 0,
        "Looks down when the mouse moves forward", needs_vertical),
    slider("controller.deadzone", "Stick dead zone", 37,
           "How far the moving stick goes before it counts\nRaise it if you walk or turn without touching the stick", 0, 90,
           Unit::percent, saved<&Settings::has_deadzone>, deadzone_percent, set_deadzone_percent),
    choice("disruptor.stick_layout", "Stick layout", 1,
           "Dual analog: The left stick moves and strafes\n"
           "  The right stick turns\n"
           "Classic: The left stick moves and turns, as the D-pad does\n"
           "Southpaw: Dual analog with the sticks swapped\n"
           "In menus the left stick works as the D-pad",
           stick_layouts, saved<&Settings::has_stick_layout>, read<&Settings::stick_layout>,
           write<&Settings::has_stick_layout, &Settings::stick_layout>),
    slider("disruptor.look_speed", "Turning speed", 100,
           "100%: A full push turns as fast as the D-pad", PSXRecompV4::kDisruptorLookSpeedLowest,
           PSXRecompV4::kDisruptorLookSpeedHighest,
           Unit::percent, saved<&Settings::has_look_speed>, read<&Settings::look_speed>,
           write<&Settings::has_look_speed, &Settings::look_speed>, needs_layout),
    slider("disruptor.look_deadzone", "Turning dead zone", 20,
           "How far the turning stick moves before it counts", 0, PSXRecompV4::kDisruptorLookDeadzoneHighest,
           Unit::percent, saved<&Settings::has_look_deadzone>, read<&Settings::look_deadzone>,
           write<&Settings::has_look_deadzone, &Settings::look_deadzone>, needs_layout),
    toggle<&Settings::has_invert_look, &Settings::invert_look>(
        "disruptor.invert_look", "Invert vertical stick", 0,
        "Looks down when the turning stick is pushed forward\nNeeds Vertical mouse look, under Mouse",
        needs_layout_and_vertical),

    slider("audio.master_volume", "Master volume", 100,
           "", 0, 100, Unit::percent,
           saved<&Settings::has_master_volume>, read<&Settings::master_volume>,
           write<&Settings::has_master_volume, &Settings::master_volume>),
    choice("disruptor.bass_cut", "Bass", 0,
           "How much deep bass the game's sound keeps, music and movies too\n"
           "Reduced: the deepest rumble is softer\n"
           "TV speaker: as a television's own speakers played it",
           bass_cuts, saved<&Settings::has_bass_cut>, read<&Settings::bass_cut>,
           write<&Settings::has_bass_cut, &Settings::bass_cut>),
    toggle<&Settings::has_mute_in_background, &Settings::mute_in_background>(
        "disruptor.mute_in_background", "Mute in the background", 0,
        ""),
    toggle<&Settings::has_audio_muted, &Settings::audio_muted>("audio.muted", "Mute all audio", 0,
        ""),
    toggle<&Settings::has_achievements, &Settings::achievements>(
        "disruptor.achievements", "Count achievements", 1,
        "Signs in with your RetroAchievements account and counts unlocks\nOff: the account stays signed in"),
    toggle<&Settings::has_achievements_encore, &Settings::achievements_encore>(
        "disruptor.achievements_encore", "Encore", 0,
        "Unlocked achievements can trigger again, with their notification\nTakes effect when the game starts"),
    toggle<&Settings::has_achievements_unofficial, &Settings::achievements_unofficial>(
        "disruptor.achievements_unofficial", "Unofficial achievements", 1,
        "Also counts the set's unofficial achievements\n"
        "RetroAchievements does not record them\n"
        "Takes effect when the game starts"),
    toggle<&Settings::has_achievements_popups, &Settings::achievements_popups>(
        "disruptor.achievements_popups", "Show notifications", 1,
        ""),
    choice("disruptor.achievements_popup_time", "Notification time", 3,
           "", popup_times,
           saved<&Settings::has_achievements_popup_time>, read<&Settings::achievements_popup_time>,
           write<&Settings::has_achievements_popup_time, &Settings::achievements_popup_time>, needs_popups),
    choice("disruptor.achievements_corner", "Notification corner", 0,
           "Also sets where the progress indicator appears", popup_corners,
           saved<&Settings::has_achievements_corner>, read<&Settings::achievements_corner>,
           write<&Settings::has_achievements_corner, &Settings::achievements_corner>),
    toggle<&Settings::has_achievements_progress, &Settings::achievements_progress>(
        "disruptor.achievements_progress", "Progress indicator", 0,
        "Shows for a moment how far an achievement has counted\nSuch as the kills and pickups of a 100% achievement"),
    toggle<&Settings::has_achievements_sound, &Settings::achievements_sound>(
        "disruptor.achievements_sound", "Unlock sound", 1,
        "Plays at the game's volume"),
    toggle<&Settings::has_detailed_logging, &Settings::detailed_logging>(
        "disruptor.detailed_logging", "Detailed logging", 0,
        "Writes more into the game's log for a bug report"),

    choice("disruptor.multisampling", "Anti-aliasing (MSAA / SSAA)", 0,
           "MSAA: smooths polygon edges and the HUD\n"
           "SSAA: also smooths textures and cut-out outlines\n"
           "Higher counts cost the graphics card more, SSAA the most",
           antialiasing_levels,
           antialiasing_saved, antialiasing, set_antialiasing, needs_geometry),
    choice("disruptor.draw_distance", "Draw distance", 0,
           "How far walls, enemies and pickups are drawn",
           draw_distances, saved<&Settings::has_draw_distance>, draw_distance, set_draw_distance, {}, distances_offered),
    toggle<&Settings::has_probe_lighting, &Settings::probe_lighting>(
        "disruptor.probe_lighting", "Probe lighting", 0,
        "Light bounced from the walls around you", needs_geometry),
    slider("disruptor.probe_strength", "Probe light strength", 100,
           "", 0, 200, Unit::percent,
           saved<&Settings::has_probe_strength>, read<&Settings::probe_strength>,
           write<&Settings::has_probe_strength, &Settings::probe_strength>, needs_probes),
    slider("disruptor.probe_bounce", "Second bounce", 0, "Light that bounced twice", 0, 100, Unit::percent,
           saved<&Settings::has_probe_bounce>, read<&Settings::probe_bounce>,
           write<&Settings::has_probe_bounce, &Settings::probe_bounce>, needs_probes),
    slider("disruptor.probe_glow", "Glow of bright surfaces", 200, "How much lamps and screens light round them", 0, 400,
           Unit::percent, saved<&Settings::has_probe_glow>, read<&Settings::probe_glow>,
           write<&Settings::has_probe_glow, &Settings::probe_glow>, needs_probes),
    toggle<&Settings::has_dynamic_shadows, &Settings::dynamic_shadows>(
        "disruptor.dynamic_shadows", "Dynamic shadows", 0,
        "Enemies, bodies and pickups cast shadows from the level's lights\nA soft shadow under each of them",
        needs_geometry),
    slider("disruptor.shadow_strength", "Shadow strength", 100, "How dark the shadows from the level's lights are", 0, 200,
           Unit::percent, saved<&Settings::has_shadow_strength>, read<&Settings::shadow_strength>,
           write<&Settings::has_shadow_strength, &Settings::shadow_strength>, needs_shadows),
    slider("disruptor.shadow_underneath", "Shadow underneath", 100, "How dark the soft shadow under each one is", 0, 200,
           Unit::percent, saved<&Settings::has_shadow_underneath>, read<&Settings::shadow_underneath>,
           write<&Settings::has_shadow_underneath, &Settings::shadow_underneath>, needs_shadows),
    toggle<&Settings::has_shot_lights, &Settings::shot_lights>(
        "disruptor.shot_lights", "Light from shots and blasts", 0, "Shots, impacts and blasts light the walls, the floor and enemies",
        needs_geometry),
    slider("disruptor.shot_light_strength", "Light strength", 150, "", 0, 400,
           Unit::percent, saved<&Settings::has_shot_light_strength>, read<&Settings::shot_light_strength>,
           write<&Settings::has_shot_light_strength, &Settings::shot_light_strength>, needs_shots),
    toggle<&Settings::has_sun_lighting, &Settings::sun_lighting>(
        "disruptor.sun_lighting", "Sun on open levels", 0, "Sunlight and shadows on the second mission for now", needs_geometry),
    slider("disruptor.sun_direction", "Sun direction", 35, "", 0, 360, Unit::degrees,
           saved<&Settings::has_sun_direction>, read<&Settings::sun_direction>,
           write<&Settings::has_sun_direction, &Settings::sun_direction>, needs_sun),
    slider("disruptor.sun_height", "Sun height", 50, "", 1, 89, Unit::degrees,
           saved<&Settings::has_sun_height>, read<&Settings::sun_height>,
           write<&Settings::has_sun_height, &Settings::sun_height>, needs_sun),
    slider("disruptor.sun_strength", "Sun strength", 110, "100% keeps sunlit ground as the game lights it", 1, 400,
           Unit::percent, saved<&Settings::has_sun_strength>, read<&Settings::sun_strength>,
           write<&Settings::has_sun_strength, &Settings::sun_strength>, needs_sun),
    slider("disruptor.sun_shade", "Shade", 50, "How much darker what the sun does not reach is", 0, 90, Unit::percent,
           saved<&Settings::has_sun_shade>, read<&Settings::sun_shade>,
           write<&Settings::has_sun_shade, &Settings::sun_shade>, needs_sun),
    choice("disruptor.fog_shape", "Fog shape", 0,
           "How the smooth distance fade thickens\n"
           "Original: at one pace, as the game's own",
           fog_shapes, saved<&Settings::has_fog_shape>, read<&Settings::fog_shape>,
           write<&Settings::has_fog_shape, &Settings::fog_shape>, needs_smooth_fade, fog_shapes_offered,
           "Other shapes come in a later release"),
};
} // namespace

std::span<const Option> options() { return option_list; }

const Option* find_option(const char* key) {
    for (const auto& option : option_list)
        if (std::string_view(option.key) == key) return &option;
    return nullptr;
}

bool applies(const Option& option, const std::function<bool(const Option&)>& on) {
    if (option.kind == Kind::toggle && option.highest == 0) return false;
    return std::all_of(option.needs.begin(), option.needs.end(), [&](const char* key) {
        const Option* needed = find_option(key);
        return needed && on(*needed) && applies(*needed, on);
    });
}

int stored(const Settings& settings, const Option& option) {
    if (!option.saved(settings)) return option.initial;
    const int value = option.get(settings);
    return (option.kind == Kind::choice || option.kind == Kind::toggle) && value > option.highest ? option.initial : value;
}

Settings with_defaults(Settings settings) {
    for (const auto& option : option_list)
        if (std::string_view(option.key).starts_with("disruptor") && !option.saved(settings))
            option.set(settings, option.initial);
    return settings;
}

std::string describe(const Settings& settings) {
    std::string text;
    for (const auto& option : option_list) {
        if (!text.empty()) text += ' ';
        text += std::string(option.key) + '=' + std::to_string(stored(settings, option));
    }
    return text;
}

} // namespace disruptor::settings
