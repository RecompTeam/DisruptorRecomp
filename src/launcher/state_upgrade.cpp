#include "state_upgrade.h"

#include "game_process.h"
#include "state_stamp.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

constexpr DWORD trial_milliseconds = 90000;
constexpr std::size_t asked_frames = 60;

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value == INVALID_HANDLE_VALUE ? nullptr : value) {}
    ~Handle() {
        if (value_) CloseHandle(value_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }

private:
    HANDLE value_;
};

// A line of tried.txt: the state's path and contents, and the build that refused it.
std::string tried_line(const fs::path& saves, const SavedState& state, std::string_view bytes) {
    return state.path.lexically_relative(saves).generic_string() + "|" + fingerprint(bytes) + "|" + stamp_text(build_stamp());
}

std::set<std::string> tried(const fs::path& saves) {
    std::set<std::string> lines;
    std::ifstream input(saves / "old-states" / "tried.txt", std::ios::binary);
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.insert(line);
    }
    return lines;
}

void note_tried(const fs::path& saves, const std::string& line) {
    std::error_code failed;
    fs::create_directories(saves / "old-states", failed);
    std::ofstream(saves / "old-states" / "tried.txt", std::ios::binary | std::ios::app) << line << "\n";
}

struct Waiting {
    SavedState state;
    std::string bytes; // what was read once and is kept, tried and written back
};

std::vector<Waiting> untried(const fs::path& saves) {
    const std::set<std::string> done = tried(saves);
    std::vector<Waiting> waiting;
    for (SavedState& state : stale_states(saves)) {
        auto bytes = read_whole(state.path);
        if (!bytes || done.contains(tried_line(saves, state, *bytes))) continue;
        waiting.push_back({std::move(state), std::move(*bytes)});
    }
    return waiting;
}

std::string slot_name(const SavedState& state) {
    return "F" + std::to_string(state.slot + 1) + (state.language.empty() ? "" : " (" + state.language + ")");
}

// The original beside the others of its stamp: a different state at the same path gets a name of its own.
bool keep_original(const fs::path& saves, const Waiting& waiting) {
    const fs::path relative = waiting.state.path.lexically_relative(saves);
    fs::path kept = saves / "old-states" / stamp_text(waiting.state.stamp) / relative;
    std::error_code failed;
    if (fs::exists(kept, failed)) {
        if (read_whole(kept) == waiting.bytes) return true;
        kept.replace_filename(kept.stem().string() + "-" + fingerprint(waiting.bytes) + kept.extension().string());
        if (fs::exists(kept, failed)) return read_whole(kept) == waiting.bytes;
    }
    fs::create_directories(kept.parent_path(), failed);
    fs::path partial = kept;
    partial += ".partial";
    if (failed || !write_whole(partial, waiting.bytes)) return false;
    fs::rename(partial, kept, failed);
    return !failed;
}

// This process's environment with the switches of an unattended run: the trial folder as the player's files, no
// window, no sound, slot 0 loaded at once and a second of play recorded, after which the game ends by itself.
std::wstring trial_environment(const fs::path& trial) {
    const std::array<std::pair<std::wstring, std::wstring>, 6> given = {{
        {L"PSX_USER_DIR", trial.wstring()},
        {L"PSX_HIDDEN_WINDOW", L"1"},
        {L"SDL_AUDIODRIVER", L"dummy"},
        {L"PSX_HEADLESS", L"1"},
        {L"PSX_LOAD_SLOT", L"0"},
        {L"PSX_DISRUPTOR_CAPSULE_RECORD", std::to_wstring(asked_frames)},
    }};
    constexpr std::array<const wchar_t*, 3> dropped = {
        L"PSX_DISRUPTOR_CAPSULE_REPLAY", L"PSX_DISRUPTOR_CAPSULE_FRAMES", L"PSX_DISRUPTOR_LANGUAGE_DISC"};
    return environment_block(given, dropped);
}

// The length of the recording the run ended with, from capsule.cpp's line `disruptor: capsule: <N> frames in <folder>
// (<why>)`, counted only when it ended for the asked length.
std::size_t recorded_frames(const std::string& log) {
    constexpr std::string_view said = "disruptor: capsule: ";
    for (auto at = log.find(said); at != std::string::npos; at = log.find(said, at + 1)) {
        char* end = nullptr;
        const char* digits = log.c_str() + at + said.size();
        const std::size_t frames = std::strtoull(digits, &end, 10);
        std::string_view rest(end);
        rest = rest.substr(0, rest.find('\n'));
        if (!rest.empty() && rest.back() == '\r') rest.remove_suffix(1);
        if (end != digits && rest.starts_with(" frames in ") && rest.ends_with(" (the asked length)")) return frames;
    }
    return 0;
}

enum class Verdict { ran, refused, unknown, stuck };

struct TrialRun {
    Verdict verdict = Verdict::unknown;
    std::string why;
};

TrialRun run_trial(const fs::path& program, const fs::path& trial, const VerifiedDisc& disc,
                   const std::atomic_bool& cancelled) {
    std::wstring command = game_command(program, disc) + L" --memcard-dir saves";
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const fs::path log_path = trial / "run.log";
    std::wstring environment = trial_environment(trial);
    PROCESS_INFORMATION process{};
    {
        const Handle log(CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr));
        const Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!log.get() || !input.get()) return {Verdict::unknown, "its log could not be written"};
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input.get();
        startup.hStdOutput = startup.hStdError = log.get();
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, environment.data(),
                            trial.c_str(), &startup, &process))
            return {Verdict::unknown, "the game did not start"};
    }
    const Handle game(process.hProcess);
    const Handle thread(process.hThread);
    // The job ends the trial's game with the launcher, should the launcher itself end first.
    const Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const bool held = job.get() &&
                      SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
                      AssignProcessToJobObject(job.get(), game.get());
    if (!held) {
        const bool ended = TerminateProcess(game.get(), 1) && WaitForSingleObject(game.get(), 10000) == WAIT_OBJECT_0;
        return {ended ? Verdict::unknown : Verdict::stuck, "the game could not be held to the launcher's life"};
    }
    ResumeThread(thread.get());
    DWORD waited = 0;
    while (WaitForSingleObject(game.get(), 100) == WAIT_TIMEOUT) {
        waited += 100;
        if (!cancelled && waited < trial_milliseconds) continue;
        const bool ended = TerminateJobObject(job.get(), 1) && WaitForSingleObject(game.get(), 10000) == WAIT_OBJECT_0;
        if (!ended) return {Verdict::stuck, "the game could not be stopped"};
        return {Verdict::unknown, cancelled ? "cancelled" : "the game did not end within 90 seconds"};
    }
    DWORD code = 1;
    GetExitCodeProcess(game.get(), &code);
    const auto log = read_whole(log_path);
    if (!log) return {Verdict::unknown, "its log could not be read"};
    // Only the game's own word is a refusal: a run that ends before it says either is tried again.
    if (log->find("savestate: LOAD FAILED slot 0") != std::string::npos) return {Verdict::refused, "the game did not load it"};
    if (log->find("savestate: LOADED slot 0") == std::string::npos)
        return {Verdict::unknown, std::format("the game ended before it loaded a state, code 0x{:08X}", static_cast<unsigned>(code))};
    if (code != 0 || recorded_frames(*log) < asked_frames)
        return {Verdict::refused, std::format("the game stopped after loading it, code 0x{:08X}", static_cast<unsigned>(code))};
    return {Verdict::ran, {}};
}

// The language disc a state of `language` was made with, from the launcher's list. Empty when none is listed.
std::string disc_of(const std::string& language, const Regions& regions) {
    for (const auto& [path, region] : regions)
        if (region && region->language == language) return path;
    return {};
}

} // namespace

StateUpgrade upgrade_states(const fs::path& program, const fs::path& data, const VerifiedDisc& disc,
                            const SettingsFile& settings, const Regions& regions, const std::atomic_bool& cancelled,
                            const std::function<void(const std::wstring&, unsigned)>& report) {
    StateUpgrade result;
    const fs::path saves = settings.saves_folder();
    const std::vector<Waiting> states = untried(saves);
    if (states.empty()) return result;
    report(L"Save states of an earlier version were found: trying them on this one...", 0);
    const fs::path trial = data / "cache" / "state-trial";
    std::error_code failed;
    fs::remove_all(trial, failed);
    try {
        check_package(program, *disc.profile);
        fs::create_directories(trial);
        if (fs::exists(data / "settings.toml")) fs::copy_file(data / "settings.toml", trial / "settings.toml");
        if (fs::exists(data / "keybinds.ini")) fs::copy_file(data / "keybinds.ini", trial / "keybinds.ini");
    } catch (const std::exception& error) {
        result.lines.push_back(std::string("Save states of an earlier version were not tried: ") + error.what());
        fs::remove_all(trial, failed);
        return result;
    }
    SettingsFile trial_settings(trial / "settings.toml");
    trial_settings.load();
    bool stuck = false;
    std::size_t index = 0;
    for (const Waiting& waiting : states) {
        if (cancelled || stuck) break;
        const SavedState& state = waiting.state;
        report(wide(std::format("Trying save state {} on this version, {} of {}...", slot_name(state), index + 1,
                                states.size())),
               static_cast<unsigned>(index * 100 / states.size()));
        ++index;
        try {
            const std::string language_disc = state.language.empty() ? std::string() : disc_of(state.language, regions);
            if (!state.language.empty() && language_disc.empty()) {
                ++result.untried;
                result.lines.push_back(slot_name(state) + " was not tried: the " + state.language +
                                       " disc is not in the launcher's list.");
                continue;
            }
            fs::remove_all(trial / "saves");
            fs::remove_all(trial / "capsules");
            const fs::path slots = state.language.empty() ? trial / "saves" : trial / "saves" / state.language;
            fs::create_directories(slots);
            const std::string carried = restamped(waiting.bytes, build_stamp());
            if (carried.empty() || !keep_original(saves, waiting) || !trial_settings.choose_language_disc(language_disc) ||
                !write_whole(slots / state_name(state.entry, 0), carried)) {
                ++result.untried;
                result.lines.push_back(slot_name(state) + " was not tried: its copies could not be written.");
                continue;
            }
            const TrialRun run = run_trial(program, trial, disc, cancelled);
            stuck = run.verdict == Verdict::stuck;
            if (run.verdict == Verdict::ran) {
                // Written back only when the file is still the one tried.
                fs::path written = state.path;
                written += ".new";
                if (read_whole(state.path) == waiting.bytes && write_whole(written, carried)) {
                    fs::rename(written, state.path);
                    ++result.carried;
                    result.lines.push_back(slot_name(state) + " now loads on this version.");
                    continue;
                }
                fs::remove(written, failed);
                ++result.untried;
                result.lines.push_back(slot_name(state) + " changed while it was tried: it is tried again next time.");
                continue;
            }
            if (cancelled) break;
            if (run.verdict == Verdict::refused) {
                ++result.refused;
                note_tried(saves, tried_line(saves, state, waiting.bytes));
                result.lines.push_back(slot_name(state) + " does not run on this version (" + run.why +
                                       "). It stays as it was, and a copy is in saves\\old-states.");
            } else {
                ++result.untried;
                result.lines.push_back(slot_name(state) + " could not be tried (" + run.why + "): it is tried again next time.");
            }
        } catch (const std::exception& error) {
            ++result.untried;
            result.lines.push_back(slot_name(state) + " could not be tried: " + error.what());
        }
    }
    if (stuck) result.lines.push_back("A trial run of the game did not stop: " + utf8(trial) + " is left as it is.");
    else fs::remove_all(trial, failed);
    return result;
}

} // namespace disruptor::launcher
