#include "game/settings/options.h"

#include "game/frame/present_rate.h"

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
constexpr int sample_levels = 4;
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
constexpr const char* antialiasing_levels[] = {"Off",     "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA",
                                               "2x SSAA", "4x SSAA", "8x SSAA", "16x SSAA"};
constexpr const char* draw_distances[] = {"Original", "1.25x", "1.5x", "2x"};
constexpr const char* fog_shapes[] = {"Original", "Soft", "Late"};
constexpr int distances_offered = 2;  /* Original and 1.25x for now */
constexpr int fog_shapes_offered = 1;  /* Original only for now */
constexpr const char* bass_cuts[] = {"Original", "Reduced", "TV speaker"};
constexpr const char* movie_filters[] = {"Original", "Smooth"};

constexpr const char* geometry_key = "disruptor.geometry_correction";
constexpr const char* frames_key = "video.frame_interpolation";
constexpr const char* rate_key = "video.frame_interpolation_fps";
constexpr const char* occlusion_key = "disruptor.ambient_occlusion";
constexpr const char* aim_key = "disruptor.mouse_aim";
constexpr const char* vertical_key = "disruptor.vertical_look";
constexpr const char* separate_key = "disruptor.separate_vertical_speed";
constexpr const char* smooth_fade_key = "disruptor.smooth_fade";
constexpr const char* number_keys_key = "disruptor.number_keys";
constexpr const char* needs_geometry[] = {geometry_key};
constexpr const char* needs_frames[] = {frames_key};
constexpr const char* needs_occlusion[] = {occlusion_key};
constexpr const char* needs_aim_and_geometry[] = {aim_key, geometry_key};
constexpr const char* needs_vertical[] = {vertical_key};
constexpr const char* needs_separate[] = {separate_key};
constexpr const char* needs_smooth_fade[] = {smooth_fade_key};
constexpr const char* needs_number_keys[] = {number_keys_key};
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
           "Windowed: a window\n"
           "Borderless fullscreen: a window the size of the screen\n"
           "Exclusive fullscreen: the screen is the game's alone\n"
           "Alt+Enter switches between a window and fullscreen while playing", display_modes,
           saved<&Settings::has_fullscreen>, read<&Settings::fullscreen>,
           write<&Settings::has_fullscreen, &Settings::fullscreen>),
    choice("video.aspect_ratio", "Aspect ratio", 0,
           "4:3: the original picture\n"
           "16:9 and wider: more of the world at the sides\n"
           "Match window: follows the window's shape, up to 32:9\n"
           "Movies stay 4:3", aspects,
           aspect_saved, aspect, set_aspect),
    choice("video.supersampling", "Internal resolution scale", 3,
           "Draws the picture at this multiple of the original resolution\nHigher is sharper\n"
           "The graphics card's work grows with the square of the value", scales,
           saved<&Settings::has_supersampling>, scale, set_scale),
    choice("video.vsync", "VSync", 2,
           "Synchronised: waits for the display, the picture never tears\n"
           "Immediate: does not wait\n"
           "Adaptive: waits only while the game keeps up with the display",
           vsyncs, saved<&Settings::has_vsync>, vsync, set_vsync),
    slider("disruptor.hud_scale", "HUD size", 100,
           "Widescreen only\nShrinks the health and ammunition displays toward the screen edges\n"
           "The weapon keeps its size", 50, 100, Unit::percent,
           saved<&Settings::has_hud_scale>, read<&Settings::hud_scale>,
           write<&Settings::has_hud_scale, &Settings::hud_scale>),

    toggle<&Settings::has_geometry_correction, &Settings::geometry_correction>(
        "disruptor.geometry_correction", "Exact-provenance geometry", 1,
        "Places polygons from the game's own arithmetic, before it rounds them to whole pixels\n"
        "This steadies the PlayStation's wobbling edges\n"
        "With it off, the options that need it are greyed out"),
    toggle<&Settings::has_perspective_textures, &Settings::perspective_textures>(
        "disruptor.perspective_textures", "Perspective-correct world textures", 1,
        "Draws wall and floor textures in true perspective\n"
        "The PlayStation's own way bends them near the viewer", needs_geometry),
    toggle<&Settings::has_gap_filling, &Settings::gap_filling>(
        "disruptor.gap_filling", "Gap filling", 1,
        "Closes hairline gaps between polygons\n"
        "Keeps the game's own patches off their neighbours", needs_geometry),
    toggle<&Settings::has_depth_sorted_world, &Settings::depth_sorted_world>(
        "disruptor.depth_sorted_world", "Depth-sorted world", 1,
        "Draws world polygons by their depth where the game's own order puts a far one over a near "
        "one", needs_geometry),
    toggle<&Settings::has_smooth_fade, &Settings::smooth_fade>(
        "disruptor.smooth_fade", "Smooth distance fade", 1,
        "Fades walls and floors evenly into the distance\n"
        "The game itself steps through twelve shades\n"
        "The world's far edge dissolves instead of appearing in squares", needs_geometry),
    choice("disruptor.movie_filter", "Movie filter", 1,
           "How the game's movies are drawn at the size of your screen\n"
           "Original: every pixel shows as a square\n"
           "Smooth: the square blocks and the grain smoothed away",
           movie_filters, saved<&Settings::has_movie_filter>, read<&Settings::movie_filter>,
           write<&Settings::has_movie_filter, &Settings::movie_filter>),
    toggle<&Settings::has_ambient_occlusion, &Settings::ambient_occlusion>(
        "disruptor.ambient_occlusion", "Ambient occlusion", 0,
        "Shades corners and the floor under things", needs_geometry),
    slider("disruptor.occlusion_reach", "Occlusion reach", 160,
           "How far from a corner or an object the shade spreads", 50, 800, Unit::none,
           saved<&Settings::has_occlusion_reach>, read<&Settings::occlusion_reach>,
           write<&Settings::has_occlusion_reach, &Settings::occlusion_reach>, needs_occlusion),
    slider("disruptor.occlusion_strength", "Occlusion strength", 80,
           "How dark the shade is", 0, 300, Unit::percent,
           saved<&Settings::has_occlusion_strength>, read<&Settings::occlusion_strength>,
           write<&Settings::has_occlusion_strength, &Settings::occlusion_strength>, needs_occlusion),
    toggle<&Settings::has_frame_unlock, &Settings::frame_unlock>(
        "disruptor.frame_unlock", "60 FPS gameplay", 1,
        "Lets the game run at up to 60 frames a second in place of its original rate"),
    toggle<&Settings::has_frame_interpolation, &Settings::frame_interpolation>(
        "video.frame_interpolation", "In-between frames", 0,
        "Shows extra pictures between the game's frames, for smoother motion\n"
        "They are built from the game's own geometry", needs_geometry),
    slider("video.frame_interpolation_fps", "In-between frame rate", 0,
           "Pictures shown per second while in-between frames are on",
           kDisruptorPresentRateLowest, kDisruptorPresentRateHighest, Unit::frames,
           saved<&Settings::has_frame_interpolation_fps>, read<&Settings::frame_interpolation_fps>,
           write<&Settings::has_frame_interpolation_fps, &Settings::frame_interpolation_fps>, needs_frames),
    toggle<&Settings::has_skip_intro, &Settings::skip_intro>(
        "disruptor.skip_intro", "Skip the logos and the title movie", 0,
        "Starts at the main menu without the logos and the title movie\n"
        "Escape skips a logo or a movie either way"),

    toggle<&Settings::has_mouse_aim, &Settings::mouse_aim>(
        "disruptor.mouse_aim", "Horizontal mouse aim", 1,
        "Turns the view with the mouse\n"
        "In the game a middle-click captures the mouse\nA middle-click or Escape releases it"),
    choice("disruptor.modern_controls", "Control scheme", 1,
           "Classic: the original keys\nThe mouse only turns\n"
           "Modern: WASD, left click fires, right click uses psionics, side buttons choose\n"
           "Keys you set yourself stay either way",
           schemes, saved<&Settings::has_modern_controls>, scheme, set_scheme),
    toggle<&Settings::has_number_keys, &Settings::number_keys>(
        "disruptor.number_keys", "Weapon and psionic keys", 0,
        "A key puts a weapon or a psionic power you own in hand\n"
        "Weapons: 1 to 9 and 0 at first, on the keypad too\n"
        "Powers: no keys at first\n"
        "On a change the game's list shows for a second\n"
        "Each takes two keys"),
    toggle<&Settings::has_psionic_keys_use, &Settings::psionic_keys_use>(
        "disruptor.psionic_keys_use", "Psionic keys use the power", 1,
        "A power's key also uses the power once it is in hand\n"
        "Holding the key keeps using it, like Psionic attack\n"
        "Off: the key only picks the power", needs_number_keys),
    toggle<&Settings::has_selection_wheels, &Settings::selection_wheels>(
        "disruptor.selection_wheels", "Weapon and psionic wheels", 0,
        "Replaces both selection lists with wheels\n"
        "Hold Choose weapon or Choose psionic, point with the mouse or the left stick, let go to equip\n"
        "Escape or a right click cancels\n"
        "The game keeps running while a wheel is open"),
    slider("disruptor.horizontal_sensitivity", "Horizontal sensitivity", 80,
           "How far the view turns for a movement of the mouse", 5, 2000, Unit::thousandths,
           saved<&Settings::has_horizontal_sensitivity>, thousandths<&Settings::horizontal_sensitivity>,
           set_speed<&Settings::has_horizontal_sensitivity, &Settings::horizontal_sensitivity>),
    toggle<&Settings::has_invert_horizontal, &Settings::invert_horizontal>(
        "disruptor.invert_horizontal", "Invert horizontal mouse", 0,
        "Turns the view left when the mouse moves right"),
    toggle<&Settings::has_high_precision_camera, &Settings::high_precision_camera>(
        "disruptor.high_precision_camera", "Smooth mouse turning", 1,
        "The game turns in 256 steps of a circle\n"
        "This also turns the picture by the part of a step in between\n"
        "Needs Horizontal mouse aim and Exact-provenance geometry", needs_aim_and_geometry),
    toggle<&Settings::has_vertical_look, &Settings::vertical_look>(
        "disruptor.vertical_look", "Vertical mouse look", 0,
        "Looks up and down with the mouse, about 31 degrees each way\n"
        "The original game has no such view"),
    toggle("disruptor.separate_vertical_speed", "Separate vertical speed", 0,
           "Off: the view tilts at the horizontal speed\n"
           "On: the vertical speed below applies", separate_saved,
           separate, write<&Settings::has_separate_vertical_speed, &Settings::separate_vertical_speed>, needs_vertical),
    slider("disruptor.vertical_sensitivity", "Vertical sensitivity", 80,
           "How far the view tilts for a movement of the mouse", 5, 2000, Unit::thousandths,
           saved<&Settings::has_vertical_sensitivity>, thousandths<&Settings::vertical_sensitivity>,
           set_speed<&Settings::has_vertical_sensitivity, &Settings::vertical_sensitivity>, needs_separate),
    toggle<&Settings::has_invert_vertical, &Settings::invert_vertical>(
        "disruptor.invert_vertical", "Invert vertical mouse", 0,
        "Looks down when the mouse moves forward", needs_vertical),
    slider("controller.deadzone", "Stick dead zone", 37,
           "How far the stick moves before it counts\n"
           "Raise it if you walk or turn without touching the stick", 0, 90,
           Unit::percent, saved<&Settings::has_deadzone>, deadzone_percent, set_deadzone_percent),

    slider("audio.master_volume", "Master volume", 100,
           "The loudness of everything the game plays", 0, 100, Unit::percent,
           saved<&Settings::has_master_volume>, read<&Settings::master_volume>,
           write<&Settings::has_master_volume, &Settings::master_volume>),
    choice("disruptor.bass_cut", "Bass", 0,
           "How much deep bass the game's sound keeps, music and movies too\n"
           "Original: as the game plays it\n"
           "Reduced: the deepest rumble is softer\n"
           "TV speaker: as a television's own speakers played it",
           bass_cuts, saved<&Settings::has_bass_cut>, read<&Settings::bass_cut>,
           write<&Settings::has_bass_cut, &Settings::bass_cut>),
    toggle<&Settings::has_mute_in_background, &Settings::mute_in_background>(
        "disruptor.mute_in_background", "Mute in the background", 0,
        "Silences the game while its window is not in front"),
    toggle<&Settings::has_audio_muted, &Settings::audio_muted>("audio.muted", "Mute all audio", 0,
        "Silences the game and keeps the volume setting"),
    toggle<&Settings::has_detailed_logging, &Settings::detailed_logging>(
        "disruptor.detailed_logging", "Detailed logging", 0,
        "Writes more into the game's log for a bug report\n"
        "Frame timing, menu changes, controllers and the graphics driver's complaints\n"
        "Sound output and movie timing\n"
        "The driver's part starts with the next launch"),

    choice("disruptor.multisampling", "Anti-aliasing (MSAA / SSAA)", 0,
           "MSAA: smooths the edges of polygons and of the HUD\n"
           "SSAA: also smooths textures and cut-out outlines\nIt shades that many times over and needs OpenGL 4.0\n"
           "Video memory grows with the number",
           antialiasing_levels,
           antialiasing_saved, antialiasing, set_antialiasing, needs_geometry),
    choice("disruptor.draw_distance", "Draw distance", 0,
           "How far off walls, enemies and pickups are drawn\n"
           "Original: as on the PlayStation\n"
           "1.25x: a quarter further",
           draw_distances, saved<&Settings::has_draw_distance>, draw_distance, set_draw_distance, {}, distances_offered),
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
    return std::all_of(option.needs.begin(), option.needs.end(), [&](const char* key) {
        const Option* needed = find_option(key);
        return needed && on(*needed) && applies(*needed, on);
    });
}

int stored(const Settings& settings, const Option& option) {
    if (!option.saved(settings)) return option.initial;
    const int value = option.get(settings);
    return option.kind == Kind::choice && value > option.highest ? option.initial : value;
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
