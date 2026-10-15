#!/usr/bin/env python3
"""Source contract for the detailed log: the setting, where it is read, the driver's messages, the launcher's page and report."""

from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
runtime = read("psxrecomp/runtime/src/main.cpp")
module = read("src/game/session/diagnostics.cpp")
host = read("src/game/host.cpp")
menu = menu_text(ROOT)
debug_tab = read("src/game/ui/menu/tab_debug.cpp")
fields = read("src/game/settings/game_user_settings.h")
options = read("src/game/settings/options.cpp")
saved = read("src/game/settings/saved.cpp")
pages = read("src/launcher/launcher_settings.cpp")
page_kinds = read("src/launcher/launcher_settings.h")
window = "".join(read(f"src/launcher/{name}") for name in ("main.cpp", "session.h", "session.cpp", "view.cpp", "game_process.cpp"))
report = read("src/launcher/report.cpp")
log = read("src/launcher/startup_log.cpp")
playing = read("docs/PLAYING.md")

require(
    "    bool has_detailed_logging = false; bool detailed_logging = false;\n" in fields
    and "    toggle<&Settings::has_detailed_logging, &Settings::detailed_logging>(\n"
    '        "disruptor.detailed_logging", "Detailed logging", 0,\n' in options
    and '        if (d.contains("detailed_logging")) try_get([&]{\n'
    '            s.detailed_logging = toml::find<bool>(d, "detailed_logging");\n'
    "            s.has_detailed_logging = true;\n" in saved
    and '            f << "detailed_logging = " << (s.detailed_logging ? "true" : "false") << "\\n";\n' in saved
    and "s.has_diagnostic_view || s.has_detailed_logging ||\n" in saved,
    "the detailed log is a saved setting that starts off and needs nothing",
)
require(
    "enum class Page { display, enhancements, controls, audio, experimental, troubleshooting };\n" in page_kinds
    and 'L"Experimental", L"Troubleshooting"};\n' in pages
    and '    {"disruptor.detailed_logging", Page::troubleshooting},\n' in pages
    and pages.count("Page::troubleshooting") == 1
    and '    "Switch on, play until the problem shows, close the game, then press Report a problem.",\n};\n' in window
    and "    const char* note = page_notes[std::clamp(page_, 0, static_cast<int>(std::size(page_notes)) - 1)];\n" in window
    and window.count('    "",\n') == 5,
    "the launcher's sixth page is Troubleshooting: the switch alone, with a note that says what to do with it",
)
require(
    "    bool detailed = disruptor_diagnostics_detailed() != 0;\n"
    '    if (option_toggle("disruptor.detailed_logging", &detailed)) {\n'
    "        disruptor_diagnostics_set_detailed(detailed ? 1 : 0);\n"
    "        mark_detailed_logging(detailed);\n"
    "    }\n"
    '    if (detailed) ImGui::TextDisabled("Driver messages: %s.", driver_words[disruptor_diagnostics_driver_state()]);\n' in debug_tab
    and 'constexpr const char *driver_words[] = {"off", "on", "from the next launch", "not offered by this driver"};\n' in debug_tab
    and "enum { DISRUPTOR_DRIVER_OFF = 0, DISRUPTOR_DRIVER_ON = 1, DISRUPTOR_DRIVER_NEXT_LAUNCH = 2, DISRUPTOR_DRIVER_REFUSED = 3 };\n"
    in read("src/game/session/diagnostics.h")
    and 'void mark_detailed_logging(bool value) { mark("disruptor.detailed_logging", value); }\n' in menu
    and '    if (changed("disruptor.detailed_logging"))\n        disruptor_diagnostics_set_detailed(pending.detailed_logging ? 1 : 0);\n' in menu,
    "the Debug tab switches the detailed log at once, stages it for saving and says where the driver's part stands, a word a state",
)
require(
    "bool psx_game_settings_loaded(const PSXRecompV4::UserSettings &settings) {\n"
    "    disruptor_diagnostics_configure(settings.has_detailed_logging && settings.detailed_logging);\n" in host
    and runtime.index("        if (psx_game_settings_loaded(us)) fast_boot = true;\n") < runtime.index("        configure_core_gl_context_attributes();\n")
    and "    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);\n"
    "    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, gl_renderer_debug_messages() ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);\n}\n" in runtime
    and '    const char* forced = std::getenv("PSX_DISRUPTOR_DETAILED_LOGGING");\n'
    "    g_session.detailed = forced && forced[0] ? forced[0] != '0' : saved != 0;\n"
    "    g_session.asked_at_start = g_session.detailed;\n"
    "    gl_renderer_set_debug_messages(g_session.detailed ? 1 : 0);\n" in module,
    "the setting is read before the game's window is made, so that its contexts are made for the driver's messages, "
    "and an environment variable replaces it for a run",
)
require(
    '    fprintf(stdout, "psxrecomp: OpenGL context created (%s, %s)\\n", ver ? ver : "?", card ? card : "?");\n    debug_install(0);\n'
    in renderer
    and "        s_interp_ctx = SDL_GL_CreateContext(s_win);\n"
    "        if (!s_interp_ctx && s_debug_asked) {  /* as for the main context: a plain one, shared all the same */\n"
    "            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);\n            s_interp_ctx = SDL_GL_CreateContext(s_win);\n        }\n"
    "        if (s_interp_ctx) debug_install(1);\n" in renderer
    and renderer.index("        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);\n")
    < renderer.index("        s_interp_ctx = SDL_GL_CreateContext(s_win);\n        if (!s_interp_ctx && s_debug_asked) {")
    < renderer.index("        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);\n")
    and "    s_ctx = SDL_GL_CreateContext(win);\n    if (!s_ctx && s_debug_asked) {  /* a driver with no debug context still has a plain one */\n"
    "        s_debug_asked = 0;\n        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);\n        s_ctx = SDL_GL_CreateContext(win);\n    }\n" in renderer
    and "    if (!s_debug_asked) return;\n" in renderer
    and '    if (major * 10 + minor < 43 && !SDL_GL_ExtensionSupported("GL_KHR_debug")) return;\n' in renderer
    and "    if (!callback || !control) return;\n    glEnable(0x92E0 /* GL_DEBUG_OUTPUT */);\n"
    "    control(GL_DONT_CARE, GL_DONT_CARE, 0x826B /* GL_DEBUG_SEVERITY_NOTIFICATION */, 0, NULL, GL_FALSE);\n"
    "    control(GL_DONT_CARE, GL_DONT_CARE, 0x9148 /* GL_DEBUG_SEVERITY_LOW */, 0, NULL, GL_FALSE);\n"
    "    callback(debug_message, presenter ? &s_presenter_tag : NULL);\n    s_debug_active = 1;\n" in renderer
    and "    SDL_AtomicLock(&s_debug_lock);\n    if (s_debug_count < DEBUG_QUEUE) {\n" in renderer
    and "    } else {\n        ++s_debug_lost;\n    }\n    SDL_AtomicUnlock(&s_debug_lock);\n}\n" in renderer
    and "    int taken = 0;\n    SDL_AtomicLock(&s_debug_lock);\n    *lost = s_debug_lost;\n    s_debug_lost = 0;\n    if (s_debug_count > 0) {\n" in renderer
    and "        taken = 1;\n    }\n    SDL_AtomicUnlock(&s_debug_lock);\n    return taken;\n}\n" in renderer
    and "        memmove(&s_debug_queue[0], &s_debug_queue[1], (size_t)(--s_debug_count) * sizeof s_debug_queue[0]);\n" in renderer,
    "each of the two contexts hands the driver's medium and high messages to a short queue under a lock, where the driver "
    "offers them: a full queue counts what it loses, the game takes the oldest first, and a driver that makes no context "
    "for them is asked for a plain one, the presenter's shared like the first",
)
require(
    '        std::printf("disruptor: first frame %.1f s after the start\\n", seconds);\n' in module
    and "    if (!session.detailed) {\n        session.announced = false;\n        return;\n    }\n" in module
    and 'void say(const std::string& text) { std::printf("disruptor: +%.1fs %s\\n", g_session.now, text.c_str()); }\n' in module
    and "void disruptor_diagnostics_note(const char *format, ...) {\n    if (!g_session.detailed) return;\n" in module
    and "    for (int taken = 0; taken < driver_messages_a_frame; ++taken) {\n" in module
    and "constexpr int driver_messages_a_frame = 16;" in module
    and '        std::printf("disruptor: settings at start: the settings file is invalid and was not applied\\n");\n        return;\n' in menu
    and "    for (const Option *option : g_preferences.changed)\n"
    '        disruptor_diagnostics_note("setting %s = %d", option->key, option->get(g_preferences.pending));\n'
    "    g_preferences.clean();\n" in menu
    and '    std::printf("disruptor: settings at start: %s\\n", disruptor::settings::describe(settings).c_str());\n' in menu,
    "every session's log says its settings, or that their file is invalid, and when its first frame came, and the detailed "
    "one times its lines, says each setting the menu saved and takes no more driver messages a frame than the queue holds",
)
require(
    'constexpr std::array<const char*, 6> gathered = {"startup.log", "startup.previous.log", "disruptor-mouse-aim.log", "settings.toml",\n'
    '                                                 "psx_last_run_report.json", "psx_freeze_heartbeat.json"};\n' in report
    and "        if (made && *made >= began && plain_file(entry->path())) found.emplace_back(*made, name);\n" in report
    and "    return fs::is_regular_file(fs::symlink_status(file, missing));\n" in report
    and '    const std::optional<std::int64_t> began = run_began(root / "startup.log");\n    if (began)\n' in report
    and 'std::string started_line() { return std::string(started) + utc_now() + "\\r\\n"; }\n' in report
    and "    std::string heading = started_line();\n" in window
    and "    if (found.size() > most_freeze_dumps) found.resize(most_freeze_dumps);\n" in report
    and '        note += narrow(name.native()) + ": " + (present ? listed(root / name) : "none") + "\\r\\n";\n' in report
    and '    write(folder / "system.txt", without_user_names(note));\n' in report
    and "    : process_(start_game(program, data, disc)), started_(GetTickCount64()) {}\n" in window
    and "unsigned long long GameProcess::seconds() const { return (GetTickCount64() - started_) / 1000; }\n" in window
    and "        game_ = std::make_unique<GameProcess>(program_, data_, verified_);\n" in window
    and '    note_exit(data_ / "startup.log", code, seconds);\n' in window
    and '"%slauncher: the game ended with code 0x%08lX after %llu s\\r\\n"' in log,
    "a report takes how the last run ended and lists every file with its time, and the launcher heads the log with when "
    "the game started and ends it with how it ended and after how long: a freeze dump is taken when its name says it was "
    "made since that start, and a link is never followed",
)
require(
    "Switch on **Detailed logging** on the launcher's **Troubleshooting** page" in " ".join(playing.split())
    and "PSX_DISRUPTOR_DETAILED_LOGGING" not in playing,
    "the guide sends a player to the switch before the report",
)
require(
    "    disruptor_contract(diagnostics)\n" in cmake
    and '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/diagnostics.cpp"\n' in cmake
    and "    add_test(NAME disruptor_diagnostics_forced COMMAND disruptor-diagnostics-test --forced)\n"
    '    set_tests_properties(disruptor_diagnostics_forced PROPERTIES ENVIRONMENT "PSX_DISRUPTOR_DETAILED_LOGGING=1")\n' in cmake
    and "    add_test(NAME disruptor_diagnostics_refused COMMAND disruptor-diagnostics-test --refused)\n"
    '    set_tests_properties(disruptor_diagnostics_refused PROPERTIES ENVIRONMENT "PSX_DISRUPTOR_DETAILED_LOGGING=0")\n' in cmake,
    "this contract and the module's three test runs must remain registered",
)

print("Disruptor diagnostics source contract: PASS")
