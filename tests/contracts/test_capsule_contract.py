#!/usr/bin/env python3
"""Source contract for the replay capsule: where the game calls it, and what its glue promises."""

from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


host = read("psxrecomp/runtime/src/main.cpp")
game_side = read("src/game/host.cpp")
capsule = read("src/game/session/capsule.cpp")
mouse = read("src/game/input/mouse_aim.cpp")
menu = menu_text(ROOT)
cmake = read("CMakeLists.txt")

offline = host.find("    if (psx_netplay_active()) {\n        psx_netplay_finish_frame();\n    } else {\n")
before = host.find(
    "#ifdef PSX_HAS_GAME_HOST\n        psx_host_set_ui_capture(psx_host_ui_game_input_captured());\n"
    "        psx_game_before_input();\n#endif\n        mod_controller_source_begin_frame();\n"
)
after = host.find(
    "            env_pad_hold_apply();\n        }\n#ifdef PSX_HAS_GAME_HOST\n        psx_game_after_input();\n#endif\n"
)
require(
    0 <= offline < before < after
    and host.find("\n    }\n", offline + 80) > after
    and host.count("psx_game_before_input();") == host.count("psx_game_after_input();") == 1
    and "void psx_game_before_input(void) { disruptor_capsule_before_input(); }\n" in game_side
    and "void psx_game_after_input(void) {\n    disruptor_capsule_after_input();\n    disruptor_selection_wheel_guard_input();\n"
    "    disruptor_achievements_frame();\n    disruptor_state_screen_frame();\n}\n"
    in game_side
    and game_side.count("disruptor_capsule_before_input();") == game_side.count("disruptor_capsule_after_input();") == 1
    and game_side.count("disruptor_capsule_state_loaded();") == 1
    and "disruptor_capsule_" not in host,
    "the capsule is called once on either side of the pad sampling of every VBlank, and never in a network game",
)
sampling = after
require(
    0 <= sampling < host.index("\n    mod_call_frame_hooks();\n")
    and sampling < host.index("    if (g_headless || module_fast_forward) {\n#ifndef PSX_NO_DEBUG_TOOLS\n"),
    "a replay must put its frame in before the frame loop's early ways out: module hooks never run without a window",
)
require(
    "#ifdef PSX_HAS_GAME_HOST\n    psx_game_state_loaded();\n#endif\n"
    in host[host.index('extern "C" void psx_frontend_on_savestate_loaded(void) {\n') :][:1200].split("\n}\n")[0]
    and "void psx_game_state_loaded(void) {\n    disruptor_capsule_state_loaded();\n" in game_side,
    "the capsule must hear of a loaded state before the game runs on from it",
)
require(
    "    if (g_capsule.replay != Replay::kLoading) return;\n    g_capsule.loaded = true;\n" in capsule
    and "    interrupts_advance_cycles(left - static_cast<uint32_t>(first - now));\n" in capsule
    and "    if (left == 0u || first <= now || first - now > kVBlankCycles) {" in capsule
    and "constexpr uint32_t kVBlankCycles = 564480u;" in capsule
    and "#define PSX_VBLANK_CYCLES_NTSC   564480u" in read("psxrecomp/runtime/include/psx_video_timing.h"),
    "a replay must put the first VBlank at the recording's cycle, with the framework's own VBlank period",
)
require(
    '        capsule.recording = Recording::kOn;\n        say("REC");\n        capsule.first_cycle = psx_get_cycle_count();\n'
    "        gl_renderer_shown_log_begin(capsule.first_cycle, kVBlankCycles);\n        capsule.shown_base = gl_renderer_shown_captures();\n"
    "        open_frame();\n"
    in capsule
    and "    capsule.open.pad[0] = sio_get_pad_buttons_slot(0);\n    capsule.open.pad[1] = sio_get_pad_buttons_slot(1);\n"
    in capsule
    and "    last.memory = memory_now();\n    capsule.recorded.push_back(last);\n" in capsule,
    "a recording takes the pads as the last VBlank left them, and ends with the memory its last input led to",
)
require(
    "    g_capsule.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);\n" in capsule
    and "    return std::chrono::steady_clock::now() < g_capsule.notice_until ? g_capsule.notice : nullptr;\n"
    in capsule
    and '    say(stored ? "REC saved" : "REC NOT saved");\n' in capsule
    and '    g_capsule.recording = Recording::kOff;\n    say("REC refused");\n' in capsule
    and "        const bool pointing = disruptor_menu_pointing() != 0 || password_hint_shown() || state_browser_shown();\n"
    "        const bool crosshair = disruptor_mouse_crosshair_shown() != 0;\n"
    "        return disruptor_capsule_notice() || achievements || pointing || crosshair ? PSX_HOST_UI_VISIBLE | PSX_HOST_UI_LAYER : 0u;\n"
    in menu
    and "    if (!g_menu.open) {\n"
    "        if (initialize_imgui()) render_notices(disruptor_capsule_notice(), width, height);\n        return;\n    }\n"
    in menu
    and "imgui_sdl_new_frame" not in menu[menu.index("void render_notices(") : menu.index("void render_gl(")]
    and "ImGuiWindowFlags_NoInputs" in menu[menu.index("void render_notices(") : menu.index("void render_gl(")],
    "a recording shows a word for one second when it starts, ends or is refused, takes no input for it, "
    "and never holds the in-between frames off for longer",
)
replay = capsule[capsule.index("void replay_frame() {") : capsule.index('extern "C" void disruptor_capsule_note_byte(')]
require(
    replay.index("capsule.seen.push_back(memory_now());") < replay.index("    psx_game_pad_word(0, frame.pad[0]);")
    and replay.index("if (++capsule.at == capsule.replayed.size()) finish_replay();")
    < replay.index("    psx_game_pad_word(0, frame.pad[0]);")
    and "        psx_host_write_byte(frame.byte_at, frame.byte);\n        disruptor_capsule_note_byte(frame.byte_at, frame.byte);"
    in replay
    and "    disruptor_mouse_set_vertical_pitch(static_cast<double>(frame.pitch));\n" in replay,
    "a replay compares the memory before it puts a frame's input in, puts in all a frame holds, "
    "and notes the byte it writes for a capsule recorded inside it",
)
require(
    "        psx_host_write_byte(frame.byte2_at, frame.byte2);\n        disruptor_capsule_note_byte(frame.byte2_at, frame.byte2);"
    in replay,
    "a replay must also restore weapon selection when the same frame holds a mouse yaw write",
)
require(
    "    const disruptor::capsule::Verdict verdict = disruptor::capsule::judge(capsule.replayed, capsule.seen);\n"
    "    const bool in_step = verdict.in_step;\n"
    in capsule
    and "    verdict.in_step = verdict.same != 0 && verdict.last_same + kInStep > last_compared;\n" in capsule
    and "inline constexpr size_t kInStep = 60;\n" in read("src/game/session/capsule.h")
    and "constexpr size_t kSettle = 120, kLongest = 60 * 60 * 10;" in capsule,
    "a replay's exit code must say whether it still had the recording's memory in its last second",
)
require(
    "    if (capsule.recording != Recording::kOff || capsule.replay == Replay::kAsked || capsule.replay == Replay::kLoading || !capsule.loading.empty() ||\n"
    "        savestate_pending())\n        return;\n"
    in capsule
    and "    if (psx_netplay_active()) return refuse_recording(" in capsule
    and capsule.index("    if (psx_netplay_active()) return refuse_recording(")
    < capsule.index("turn_states_to(capsule.made)")
    and "    if (!capsule.from_boot && !savestate_request_save(0)) {\n        cdrom_savestate_boost_hold(0);\n        turn_states_back();\n"
    in capsule
    and "        if (!capsule.from_boot) {\n            const bool saved = savestate_slot_exists(0) != 0;\n            turn_states_back();\n            if (!saved) {\n"
    in capsule
    and capsule.count("turn_states_back()") == 6
    and "        made = fs::create_directory(capsule.made, failed);\n" in capsule
    and "    if (failed || !made || (!capsule.from_boot && !turn_states_to(capsule.made)))\n        return refuse_recording("
    in capsule
    and "    if (capsule.replay == Replay::kAsked && capsule.from_boot) {\n        if (psx_get_cycle_count() < capsule.played_first_cycle) return;\n"
    in capsule,
    "the save state folder is the capsule's for one save or load only, and is turned back whatever came of it",
)
require(
    "    psx_host_write_byte(kPlayerYawAddress, new_yaw);\n    disruptor_capsule_note_byte(kPlayerYawAddress, new_yaw);\n"
    in mouse
    and 'extern "C" void disruptor_mouse_set_vertical_pitch(double pitch) {\n    load_configuration();\n'
    "    g_mouse.vertical_pitch = pitch;\n"
    "    if (g_mouse.vertical_look_enabled) disruptor_vertical_camera_set_requested_pitch(pitch);\n}\n"
    in mouse,
    "the heading the mouse writes into the game must be told to a recording, and a replay must put the pitch back into the camera",
)
require(
    "    if (!g_menu.open && key_down(event, SDL_SCANCODE_F12)) {\n"
    "        const int held = key_held(event);\n"
    "        disruptor_capsule_key(event->key.repeat ? 1 : 0, (held & KMOD_CTRL) != 0, (held & KMOD_SHIFT) != 0);\n"
    "        return 1;\n"
    "    }\n"
    in menu
    and "    for (int key = SDL_SCANCODE_F1; !g_menu.open && key <= SDL_SCANCODE_F11; ++key)\n"
    "        if (key_down(event, static_cast<SDL_Scancode>(key)) && disruptor_capsule_holds_states()) return 1;\n"
    in menu
    and "    capsule.states_turned = true;\n    return true;\n" in capsule
    and "    savestate_configure(g_capsule.states_before.c_str(), g_capsule.bios, g_capsule.entry, nullptr, 0u);\n    g_capsule.states_turned = false;\n"
    in capsule
    and 'extern "C" int disruptor_capsule_holds_states(void) { return g_capsule.states_turned ? 1 : 0; }\n' in capsule
    and "bool key_down(const SDL_Event *event, SDL_Scancode scancode) {\n    if (!event || event->type != SDL_KEYDOWN) return false;\n"
    in menu
    and "    switch (disruptor::capsule::press(repeat != 0, ctrl != 0, shift != 0, g_capsule.recording != Recording::kOff)) {\n"
    in capsule
    and "    if (repeat || (shift && !ctrl)) return Press::kNothing;\n" in capsule
    and "    case disruptor::capsule::Press::kRecord:\n        disruptor_capsule_toggle();\n        break;\n" in capsule
    and "    return ctrl || recording ? Press::kRecord : Press::kLoad;\n" in capsule
    and "int key_held(const SDL_Event *event) {\n#if defined(PSX_SDL3)\n    return event->key.mod;\n#else\n    return event->key.keysym.mod;\n#endif\n}\n"
    in menu
    and menu.count("KMOD_") == 4
    and "    case SDL_SCANCODE_KP_ENTER: return (held & KMOD_SHIFT) != 0 ? browser::Key::save : browser::Key::choose;\n"
    in menu
    and "    if (!g_menu.open && key_down(event, SDL_SCANCODE_F11) && (key_held(event) & KMOD_CTRL) != 0 &&\n"
    "        disruptor_capsule_mark(event->key.repeat ? 1 : 0))\n        return 1;\n"
    in menu
    and "    if (capsule.recording == Recording::kSaving) capsule.end_asked = true;\n"
    "    if (capsule.recording != Recording::kOff ||"
    in capsule
    and "    capsule.recorded.clear();\n    capsule.end_asked = false;\n    capsule.recording = Recording::kSaving;\n"
    in capsule
    and '    case Recording::kOn:\n        if (capsule.end_asked) {\n            finish_recording("the key");\n            break;\n        }\n'
    in capsule
    and "static int host_ui_sink(const void *event, void *) {\n    return psx_host_ui_handle_event(static_cast<const SDL_Event *>(event));\n}\n"
    in host
    and host.index("        if (psx_host_input_sink_dispatch(&ev)) continue;\n")
    < host.index("            if (key >= SDLK_F1 && key <= SDLK_F12) {"),
    "every F12 press outside the menu and the repeats of the held key are the capsule's and never reach the save state keys: the frontend "
    "loads a state on a repeat too. Ctrl, read from the event itself, records, a press without it loads unless a recording runs, and "
    "Shift alone does nothing. While the slot folder is a capsule's the save state keys are held back. A press while the recording's "
    "state is still being saved ends the recording at its first frame",
)
require(
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/capsule.cpp"\n' in cmake
    and "    PSX_HAS_GAME_HOST=1" in cmake
    and '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/host.cpp"\n' in cmake
    and '#define DISRUPTOR_CAPSULE_NO_HOST 1\n#include "../../src/game/session/capsule.cpp"\n'
    in read("tests/unit/disruptor_capsule_test.cpp")
    and "    disruptor_unit_test(disruptor_capsule " in cmake
    and "    disruptor_contract(capsule)\n" in cmake,
    "the capsule and its tests must remain built and registered",
)
require(
    "    out.close();\n    return !out.fail();\n" in capsule
    and '    const bool stored = write(capsule.made / "capsule.toml", note) && input && settings && lists;\n' in capsule
    and "    std::_Exit(stored ? 0 : 2);\n" in capsule
    and "    if (g_capsule.scripted == 0) return;\n    std::fflush(nullptr);\n    std::_Exit(2);" in capsule
    and "    std::_Exit(!in_step ? 3 : capsule.unwritten != 0 ? 2 : !capsule.presented_whole ? 4 : 0);" in capsule
    and "        capsule.presented_whole = late == 0 && timeouts == 0;\n" in capsule
    and "    const bool settings = capsule.settings_kept;\n" in capsule,
    "a capsule that was not written whole must not be called recorded, and a script must never be left waiting",
)
require(
    "    if (g_capsule.recording == Recording::kOn) {" in capsule
    and '        else store_recording("a state was loaded");\n' in capsule
    and capsule.index("    if (g_capsule.recording == Recording::kOn) {")
    < capsule.index("    if (g_capsule.replay != Replay::kLoading) return;"),
    "a recording must end when another state is loaded: a capsule holds one state",
)
guide = read("docs/BUILD.md")
require(
    "Ctrl+F12 in the game starts a recording" in guide
    and "tools/replay_capsule.py" in guide
    and "PSX_DISRUPTOR_CAPSULE_REPLAY" in capsule
    and "| Save / load state | Shift+F1–F11 / F1–F11 |" in read("docs/PLAYING.md")
    and "| Start / end a replay capsule | Ctrl+F12 |\n| Load the newest replay capsule | F12 |\n| Mark the moment in a capsule | Ctrl+F11 |"
    in read("docs/PLAYING.md")
    and "Ctrl+F11 while recording marks the moment" in guide
    and "`PSX_DISRUPTOR_CAPSULE_AS_SHOWN=1`" in guide,
    "the build guide must say how a capsule is made and replayed, and the player's guide gives Ctrl+F12 to recording, F12 to "
    "loading the newest capsule and F1 to F11 to the save states",
)
require(
    "    const bool free_now = capsule.recording == Recording::kOff && capsule.replay == Replay::kOff && capsule.loading.empty() && !savestate_pending() &&\n"
    "                          !psx_netplay_active();\n"
    "    if (!free_now || !stored_name(name, began) || fs::is_symlink(folder, failed) || failed || !fs::is_directory(folder, failed) || failed ||\n"
    "        !begins(folder, pitch) || !turn_states_to(folder)) {\n"
    in capsule
    and '    if (noted == 0 || !decode(whole_file(folder / "input.bin", kInputBytes), frames, first_cycle) || frames.size() != noted) return false;\n'
    in capsule
    and "    if (capsule.load_done) disruptor_mouse_set_vertical_pitch(capsule.load_pitch);\n" in capsule
    and "    if (!savestate_slot_exists(0) || !savestate_request_load(0)) {\n        turn_states_back();\n" in capsule
    and "    capsule.loading = folder;\n    capsule.load_pitch = pitch;\n    capsule.load_done = false;\n    return true;\n"
    in capsule
    and "    if (!g_capsule.loading.empty()) g_capsule.load_done = true;\n" in capsule
    and "    if (!capsule.loading.empty() && !savestate_pending()) finish_load();\n" in capsule
    and "static void finish_load() {\n    Capsule &capsule = g_capsule;\n    turn_states_back();\n" in capsule
    and "    capsule.loading.clear();\n}\n" in capsule
    and "capsule.replay == Replay::kLoading || !capsule.loading.empty() ||\n        savestate_pending())\n        return;\n"
    in capsule
    and "        if (const std::string name = disruptor::capsule::newest(disruptor::capsule::stored(disruptor::capsule::home())); name.empty()) "
    'say("no capsule");\n        else disruptor::capsule::load(name);\n'
    in capsule
    and "        if (kept.name != recording && kept.frames != 0) {\n"
    '            if (ImGui::SmallButton("Load")) {\n'
    "                if (capsule::load(kept.name)) set_menu_open(false);\n"
    in menu,
    "a capsule's state is loaded only when nothing records, replays or loads, only from a folder named as a capsule, never a link, whose "
    "note and input are whole, the slot folder goes back whatever came of it, no recording starts meanwhile, and both the key and the "
    "list's button load through it",
)

playing = " ".join(read("docs/PLAYING.md").split())  # the guide is wrapped where it reads best
require(
    "    if (!digits(second) || second.size() > 18 || second[0] == '0') return false;\n" in capsule
    and "    if (!stored_name(name, began)) return false;\n" in capsule
    and "    if (std::filesystem::is_symlink(folder, failed) || failed || !std::filesystem::is_directory(folder, failed) || failed) return false;\n"
    "    std::filesystem::remove_all(folder, failed);\n    return !failed;\n"
    in capsule
    and "    const fs::path all = disruptor::capsule::home();\n    fs::create_directories(all, failed);\n" in capsule
    and "    return g_capsule.recording == Recording::kOff ? std::string() : utf8(g_capsule.made.filename());\n"
    in capsule,
    "only a folder named as a capsule is listed or removed, never through a link, the list reads the folder recordings go into, "
    "and the capsule being recorded is known by its name",
)
require(
    "    if (g_menu.capsules_read < 0.0 || ImGui::GetTime() - g_menu.capsules_read > 1.0) {\n" in menu
    and '        if (kept.name != recording && ImGui::SmallButton(asked ? "Delete for good?" : "Delete")) {\n'
    "            if (asked) {\n"
    in menu
    and "            g_menu.capsule_asked = asked ? std::string() : kept.name;\n" in menu
    and "    ImGui::Separator();\n    draw_capsules();\n" in menu
    and "**Settings → Debug** lists them, the newest on top," in playing,
    "the Debug tab lists the capsules, read once a second, deletes one only on a second press and never the one being recorded, "
    "and the player's guide says the list is there",
)

renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
require(
    "    if (s_snap_state == SNAP_NONE || s_snap_state == SNAP_WAITING) {\n        s_snap_width = width;\n        s_snap_state = SNAP_ASKED;\n"
    in renderer
    and "    if (s_snap_state == SNAP_READING) s_snap_dropped = 1;\n    else s_snap_state = SNAP_NONE;\n" in renderer
    and "    if (s_snap_state == SNAP_WAITING) {\n        s_snap_state = SNAP_NONE;\n        if (s_snap_w * s_snap_h * 3 <= room) {\n"
    "            memcpy(rgb, s_snap_pixels, (size_t)s_snap_w * s_snap_h * 3);\n"
    in renderer
    and "    mine = s_snap_state == SNAP_ASKED;\n    if (mine) s_snap_state = SNAP_READING;\n    width = s_snap_width;\n    SDL_AtomicUnlock(&s_snap_lock);\n"
    "    if (!mine) return;\n"
    in renderer
    and "        for (int y = 0; y < height; ++y) memcpy(down + (size_t)y * width * 3, up + (size_t)(height - 1 - y) * width * 3, (size_t)width * 3);\n"
    in renderer
    and "    s_snap_state = down && !s_snap_dropped ? SNAP_WAITING : SNAP_NONE;\n    s_snap_dropped = 0;\n    SDL_AtomicUnlock(&s_snap_lock);\n"
    in renderer
    and renderer.count("SDL_AtomicLock(&s_snap_lock);") == 6
    and renderer.count("SDL_AtomicUnlock(&s_snap_lock);") == 6
    and "static void shown_capture(int kind, float alpha) {\n    int ww = 0, wh = 0, sequence = -1;\n    gl_visible_size(&ww, &wh);\n"
    "    if (SDL_AtomicGet(&s_shown_log_on)) {"
    in renderer
    and "        SDL_AtomicUnlock(&s_shown_log_lock);\n    }\n    shown_snap(sequence);\n    shown_count(kind);\n"
    in renderer
    and "static void shown_keep(GLuint source, int x, int y, int ww, int wh, int kind, float alpha) {\n"
    "    if (s_shown_left <= 0) return;\n"
    in renderer
    and "    if (!rgb || width <= 0 || height <= 0 || width > 4096 || height > 4096) return 0;\n" in renderer
    and "    if (texture) glDeleteTextures(1, &texture);\n" in renderer,
    "the renderer takes one picture of what reaches the screen when asked, one thread at a time and under a lock, copies it out "
    "rows top down, drops one that was cancelled, and makes a picture for the menu in the context that asks",
)
require(
    "    if (!capsule.preview_asked || !gl_renderer_snap_taken(capsule.preview.data(), static_cast<int>(capsule.preview.size()), &width, &height)) return;\n"
    "    capsule.preview_asked = false;\n"
    in capsule
    and "    if (write(capsule.made / disruptor::capsule::preview_name(disruptor::capsule::preview_slot(capsule.previews)),\n"
    in capsule
    and "        ++capsule.previews;\n" in capsule
    and "    keep_mark();\n    if (!capsule.mark_asked) keep_preview();\n    gl_renderer_snap_cancel();\n"
    "    capsule.preview_asked = capsule.mark_asked = false;\n"
    in capsule
    and "        gl_renderer_snap_cancel();\n        capsule.previews = 0;\n        capsule.preview_asked = capsule.mark_asked = false;\n"
    in capsule
    and "        keep_mark();\n        if (!capsule.mark_asked) keep_preview();\n"
    "        if (!capsule.mark_asked && disruptor::capsule::preview_due(capsule.recorded.size()) &&\n"
    "            gl_renderer_snap_shown(static_cast<int>(disruptor::capsule::kPreviewWidth)))\n"
    "            capsule.preview_asked = true;\n"
    "        close_frame();\n"
    in capsule
    and "inline constexpr uint32_t kPreviewWidth = 240, kPreviewFirst = 60, kPreviewEvery = 30, kPreviewMost = 80;\n"
    in read("src/game/session/capsule.h"),
    "a recording asks for a picture every half second from its first second on, keeps each under the next number and the eightieth "
    "again, and cancels the one it waits for when it starts and when it ends",
)
require(
    "        if (each->is_symlink(unread) || unread || !each->is_directory(unread) || unread || !stored_name(one.name, one.began)) continue;\n"
    in capsule
    and "        if (std::filesystem::is_regular_file(note, unread)) about.open(note, std::ios::binary);\n" in capsule
    and "    if (!std::filesystem::is_regular_file(file, failed) || failed) return {};\n" in capsule
    and "    if (failed || size == 0 || size > most) return {};\n" in capsule
    and "std::string preview_read(const std::filesystem::path &file) { return whole_file(file, kPreviewBytes); }\n"
    in capsule,
    "a linked folder is no capsule, and a capsule's note and pictures are read only from regular files of a bounded size",
)
require(
    "        if (thumb.pictures != kept.pictures || thumb.revised != kept.revised) {\n            gl_renderer_picture_free(thumb.picture);\n"
    in menu
    and "            thumb.picture = kept.pictures ? load_picture(all / kept.name / capsule::preview_name(kept.pictures - 1), &thumb.tall) : 0;\n"
    in menu
    and "        if (thumb.picture && ImGui::IsItemHovered()) draw_film(all / kept.name, kept, thumb.tall);\n" in menu
    and "    if (g_menu.capsule_film_of != kept.name || g_menu.capsule_film_pictures != kept.pictures || g_menu.capsule_film_revised != kept.revised) {\n"
    "        drop_film();\n"
    in menu
    and "    for (const unsigned picture : g_menu.capsule_film) gl_renderer_picture_free(picture);\n" in menu
    and "[](const capsule::Stored &kept) { return kept.name == g_menu.capsule_film_of; })) drop_film();\n" in menu
    and "            if (!listed) gl_renderer_picture_free(thumb->second.picture);\n" in menu
    and "    const std::string bytes = disruptor::capsule::preview_read(file);\n" in menu
    and "    if (!disruptor::capsule::preview_decode(bytes, width, height, rgb)) return 0;\n" in menu
    and '        else ImGui::Text("%u s", capsule::stored_seconds(kept.frames));\n' in menu
    and "with the last picture, time and length of each" in playing
    and "Hold the mouse over a picture to see that capsule play." in playing,
    "the list shows each capsule's last picture and loads it again when it is replaced, plays its pictures under the mouse, frees "
    "what it no longer shows, takes no file that is not a whole picture, and the player's guide says so",
)

temporal_thread = read("psxrecomp/runtime/src/gpu_gl_render_thread.c.inc")
require(
    "        const uint64_t cycle = kind ? s_interp_cur_cycle : rth_replaying() ? s_rthx_cycle : psx_get_cycle_count();\n"
    "        SDL_AtomicLock(&s_shown_log_lock);\n        if (s_shown_log.on)\n"
    "            sequence = (int)psx_shown_log_add(&s_shown_log, psx_shown_frame_of(cycle, s_shown_log_first, s_shown_log_period), kind, alpha, ww, wh);\n"
    in renderer
    and "    s_interp_cur_cycle = cycle;\n    s_interp_captures++;\n}\n" in renderer
    and "    const uint64_t cycle = rth_replaying() ? s_rthx_cycle : psx_get_cycle_count();\n" in renderer
    and renderer.count("        const uint64_t cycle = psx_get_cycle_count();\n") == 2
    and "        if (rth_rec_call(RTH_PRESENT_VRAM, v, 12)) return;\n" in renderer
    and "        if (rth_rec_call(RTH_PRESENT_WIDE, v, 14)) return;\n" in renderer
    and "        s_rthx_cycle = (uint64_t)(uint32_t)v[11] | (uint64_t)(uint32_t)v[12] << 32;\n" in temporal_thread
    and "        s_rthx_cycle = (uint64_t)(uint32_t)v[10] | (uint64_t)(uint32_t)v[11] << 32;\n" in temporal_thread
    and "    job.cycle = cycle;\n" in renderer
    and "job->presented, job->cycle);\n" in renderer,
    "every game frame reaches the presenter with the emulated clock of its present, whichever thread draws it, and each "
    "picture shown while a capsule records is logged under the VBlank of the later frame it is placed before",
)
require(
    "        const int step = psx_shown_script_step(&s_script, psx_shown_frame_of(s_interp_cur_cycle, s_script.first, s_script.period),\n"
    "                                               s_interp_valid >= 2, &scripted);\n"
    in renderer
    and "    double a = scripted >= 0.0f ? (double)scripted : (double)(now - s_interp_start) / (double)s_interp_duration;\n"
    in renderer
    and renderer.index("    s_script_index = scripted >= 0.0f ? (int)s_script.pictures[s_script.next].sequence : -1;\n")
    < renderer.index("    shown_capture(1, shown);\n")
    < renderer.index("        psx_shown_script_presented(&s_script);\n        SDL_CondBroadcast(s_script_cond);\n")
    and "    SDL_LockMutex(s_interp_mutex);\n    interp_script_wait();\n    if (job->generation == s_temp_generation) {\n"
    in renderer
    and "    const uint64_t locked = SDL_GetPerformanceCounter();\n    interp_script_wait();  /* before any history this frame resets or replaces */\n"
    in renderer
    and "            s_script.timeouts++;\n" in renderer
    and "    s_script_index = scripted >= 0.0f ? (int)s_script.pictures[s_script.next].sequence : -1;\n" in renderer
    and "        int uncapped = hz < 0.0 || scripted;" in renderer,
    "a replay as shown presents each recorded picture between the same two game frames at the same place, and no game "
    "frame's scene is handed over before the pictures of the one before are shown",
)
require(
    "        capsule.first_cycle = psx_get_cycle_count();\n        gl_renderer_shown_log_begin(capsule.first_cycle, kVBlankCycles);\n"
    in capsule
    and "            if (!gl_renderer_shown_script(script.data(), static_cast<int>(script.size()), capsule.played_first_cycle, kVBlankCycles)) {\n"
    in capsule
    and "    capsule.open.residual = capsule.notes.residual;\n" in capsule
    and "    take_shown();\n    capsule.recorded.push_back(capsule.open);\n" in capsule
    and "    gl_renderer_shown_log_end();\n    take_shown();\n" in capsule
    and "    if (!gl_renderer_snap_shown(kMarkWidth)) return 1;\n" in capsule
    and capsule.index("    if (!write(capsule.made / disruptor::capsule::mark_name(")
    < capsule.index("    mark.picture = gl_renderer_snap_taken_sequence();\n")
    and capsule.count('        say("MARK NOT saved");\n') == 2
    and "        if (++capsule.mark_waited < kMarkWait) return;\n" in capsule
    and "    *yaw = static_cast<double>(capsule.replayed[capsule.at - 1].residual);\n" in capsule
    and "    if (width < 64 || width > 3840) return 0;\n" in renderer
    and "    s_snap_sequence = sequence;\n" in renderer
    and "            s_snap_taken_sequence = s_snap_sequence;\n" in renderer,
    "a recording keeps every picture shown from its first VBlank on, the turn's remainder each frame and a full picture for "
    "each mark with the number of the picture it was read from, and a replay hands the remainder back",
)

cdrom = read("psxrecomp/runtime/src/cdrom.c")
require(
    capsule.index("    if (!capsule.from_boot) cdrom_savestate_boost_hold(1);")
    < capsule.index(
        "    if (!capsule.from_boot && !savestate_request_save(0)) {\n        cdrom_savestate_boost_hold(0);\n"
    )
    and "    case Recording::kSaving:\n        if (savestate_pending()) break;\n        cdrom_savestate_boost_hold(0);\n"
    in capsule
    and "    if (capsule.replay == Replay::kAsked) {\n        cdrom_savestate_boost_hold(1);\n"
    "        if (!turn_states_to(capsule.played) || !savestate_request_load(0)) {\n"
    in capsule
    and "    if (capsule.replay == Replay::kLoading) {\n        if (savestate_pending()) return;\n"
    "        cdrom_savestate_boost_hold(0);\n"
    in capsule
    and "    if (s_savestate_cd_boost_held) return;\n    s_savestate_cd_boost_vblanks = CDROM_SAVESTATE_BOOST_VBLANKS;\n"
    in cdrom
    and "    s_savestate_cd_boost_held = held != 0;\n    if (held) s_savestate_cd_boost_vblanks = 0;\n" in cdrom,
    "a capsule's state is saved with no post-load drive speed-up running and its replay loads it without one, so the "
    "replay's drive delivers its sectors on the recording's cycles",
)

require(
    'extern "C" void disruptor_capsule_after_input(void) {\n    Capsule &capsule = g_capsule;\n'
    "    if (capsule.replay != Replay::kOff && !capsule.aspect_applied) apply_played_aspect();\n"
    "    if (!capsule.loading.empty() && !savestate_pending()) finish_load();\n"
    in capsule
    and "    if (!psx_mod_set_fixed_display_aspect(numerator, denominator)) {\n" in capsule
    and "    gl_renderer_set_display_aspect(static_cast<int>(numerator), static_cast<int>(denominator));\n}\n"
    in capsule
    and "kRamBytes / disruptor::capsule::kPageBytes,\n" in capsule
    and "            psx_host_video_get_applied_display_aspect(&numerator, &denominator);\n" in capsule
    and capsule.index("            psx_host_video_get_applied_display_aspect(&numerator, &denominator);\n")
    < capsule.index("        open_frame();\n        if (disruptor_baton_held_by_choice())")
    and '        note += "aspect = \\"" + std::to_string(capsule.aspect[0]) + ":" + std::to_string(capsule.aspect[1]) + "\\"\\n";\n'
    in capsule
    and '    const bool stored = write(capsule.made / "capsule.toml", note) && input && settings && lists;\n' in capsule
    and "(capsule.pages.empty() || write(capsule.made / disruptor::capsule::kPagesFile, "
    "disruptor::capsule::pages_encode(capsule.pages)));\n"
    in capsule
    and "    if (g_capsule.recorded.size() < disruptor::capsule::kPagedFrames)\n"
    "        g_capsule.pages.push_back(disruptor::capsule::page_digests(memory_get_ram_ptr(), kRamBytes));\n"
    in capsule
    and capsule.count("    capsule.pages.clear();\n") == 2
    and "    } else if (frame.memory != 0u && !capsule.pages_told && capsule.at < capsule.played_pages.size()) {\n"
    in capsule,
    "a recording keeps the aspect it was shown at and its first frames' memory by page, and a replay shows that aspect "
    "before its state loads and names the pages of a compared first frame that is apart",
)

print("Disruptor replay capsule source contract: PASS")
