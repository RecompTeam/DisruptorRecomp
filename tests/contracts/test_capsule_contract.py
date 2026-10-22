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

require(
    "#ifdef PSX_HAS_GAME_HOST\n"
    "    if (!psx_netplay_active()) psx_game_before_input();\n"
    "#endif\n"
    "    if (psx_netplay_active()) {\n"
    "        psx_netplay_finish_frame();\n"
    "    } else if (g_headless) {\n"
    "        sample_headless_pad_into_sio(override);\n"
    "    } else {\n"
    "        sample_pad_into_sio(override);\n"
    "    }\n"
    "#ifdef PSX_HAS_GAME_HOST\n"
    "    if (!psx_netplay_active()) psx_game_after_input();\n"
    "#endif\n"
    in host
    and host.count("psx_game_before_input();") == host.count("psx_game_after_input();") == 1
    and "void psx_game_before_input(void) { disruptor_capsule_before_input(); }\n" in game_side
    and "void psx_game_after_input(void) {\n    disruptor_capsule_after_input();\n    disruptor_selection_wheel_guard_input();\n}\n"
    in game_side
    and game_side.count("disruptor_capsule_before_input();") == game_side.count("disruptor_capsule_after_input();") == 1
    and game_side.count("disruptor_capsule_state_loaded();") == 1
    and "disruptor_capsule_" not in host,
    "the capsule is called once on either side of the pad sampling of every VBlank, and never in a network game",
)
sampling = host.index("    if (!psx_netplay_active()) psx_game_after_input();")
require(
    sampling < host.index("    if (g_headless || module_fast_forward) {")
    and sampling < host.index("    mod_call_frame_hooks();"),
    "a replay must put its frame in before the frame loop's early ways out: module hooks never run without a window",
)
require(
    'extern "C" void psx_frontend_on_savestate_loaded(void) {\n'
    "#ifdef PSX_HAS_GAME_HOST\n"
    "    psx_game_state_loaded();\n"
    "#endif\n"
    in host
    and "void psx_game_state_loaded(void) {\n    disruptor_capsule_state_loaded();\n" in game_side,
    "the capsule must hear of a loaded state before the game runs on from it",
)
require(
    "    if (g_capsule.replay != Replay::kLoading) return;\n    g_capsule.loaded = true;\n" in capsule
    and "    interrupts_advance_cycles(left - static_cast<uint32_t>(first - now));\n" in capsule
    and "    if (left == 0u || first <= now || first - now > kVBlankCycles) {" in capsule
    and "constexpr uint32_t kVBlankCycles = 564480u;" in capsule
    and "#define VBLANK_CYCLES   564480u" in read("psxrecomp/runtime/src/interrupts.c"),
    "a replay must put the first VBlank at the recording's cycle, with the framework's own VBlank period",
)
require(
    '        capsule.recording = Recording::kOn;\n        say("REC");\n        capsule.first_cycle = psx_get_cycle_count();\n        open_frame();\n'
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
    and "    if (!g_menu.open || !g_menu.imgui_ready) return disruptor_capsule_notice() ? PSX_HOST_UI_VISIBLE : 0u;\n"
    in menu
    and "    if (const char *word = g_menu.open ? nullptr : disruptor_capsule_notice()) {\n"
    "        if (initialize_imgui()) render_notice(word, width, height);\n        return;\n    }\n"
    in menu
    and "imgui_sdl_new_frame" not in menu[menu.index("void render_notice(") : menu.index("void render_gl(")]
    and "ImGuiWindowFlags_NoInputs" in menu[menu.index("void render_notice(") : menu.index("void render_gl(")],
    "a recording shows a word for one second when it starts, ends or is refused, takes no input for it, "
    "and never holds the in-between frames off for longer",
)
replay = capsule[capsule.index("void replay_frame() {") : capsule.index('extern "C" void disruptor_capsule_note_byte(')]
require(
    replay.index("capsule.seen.push_back(memory_now());") < replay.index("    sio_set_pad_state_slot(0, frame.pad[0]);")
    and replay.index("if (++capsule.at == capsule.replayed.size()) finish_replay();")
    < replay.index("    sio_set_pad_state_slot(0, frame.pad[0]);")
    and "        psx_write_byte(frame.byte_at, frame.byte);\n        disruptor_capsule_note_byte(frame.byte_at, frame.byte);"
    in replay
    and "    disruptor_mouse_set_vertical_pitch(static_cast<double>(frame.pitch));\n" in replay,
    "a replay compares the memory before it puts a frame's input in, puts in all a frame holds, "
    "and notes the byte it writes for a capsule recorded inside it",
)
require(
    "        psx_write_byte(frame.byte2_at, frame.byte2);\n        disruptor_capsule_note_byte(frame.byte2_at, frame.byte2);"
    in replay,
    "a replay must also restore weapon selection when the same frame holds a mouse yaw write",
)
require(
    "    const bool in_step = capsule.same != 0 && capsule.last_same + kInStep >= capsule.replayed.size();\n" in capsule
    and "constexpr size_t kSettle = 120, kInStep = 60, kLongest = 60 * 60 * 10;" in capsule,
    "a replay's exit code must say whether it still had the recording's memory in its last second",
)
require(
    "    if (capsule.recording != Recording::kOff || capsule.replay == Replay::kAsked || capsule.replay == Replay::kLoading || !capsule.loading.empty() ||\n"
    "        savestate_pending())\n        return;\n"
    in capsule
    and "    if (psx_netplay_active()) return refuse_recording(" in capsule
    and capsule.index("    if (psx_netplay_active()) return refuse_recording(")
    < capsule.index("turn_states_to(capsule.made)")
    and "    if (!savestate_request_save(0)) {\n        turn_states_back();\n" in capsule
    and "        if (const bool saved = savestate_slot_exists(0) != 0; turn_states_back(), !saved) {\n" in capsule
    and capsule.count("turn_states_back()") == 6
    and "        made = fs::create_directory(capsule.made, failed);\n" in capsule
    and "    if (failed || !made || !turn_states_to(capsule.made)) return refuse_recording(" in capsule,
    "the save state folder is the capsule's for one save or load only, and is turned back whatever came of it",
)
require(
    "    psx_write_byte(kPlayerYawAddress, new_yaw);\n    disruptor_capsule_note_byte(kPlayerYawAddress, new_yaw);\n"
    in mouse
    and 'extern "C" void disruptor_mouse_set_vertical_pitch(double pitch) {\n    g_mouse.vertical_pitch = pitch;\n}\n'
    in mouse,
    "the heading the mouse writes into the game must be told to a recording, and a replay must be able to put the pitch back",
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
    and "    savestate_configure(g_capsule.states_before.c_str(), g_capsule.bios, g_capsule.entry);\n    g_capsule.states_turned = false;\n"
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
    and menu.count("KMOD_") == 2
    and "    if (capsule.recording == Recording::kSaving) capsule.end_asked = true;\n"
    "    if (capsule.recording != Recording::kOff ||"
    in capsule
    and "    capsule.recorded.clear();\n    capsule.end_asked = false;\n    capsule.recording = Recording::kSaving;\n"
    in capsule
    and '    case Recording::kOn:\n        if (capsule.end_asked) {\n            finish_recording("the key");\n            break;\n        }\n'
    in capsule
    and "} else if (!ui_consumed && ev.type == SDL_KEYDOWN) {" in host
    and host.index("const int ui_consumed = psx_host_ui_handle_event(&ev);")
    < host.index("if (key >= SDLK_F1 && key <= SDLK_F12) {"),
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
    and '    const bool stored = write(capsule.made / "capsule.toml", about) && input && !failed;\n' in capsule
    and "    std::_Exit(stored ? 0 : 2);\n" in capsule
    and "    if (g_capsule.scripted == 0) return;\n    std::fflush(nullptr);\n    std::_Exit(2);" in capsule
    and "    std::_Exit(!in_step ? 3 : capsule.unwritten != 0 ? 2 : 0);" in capsule,
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
    and "| Start / end a replay capsule | Ctrl+F12 |\n| Load the newest replay capsule | F12 |"
    in read("docs/PLAYING.md"),
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
    and renderer.count("SDL_AtomicLock(&s_snap_lock);") == 5
    and renderer.count("SDL_AtomicUnlock(&s_snap_lock);") == 5
    and "static void shown_capture(int kind, float alpha) {\n    int ww = 0, wh = 0;\n    shown_snap();\n    shown_count(kind);\n"
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
    and "    keep_preview();\n    gl_renderer_snap_cancel();\n    capsule.preview_asked = false;\n    char about[256];\n"
    in capsule
    and "        gl_renderer_snap_cancel();\n        capsule.previews = 0;\n        capsule.preview_asked = false;\n        capsule.recording = Recording::kOn;\n"
    in capsule
    and "        keep_preview();\n"
    "        if (disruptor::capsule::preview_due(capsule.recorded.size()) && gl_renderer_snap_shown(static_cast<int>(disruptor::capsule::kPreviewWidth)))\n"
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

print("Disruptor replay capsule source contract: PASS")
