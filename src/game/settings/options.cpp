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
int vsync(const Settings& settings) { return std::clamp(settings.vsync + 1, 0, 2); }
void set_vsync(Settings& settings, int index) { settings.has_vsync = true; settings.vsync = index - 1; }

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
constexpr const char* shadows[] = {"Vanilla", "Improved"};

constexpr const char* geometry_key = "disruptor.geometry_correction";
constexpr const char* frames_key = "video.frame_interpolation";
constexpr const char* rate_key = "video.frame_interpolation_fps";
constexpr const char* occlusion_key = "disruptor.ambient_occlusion";
constexpr const char* aim_key = "disruptor.mouse_aim";
constexpr const char* needs_geometry[] = {geometry_key};
constexpr const char* needs_frames[] = {frames_key};
constexpr const char* needs_occlusion[] = {occlusion_key};
constexpr const char* needs_aim_and_geometry[] = {aim_key, geometry_key};

template <auto Has, auto Value>
constexpr Option toggle(const char* key, const char* label, int initial, const char* hint,
                        std::span<const char* const> needs = {}) {
    return {key, Kind::toggle, label, saved<Has>, read<Value>, write<Has, Value>, initial, 0, 1, {}, needs,
            Unit::none, hint};
}

constexpr Option choice(const char* key, const char* label, int initial, const char* hint,
                        std::span<const char* const> choices, Saved saved, Get get, Set set) {
    return {key, Kind::choice, label, saved, get, set, initial, 0, static_cast<int>(choices.size()) - 1,
            choices, {}, Unit::none, hint};
}

constexpr Option slider(const char* key, const char* label, int initial, const char* hint,
                        int lowest, int highest, Unit unit, Saved saved, Get get, Set set,
                        std::span<const char* const> needs = {}) {
    return {key, Kind::slider, label, saved, get, set, initial, lowest, highest, {}, needs, unit, hint};
}

constexpr Option option_list[] = {
    choice("video.fullscreen", "Display mode", 0,
           "A window, a borderless window the size of the screen, or exclusive fullscreen. Alt+Enter "
           "switches between a window and fullscreen while playing.", display_modes,
           saved<&Settings::has_fullscreen>, read<&Settings::fullscreen>,
           write<&Settings::has_fullscreen, &Settings::fullscreen>),
    choice("video.aspect_ratio", "Aspect ratio", 0,
           "4:3 is the original picture. The wider choices show more of the world at the sides. Match "
           "window follows the window's shape, up to 32:9. Movies stay 4:3.", aspects,
           aspect_saved, aspect, set_aspect),
    choice("video.supersampling", "Internal resolution scale", 3,
           "Draws the picture at this multiple of the original resolution. Higher is sharper. What it "
           "costs the graphics card grows roughly with the square of the value.", scales,
           saved<&Settings::has_supersampling>, scale, set_scale),
    choice("video.vsync", "VSync", 2,
           "Synchronised waits for the display, so the picture never tears. Immediate does not wait. "
           "Adaptive waits only while the game keeps up with the display.",
           vsyncs, saved<&Settings::has_vsync>, vsync, set_vsync),
    slider("disruptor.hud_scale", "HUD size", 100,
           "Widescreen only. Shrinks the health and ammunition displays toward their screen edges. "
           "The weapon keeps its size.", 50, 100, Unit::percent,
           saved<&Settings::has_hud_scale>, read<&Settings::hud_scale>,
           write<&Settings::has_hud_scale, &Settings::hud_scale>),

    toggle<&Settings::has_geometry_correction, &Settings::geometry_correction>(
        "disruptor.geometry_correction", "Exact-provenance geometry", 1,
        "Places polygons from the game's own arithmetic before it is rounded to whole pixels, which "
        "steadies the PlayStation's wobbling edges. Other options need it."),
    toggle<&Settings::has_perspective_textures, &Settings::perspective_textures>(
        "disruptor.perspective_textures", "Perspective-correct world textures", 1,
        "Draws wall and floor textures in true perspective. The PlayStation's own way bends them near "
        "the viewer.", needs_geometry),
    toggle<&Settings::has_gap_filling, &Settings::gap_filling>(
        "disruptor.gap_filling", "Gap filling", 1,
        "Closes hairline gaps between polygons and keeps the game's own patches off their neighbours.", needs_geometry),
    toggle<&Settings::has_depth_sorted_world, &Settings::depth_sorted_world>(
        "disruptor.depth_sorted_world", "Depth-sorted world", 0,
        "Draws world polygons by their depth where the game's own order puts a far one over a near "
        "one.", needs_geometry),
    choice("disruptor.improved_shadows", "Shadows", 0,
           "Vanilla is the game's own shape. Improved lays a shadow flat on the floor: as wide as the "
           "game makes it and as tall as the floor's perspective gives. Improved needs exact "
           "geometry.", shadows,
           saved<&Settings::has_improved_shadows>, read<&Settings::improved_shadows>,
           write<&Settings::has_improved_shadows, &Settings::improved_shadows>),
    toggle<&Settings::has_frame_unlock, &Settings::frame_unlock>(
        "disruptor.frame_unlock", "60 FPS gameplay", 0,
        "Lets the game run at up to 60 frames a second in place of its original rate."),
    toggle<&Settings::has_frame_interpolation, &Settings::frame_interpolation>(
        "video.frame_interpolation", "In-between frames", 0,
        "Shows extra pictures between the game's frames, built from the game's own geometry, for "
        "smoother motion.", needs_geometry),
    slider("video.frame_interpolation_fps", "In-between frame rate", 0,
           "Pictures shown per second while in-between frames are on.",
           kDisruptorPresentRateLowest, kDisruptorPresentRateHighest, Unit::frames,
           saved<&Settings::has_frame_interpolation_fps>, read<&Settings::frame_interpolation_fps>,
           write<&Settings::has_frame_interpolation_fps, &Settings::frame_interpolation_fps>, needs_frames),
    toggle<&Settings::has_skip_intro, &Settings::skip_intro>(
        "disruptor.skip_intro", "Skip the logos and the title movie", 0,
        "Starts at the main menu without the logos and the title movie. Escape skips a logo or a "
        "movie either way."),

    toggle<&Settings::has_mouse_aim, &Settings::mouse_aim>(
        "disruptor.mouse_aim", "Horizontal mouse aim", 1,
        "Turns the view with the mouse. In the game a middle-click captures the mouse, and a "
        "middle-click or Escape releases it."),
    toggle<&Settings::has_modern_controls, &Settings::modern_controls>(
        "disruptor.modern_controls", "Modern keyboard and mouse controls", 1,
        "WASD moves, the left mouse button fires, the right one uses psionics, Space jumps and E "
        "interacts."),
    slider("disruptor.horizontal_sensitivity", "Horizontal sensitivity", 80,
           "How far the view turns for a movement of the mouse.", 5, 2000, Unit::thousandths,
           saved<&Settings::has_horizontal_sensitivity>, thousandths<&Settings::horizontal_sensitivity>,
           set_thousandths<&Settings::has_horizontal_sensitivity, &Settings::horizontal_sensitivity>),
    toggle<&Settings::has_invert_horizontal, &Settings::invert_horizontal>(
        "disruptor.invert_horizontal", "Invert horizontal mouse", 0,
        "Turns the view left when the mouse moves right."),
    toggle<&Settings::has_high_precision_camera, &Settings::high_precision_camera>(
        "disruptor.high_precision_camera", "Smooth mouse turning", 0,
        "The game turns in 256 steps of a circle. With this on, the picture also turns by the part of "
        "a step in between. Needs exact geometry.", needs_aim_and_geometry),
    toggle<&Settings::has_vertical_look, &Settings::vertical_look>(
        "disruptor.vertical_look", "Vertical mouse look", 0,
        "Looks up and down with the mouse, to about 31 degrees each way. The original game has no "
        "such view. Experimental."),
    slider("disruptor.vertical_sensitivity", "Vertical sensitivity", 80,
           "How far the view tilts for a movement of the mouse.", 5, 2000, Unit::thousandths,
           saved<&Settings::has_vertical_sensitivity>, thousandths<&Settings::vertical_sensitivity>,
           set_thousandths<&Settings::has_vertical_sensitivity, &Settings::vertical_sensitivity>),
    toggle<&Settings::has_invert_vertical, &Settings::invert_vertical>(
        "disruptor.invert_vertical", "Invert vertical mouse", 0,
        "Looks down when the mouse moves forward."),

    slider("audio.master_volume", "Master volume", 100,
           "The loudness of everything the game plays.", 0, 100, Unit::percent,
           saved<&Settings::has_master_volume>, read<&Settings::master_volume>,
           write<&Settings::has_master_volume, &Settings::master_volume>),
    toggle<&Settings::has_audio_muted, &Settings::audio_muted>("audio.muted", "Mute all audio", 0,
        "Silences the game and keeps the volume setting."),

    toggle<&Settings::has_ambient_occlusion, &Settings::ambient_occlusion>(
        "disruptor.ambient_occlusion", "Ambient occlusion", 0,
        "Shades corners and the floor under things. Unfinished.", needs_geometry),
    slider("disruptor.occlusion_reach", "Occlusion reach", 160,
           "How far from a corner or an object the shade spreads.", 50, 800, Unit::none,
           saved<&Settings::has_occlusion_reach>, read<&Settings::occlusion_reach>,
           write<&Settings::has_occlusion_reach, &Settings::occlusion_reach>, needs_occlusion),
    slider("disruptor.occlusion_strength", "Occlusion strength", 80,
           "How dark the shade is.", 0, 300, Unit::percent,
           saved<&Settings::has_occlusion_strength>, read<&Settings::occlusion_strength>,
           write<&Settings::has_occlusion_strength, &Settings::occlusion_strength>, needs_occlusion),
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

} // namespace disruptor::settings
