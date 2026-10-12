#!/usr/bin/env python3
"""Source contract for the launcher's settings pages and language row."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Keys the game saves and the launcher leaves to the in-game menu, the command line or a hand edit.
NOT_OFFERED = {
    "video.renderer", "video.window_width", "video.antialiasing", "video.texture_filtering", "video.crt_filter",
    "video.auto_skip_fmv", "video.turbo_loads", "video.fast_boot", "video.bios_hle", "video.low_latency_input",
    "video.frame_interpolation_blend", "audio.spu_hq", "disruptor.diagnostic_view",
}
# Keys written together with a launcher row, or by the language row.
COVERED = {"video.adaptive_view", "disruptor.language_disc", "disruptor.language_discs"}
SHOWN_SECTIONS = ("video", "audio", "disruptor")
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
window = read("src/launcher/main.cpp")
region = read("src/launcher/region_disc.cpp")
launcher_cmake = read("src/launcher/CMakeLists.txt")
cmake = read("CMakeLists.txt")
loader = read("psxrecomp/recompiler/src/config_loader.cpp")
menu = read("src/game/ui/dev_menu.cpp") + read("src/game/frame/present_rate.cpp")

ROW = r'\b(toggle|choice|slider)(?:<[^>]*>)?\(\s*"([\w.]+)", "([^"]+)", (-?\d+)'
rows = re.findall(ROW, options)
keys = [key for _, key, _, _ in rows]
initial = {key: int(value) for _, key, _, value in rows}
require(len(rows) == 27 and len(set(keys)) == 27, "the shared table must hold its 27 options, each key once")
placed = re.findall(r'^    \{"([\w.]+)", Page::\w+\},$', table, re.MULTILINE)
require(
    sorted(placed) == sorted(keys)
    and not re.findall(ROW, table)
    and '    "${PROJECT_SOURCE_DIR}/src/game/settings/options.cpp")\n' in launcher_cmake
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
worker = window[window.index("        worker = std::thread(") : window.index("    void take_image(")]
require(
    "auto outcome = std::make_unique<Outcome>(run(std::move(request), discs, cancelled));" in worker
    and "        Step step = after(*outcome, app->cancelled);" in window
    and "        app->regions = std::move(outcome->regions);" in window,
    "what a task does and what the window does with its outcome are the flow module's, where they are tested",
)
require(
    "            app->begin(Task::import, outcome->image, outcome->launch);\n            return 0;" in window
    and "        if (step.disc == DiscAfter::cleared) app->verified = {};\n"
    "        else if (step.disc == DiscAfter::taken) app->verified = std::move(outcome->disc);\n"
    "        if (step.save_language && !app->settings.choose_language_disc(utf8(outcome->image))) step.status = app->settings_error();\n"
    "        if (!step.status.empty()) app->say(step.status);\n"
    "        if (step.launch) app->launch();\n" in window
    and "        if (launch) unwritten = !settings.complete() && settings.readable();\n" in window
    and "            say(running_text(environment_overrides(), unwritten));" in window,
    "the window must do each thing a step names, and say when the settings file could not be completed",
)
require(
    window.count("region_disc(") == worker.count("region_disc(") == 2 and window.count("check_region_disc(") == 1
    and worker.count("import_disc(") == 1 and window.count("import_disc(") == 2
    and worker.count("verify_disc(") == 1 and window.count("verify_disc(") == 3,
    "a disc image is opened on the worker only: one on a share that is gone must not stall the window",
)
require(
    '        SetEnvironmentVariableW(L"PSX_DISRUPTOR_LANGUAGE_DISC", nullptr);' in window
    and window.count("PSX_DISRUPTOR_LANGUAGE_DISC") == 1
    and "            if (WaitForSingleObject(app->game, 0) == WAIT_OBJECT_0) {" in window
    and "code != STILL_ACTIVE" not in window,
    "Play must start from the file as it is, the language disc reaches the game through settings.toml alone, "
    "and the game's end is asked of its handle: 259 is an exit code too",
)
require(
    "                app->settings.load(); // the in-game menu writes the same file" in window
    and "    bool editable() const { return !busy && !game && settings.readable(); }" in window,
    "the launcher must not write settings while the game runs and must read them again after it",
)
require(
    "        DragAcceptFiles(window, TRUE);" in window and "    case WM_DROPFILES: {" in window
    and "    case WM_DPICHANGED: {" in window and "DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2" in window
    and 'log_tail(app->root / "startup.log", log_lines)' in window,
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

print("Disruptor launcher source contract: PASS")
