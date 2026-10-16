#!/usr/bin/env python3
"""Source contract for the launcher's settings pages and language row."""

import re
from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]

# Keys the game saves and the launcher leaves to the in-game menu, the command line or a hand edit.
NOT_OFFERED = {
    "video.renderer", "video.window_width", "video.antialiasing", "video.texture_filtering", "video.crt_filter",
    "video.auto_skip_fmv", "video.turbo_loads", "video.fast_boot", "video.bios_hle", "video.low_latency_input",
    "video.frame_interpolation_blend", "audio.spu_hq", "disruptor.diagnostic_view",
    # Withdrawn for now on the user's word: the game keeps both off whatever the file says.
    "disruptor.downscale_averaging", "disruptor.edge_smoothing",
    # game.toml routes Player 1 to the keyboard and the first gamepad together, and locks the pad mode.
    "controller.p1_device", "controller.p2_device", "controller.p1_mode", "controller.p2_mode",
}
# Keys written together with a launcher row, or by the language row.
COVERED = {"video.adaptive_view", "disruptor.language_disc", "disruptor.language_discs", "disruptor.sample_shading"}
SHOWN_SECTIONS = ("video", "audio", "disruptor", "controller")
UTF8_PAGE = '<activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>'


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def plain_values(text: str) -> dict[str, str]:
    values, section = {}, ""
    for line in text.splitlines():
        if header := re.fullmatch(r"\[(\w+)\]", line.strip()):
            section = header[1]
        elif pair := re.fullmatch(r"(\w+)\s*=\s*(\S.*)", line.strip()):
            values[f"{section}.{pair[1]}"] = pair[2]
    return values


table = read("src/launcher/launcher_settings.cpp")
options = read("src/game/settings/options.cpp")
window = "".join(read(f"src/launcher/{name}") for name in ("main.cpp", "session.h", "session.cpp", "view.cpp", "game_process.cpp"))
session = read("src/launcher/session.cpp")
region = read("src/launcher/region_disc.cpp")
launcher_cmake = read("src/launcher/CMakeLists.txt")
cmake = read("CMakeLists.txt")
loader = read("psxrecomp/recompiler/src/config_loader.cpp")
game_section = read("src/game/settings/saved.cpp")
menu = menu_text(ROOT) + read("src/game/frame/present_rate.cpp")

ROW = r'\b(toggle|choice|slider)(?:<[^>]*>)?\(\s*"([\w.]+)", "([^"]+)", (-?\d+)'
rows = re.findall(ROW, options)
keys = [key for _, key, _, _ in rows]
initial = {key: int(value) for _, key, _, value in rows}
require(len(rows) == 33 and len(set(keys)) == 33, "the shared table must hold its 33 options, each key once")
placed = re.findall(r'^    \{"([\w.]+)", Page::\w+(?:, (?:"[\w ]+"|nullptr)(?:, true)?)?\},$', table, re.MULTILINE)
require(
    sorted(placed) == sorted(keys)
    and not re.findall(ROW, table)
    and '    "${PROJECT_SOURCE_DIR}/src/game/settings/options.cpp"\n' in launcher_cmake
    and "std::span<const Placement> layout() { return placed; }\n" in table
    and "    for (const auto& placement : layout()) {\n        const Option& option = *find_option(placement.key);\n" in window,
    "the launcher puts each shared option on one page and describes none itself",
)
require(
    "    return settings::applies(option, [this](const Option& needed) { return shown(needed) != 0; });\n" in table
    and "std::any_of(other.needs.begin(), other.needs.end(), named)" in table,
    "the launcher greys a row by the shared rule asked of its file, and a toggle switched off takes with it the toggles that need it",
)

writer = loader[loader.index("bool save_user_settings(") :]
require(
    "#ifdef PSX_HAS_GAME_USER_SETTINGS\n    write_game_user_settings(f, s);\n#endif\n\n    f.flush();\n" in writer
    and "#ifdef PSX_HAS_GAME_USER_SETTINGS\n    read_game_user_settings(doc, s);\n#endif\n    return s;\n" in loader
    and "disruptor" not in loader.lower()
    and '    if (doc.contains("disruptor")) {\n        const toml::value& d = toml::find(doc, "disruptor");\n' in game_section
    and '        f << "\\n[disruptor]\\n";\n' in game_section,
    "the framework's writer and reader hand the game's section to the game, last, and name no game",
)
settings_struct = read("psxrecomp/recompiler/src/config_loader.h")
require(
    "#ifdef PSX_HAS_GAME_USER_SETTINGS\nstruct UserSettings : GameUserSettings {\n#else\nstruct UserSettings {\n#endif\n"
    in settings_struct
    and '#ifdef PSX_HAS_GAME_USER_SETTINGS\n#include "game_user_settings.h"' in settings_struct
    and "disruptor" not in settings_struct.lower()
    and "struct GameUserSettings {\n" in read("src/game/settings/game_user_settings.h")
    and "void read_game_user_settings(const toml::value& doc, UserSettings& s) {\n" in game_section
    and "void write_game_user_settings(std::ostream& f, const UserSettings& s) {\n" in game_section
    and "#define TOML11_USE_UNRELEASED_TOML_FEATURES\n" in game_section
    and "#define TOML11_USE_UNRELEASED_TOML_FEATURES\n" in loader,
    "the settings struct takes the game's fields from the game, which reads and writes its own section "
    "with the reader's own build of the TOML library",
)
require(
    cmake.index('include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings")\n')
    < cmake.index("add_compile_definitions(PSX_HAS_GAME_USER_SETTINGS=1)\n")
    < cmake.index("psxrecomp_add_runtime_target(psx-runtime")
    < cmake.index("    add_subdirectory(src/launcher)")
    and cmake.count('"${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/saved.cpp"\n') == 3
    and '    "${PROJECT_SOURCE_DIR}/src/game/settings/saved.cpp")\n' in launcher_cmake
    and "PSX_HAS_GAME_USER_SETTINGS" not in launcher_cmake,
    "every target of the build sees the same settings struct, and each that builds the reader builds the game's section",
)
writer += game_section[game_section.index("void write_game_user_settings(") :]
saved, section = set(), ""
for header, key in re.findall(r'f << "(?:\\n)?\[(\w+)\]\\n|f << "(\w+)\s*= ', writer):
    section = header or section
    if key and section in SHOWN_SECTIONS:
        saved.add(f"{section}.{key}")
require(len(saved) >= 34, "the settings writer was not read as expected")
undecided = saved - set(keys) - NOT_OFFERED - COVERED
require(not undecided, f"decide whether the launcher shows these saved keys: {sorted(undecided)}")
require(not set(keys) - saved, f"the launcher shows keys the game does not save: {sorted(set(keys) - saved)}")
require(
    not (NOT_OFFERED | COVERED) - saved,
    f"keys listed here are no longer saved: {sorted((NOT_OFFERED | COVERED) - saved)}",
)

drawn = set(re.findall(r'\boption_(?:toggle|slider|list)\("([\w.]+)"', menu)) | set(
    re.findall(r'find_option\("([\w.]+)"\)->label', menu))
require(drawn == set(keys), f"the in-game menu must draw every row by its key: {sorted(set(keys) ^ drawn)}")
for kind, key, label, _ in rows:
    require(f'"{label}"' not in menu, f"the in-game menu must take the name of {key} from the shared table")

shipped = plain_values(read("release/windows/settings.toml"))
require(not set(shipped) - set(keys), f"the shipped settings.toml sets keys the launcher does not show: {sorted(set(shipped) - set(keys))}")
mouse = read("src/game/input/mouse_aim.cpp")
require(
    "constexpr double kDefaultHorizontalSensitivity = 0.080;" in mouse
    and "constexpr double kDefaultVerticalSensitivity = 0.080;" in mouse
    and initial["disruptor.horizontal_sensitivity"] == 80 and initial["disruptor.vertical_sensitivity"] == 80,
    "the launcher must start from the game's mouse sensitivity",
)
require(
    initial["video.vsync"] == 2 and "static int           g_video_vsync        = 1;" in read("psxrecomp/runtime/src/main.cpp")
    and initial["disruptor.hud_scale"] == 100 and initial["audio.master_volume"] == 100,
    "the launcher must start from the runtime's own defaults",
)

require(
    '"${PSXRECOMP_ROOT}/recompiler/src/config_loader.cpp"' in launcher_cmake
    and '"${PROJECT_SOURCE_DIR}/src/game/language/language_disc.cpp"' in launcher_cmake
    and "    Settings fresh = PSXRecompV4::load_user_settings(path_);" in table
    and "    if (!PSXRecompV4::save_user_settings(path_, fresh)) return false;" in table
    and "    if (!readable_) return false;\n    alter(fresh);" in table,
    "the launcher must read and write settings.toml with the game's code, and never over a file it could not read",
)
require(
    "    if (!known_disc(other, known))" in region and "    if (!laid_out.build(home, other, why))" in region,
    "a language disc must be checked with the game's own layout code",
)
worker = session[session.index("    worker_ = std::thread(") : session.index("void Session::finish(")]
require(
    "auto outcome = std::make_unique<Outcome>(run(std::move(request), discs, cancelled_));" in worker
    and "    Step step = after(outcome, cancelled_);" in window
    and "    regions_ = std::move(outcome.regions);" in window,
    "what a task does and what the window does with its outcome are the flow module's, where they are tested",
)
require(
    "        begin(Task::import, outcome.image, outcome.launch);\n        return;" in window
    and "    if (step.disc == DiscAfter::cleared) verified_ = {};\n"
    "    else if (step.disc == DiscAfter::taken) verified_ = std::move(outcome.disc);\n"
    "    if (step.save_language && !settings_.choose_language_disc(utf8(outcome.image))) step.status = settings_error();\n"
    "    if (!step.status.empty()) say(step.status);\n"
    "    if (step.launch) launch();\n" in window
    and "    if (launch) unwritten_ = !settings_.complete() && settings_.readable();\n" in window
    and "        say(running_text(environment_overrides(), unwritten_));" in window,
    "the window must do each thing a step names, and say when the settings file could not be completed",
)
require(
    '    if (code == 0) {\n        show({});\n        activity_.add("Game closed.");\n    } else {\n' in window
    and "    status_ = shown_status(text, settings_.readable() ? L\"\" : settings_error());\n" in window
    and "Press Play to start again" not in window,
    "a game closed as usual clears the status line and leaves its line in the activity only",
)
require(
    window.count("region_disc(") == worker.count("region_disc(") == 2 and window.count("check_region_disc(") == 1
    and worker.count("import_disc(") == 1 and window.count("import_disc(") == 2
    and worker.count("verify_disc(") == 1 and window.count("verify_disc(") == 3,
    "a disc image is opened on the worker only: one on a share that is gone must not stall the window",
)
require(
    '        platform::unset("PSX_DISRUPTOR_LANGUAGE_DISC");' in window
    and window.count("PSX_DISRUPTOR_LANGUAGE_DISC") == 1
    and "    if (WaitForSingleObject(state_->process.get(), static_cast<DWORD>(most.count())) != WAIT_OBJECT_0) return std::nullopt;"
    in read("src/launcher/platform_windows.cpp")
    and "std::optional<int> Process::ended() { return wait(std::chrono::milliseconds(0)); }" in read("src/launcher/platform_windows.cpp")
    and "    const std::optional<int> code = process_->ended();\n" in window
    and "code != STILL_ACTIVE" not in window,
    "Play must start from the file as it is, the language disc reaches the game through settings.toml alone, "
    "and the game's end is asked of its handle: 259 is an exit code too",
)
require(
    "    settings_.load(); // the in-game menu writes the same file" in window
    and "    bool editable() const { return !busy_ && !game_ && settings_.readable(); }" in window
    and "    if (!editable()) return;\n    saved(settings_.set(option, value));\n" in window,
    "the launcher must not write settings while the game runs and must read them again after it",
)
require(
    "                        view.dropped(from_utf8(event.drop.data));" in window
    and "SDL_EVENT_DROP_FILE" in window
    and "                        view.style(SDL_GetWindowDisplayScale(window));" in window
    and "DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2" in window
    and 'log_tail(data_ / "startup.log", log_lines)' in window,
    "drag and drop, the per-monitor layout and the log tail must stay",
)
require(
    UTF8_PAGE in read("src/game/disruptor.manifest") and UTF8_PAGE in read("src/launcher/launcher.manifest")
    and '1 RT_MANIFEST "disruptor.manifest"' in read("src/game/disruptor.rc")
    and 'target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game/disruptor.manifest")' in cmake
    and 'target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game/disruptor.rc")' in cmake,
    "the game and the launcher must both run with UTF-8 as their code page, or a path outside it cannot be named",
)
require(
    "add_test(NAME disruptor_launcher_settings COMMAND disruptor-launcher-settings-test)" in launcher_cmake
    and "add_test(NAME disruptor_launcher_flow COMMAND disruptor-launcher-flow-test)" in launcher_cmake
    and "add_test(NAME disruptor_region_disc COMMAND disruptor-region-disc-test)" in launcher_cmake
    and "    disruptor_contract(launcher)\n" in cmake,
    "the launcher's tests must remain registered",
)

report = read("src/launcher/report.cpp")
playing = " ".join(read("docs/PLAYING.md").split())  # the guide is wrapped where it reads best
require(
    'constexpr std::array<const char*, 6> gathered = {"startup.log", "startup.previous.log", "disruptor-mouse-aim.log", "settings.toml",\n'
    in report
    and '        if (present) write(folder / name, without_user_names(end_of(root / name)));\n' in report
    and '    write(folder / "system.txt", without_user_names(note));\n' in report
    and "    if (!archive || !write_zip(folder, zip)) {\n        fs::remove(zip, failed);\n        return {folder, false};\n    }\n"
    "    fs::remove_all(folder, failed);\n    return {zip, true};\n" in report
    and '    std::string note = "Disruptor Recompiled " DISRUPTOR_BUILD_VERSION "\\r\\n";\n' in report
    and "    for (const std::string& line : platform::system_lines()) note += line + \"\\r\\n\";\n" in report
    and '    lines.push_back("Windows: " + windows_name(registry_text(L"ProductName"), registry_text(L"CurrentBuildNumber")) + ", version " +\n'
    in read("src/launcher/platform_windows.cpp"),
    "a report takes the game's log of two runs, the mouse log and the settings, each with user names cut out, and a note "
    "of the system: one ZIP the launcher writes itself, with no folder and no half-made ZIP left beside it",
)
require(
    '    fs::rename(data / "startup.log", data / "startup.previous.log", kept);\n' in window
    and '    launch.output = data / "startup.log";\n' in window
    and '        if (!line.empty()) launch.heading += "launcher: " + std::string(line.begin(), line.end()) + "\\r\\n";\n'
    in window
    and "        WriteFile(output.get(), launch.heading.data(), static_cast<DWORD>(launch.heading.size()), &written, nullptr);\n"
    in read("src/launcher/platform_windows.cpp")
    and "write(output, launch.heading.data(), launch.heading.size())" in read("src/launcher/platform_posix.cpp")
    and '    if (small(icon::bug, "Report a problem", true, "Saves the game\'s logs, your settings and a note of the system for a bug report."))\n'
    '        session_.make_problem_report();\n' in window
    and "void Session::make_problem_report() {\n    try {\n"
    "        const Report report = make_report(data_, system_note(), report_stamp());\n" in window
    and window.count("make_problem_report()") == 3
    and "editable" not in window[window.index("void Session::make_problem_report() {") : window.index("void Session::open_saves(")]
    and "BeginDisabled" not in window[window.index("    const auto small = [&]("):window.index("    if (small(icon::bug,")].replace("ImGui::BeginDisabled(!enabled);", "")
    and "        return clicked && enabled;\n" in window[window.index("    const auto small = [&]("):window.index("    if (small(icon::bug,")],
    "the launcher keeps the log of the run before, heads a new log with the build and the system, and has a button that "
    "makes a report at any time, an import or a running game included",
)
require(
    '        std::printf("disruptor: mouse %s failed (%s): %s\\n", captured ? "capture" : "release", reason, SDL_GetError());\n' in mouse
    and '    std::printf("disruptor: mouse %s (%s)\\n", captured ? "captured" : "released", reason);\n' in mouse
    and "    const char *card = (const char *)glGetString(GL_RENDERER);\n"
    '    fprintf(stdout, "psxrecomp: OpenGL context created (%s, %s)\\n", ver ? ver : "?", card ? card : "?");\n'
    in read("psxrecomp/runtime/src/gpu_gl_renderer.c"),
    "the game's log says each time the mouse is taken or let go and why, a refusal with the system's reason, and names the "
    "graphics card beside its driver",
)
require(
    "    report.cpp\n" in launcher_cmake
    and "execute_process(COMMAND git describe --tags --always --dirty\n" in launcher_cmake
    and 'set_property(SOURCE report.cpp APPEND PROPERTY COMPILE_DEFINITIONS\n    "DISRUPTOR_BUILD_VERSION=\\"${DISRUPTOR_BUILD_VERSION}\\"")\n'
    in launcher_cmake
    and "    add_test(NAME disruptor_launcher_report COMMAND disruptor-launcher-report-test)\n" in launcher_cmake
    and "Press **Report a problem** in the launcher." in playing
    and "your disc image, BIOS and saves are never included" in playing,
    "the report is built into the launcher with the build's version, its test is registered, and the guide tells a player "
    "of the button and of what it leaves out",
)

boot_state = read("psxrecomp/runtime/src/boot_state.c")
header_writer = boot_state[boot_state.index("static int write_header_le(") : boot_state.index("static int read_header_le(")]
fields = re.findall(r"pst_w_(?:u32|i32)\(&w, h->(\w+)\)", header_writer)
stamp = read("src/launcher/state_stamp.cpp")
upgrade = read("src/launcher/state_upgrade.cpp")
require(
    fields == ["magic", "version", "bios_checksum", "entry_pc", "codegen_hash", "abi_tag", "codegen_ver",
               "section_count", "reserved"]
    and "constexpr std::size_t hash_at = 16, abi_at = 20, version_at = 24;" in stamp
    and "word(header, 0) != BOOT_STATE_MAGIC || word(header, 4) != BOOT_STATE_VERSION" in stamp
    and "SavedState state{file, {}, *slot, word(header, 12)};" in stamp
    and '"%s%sstate_%08X_slot%02d.pst"' in read("psxrecomp/runtime/src/savestate.c")
    and 'fprintf(stderr, "savestate: LOADED slot %d -> resuming pc=0x%08X\\n",' in read("psxrecomp/runtime/src/savestate.c")
    and 'fprintf(stderr, "savestate: LOAD FAILED slot %d (missing/mismatched) %s\\n",' in read("psxrecomp/runtime/src/savestate.c")
    and 'log->find("savestate: LOADED slot 0")' in upgrade
    and 'log->find("savestate: LOAD FAILED slot 0")' in upgrade
    and '"disruptor: capsule: %zu frames in %s (%s)\\n"' in read("src/game/session/capsule.cpp")
    and 'rest.starts_with(" frames in ") && rest.ends_with(" (the asked length)")' in upgrade
    and 'std::getenv("PSX_HIDDEN_WINDOW")' in read("psxrecomp/runtime/src/main.cpp")
    and '{"PSX_HIDDEN_WINDOW", "1"}' in upgrade,
    "the launcher reads and restamps a save state by the header boot_state.c writes, and judges a trial by the lines "
    "the game prints when it loads a slot and when a recording ends, with the game's window hidden",
)

runtime_main = read("psxrecomp/runtime/src/main.cpp")
folder = read("src/game/settings/user_folder.cpp")
program_sites = re.findall(r"exe_dir_from_argv\((?:argv0|argv\[0\])\)", runtime_main)
require(
    len(program_sites) == 10
    and "        g_user_dir = psx_game_user_folder(exe_dir_from_argv(argv[0]), problem);\n" in runtime_main
    and runtime_main.index("psx_game_user_folder(") < runtime_main.index("    psx_crash_trace_install_handlers();")
    and "    if (!psx_set_note_folder(user_dir_from_argv(argv[0]).string().c_str())) {\n" in runtime_main
    and "    return psx_moved_to_user_dir(path, exe_dir_from_argv(argv0), user_dir_from_argv(argv0));\n" in runtime_main
    and "    return psx_moved_to_user_dir(path, program, user);\n" in folder
    and "    return path.is_relative() ? user_dir_from_argv(argv0) / path : in_user_dir(path, argv0);\n" in runtime_main
    and runtime_main.index("static std::filesystem::path in_user_dir(") < runtime_main.index("static std::filesystem::path under_user_dir(")
    and runtime_main.count('user_dir_from_argv(argv[0]) / "settings.toml"') == 3
    and "    fs::path config_path = user_dir_from_argv(argv0) / \"input.ini\";\n" in runtime_main
    and "    return user_dir_from_argv(argv0) / filename;\n" in runtime_main
    and 'user_dir_from_argv(argv[0]) / "mods", game_id,' in runtime_main
    and 'std::string cache_dir = (user_dir / "cache").string();' in runtime_main
    and "memcard_dir   = in_user_dir(gc.runtime.memcard_dir, argv[0]);" in runtime_main
    and 'psx_note_path("psx_cps_exit_trace.json"' in runtime_main
    and 'psx_note_path("psx_last_run_report.json", kReportPath, sizeof(kReportPath));' in read("psxrecomp/runtime/src/crash_trace.c")
    and read("psxrecomp/runtime/src/freeze_heartbeat.c").count("psx_note_path(") == 3
    and read("psxrecomp/runtime/src/memory.c").count('psx_note_path("psx_crash.txt"') == 2
    and read("psxrecomp/runtime/src/traps.c").count('psx_note_path("psx_crash.txt"') == 1
    and 'own_or_shipped("mouse-aim.ini")' in read("src/game/input/mouse_aim.cpp")
    and 'in_user_folder("disruptor-mouse-aim.log")' in read("src/game/input/mouse_aim.cpp")
    and 'in_user_folder("disruptor-control-probe.log")' in read("src/game/input/control_probe.cpp")
    and "return disruptor::user_folder::prepare(program);" in read("src/game/host.cpp"),
    "in the source, each writable path of the game, from settings to the notes of a crash, is named in the user folder "
    "the game host gives before anything else runs, a configured memory card folder is moved there by the rule the "
    "user folder test checks, and exe_dir_from_argv is left only at its ten program-file sites",
)
require(
    'constexpr const char* kInstalledName = "DisruptorRecompData";' in read("src/game/settings/user_folder.h")
    and 'return (local / kInstalledName).lexically_normal();' in folder
    and 'case Copy::portable: return (folder_of(program).parent_path() / "data").lexically_normal();' in folder
    and 'kind(root / ".portable") == fs::file_type::not_found ? Copy::installed : Copy::portable' in folder
    and "const fs::path data = disruptor::user_folder::prepare(program);" in window
    and 'launch.environment = platform::with(platform::environment(), {{"PSX_USER_DIR", utf8(data)}});' in window
    and '{{"PSX_USER_DIR", utf8(trial)},' in upgrade
    and 'const fs::path trial = data / "cache" / "state-trial";' in upgrade
    and "carry" not in launcher_cmake
    and not (ROOT / "src/launcher/carry.cpp").exists(),
    "an installed copy keeps the player's files in local application data, a portable one beside its program folder, "
    "the launcher and the game find the same folder and the trial run gets one of its own",
)

print("Disruptor launcher source contract: PASS")
