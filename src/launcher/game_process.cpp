#include "game_process.h"
#include "game/language/language.h"
#include "game/settings/game_package.h"
#include "game/settings/user_folder.h"

#include "launcher_flow.h"
#include "launcher_settings.h"
#include "platform.h"
#include "report.h"

#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

std::unique_ptr<platform::Process> start_game(const fs::path& program, const fs::path& data, const VerifiedDisc& disc,
                                             const std::optional<RegionDisc>& language) {
    check_package(program, *disc.profile);
    std::error_code kept;  // a report takes the run before as well: a crash is often seen after a second start
    fs::rename(data / "startup.log", data / "startup.previous.log", kept);
    platform::Launch launch;
    launch.executable = platform::game_executable(program);
    launch.arguments = game_arguments(program, disc);
    launch.folder = program;
    platform::Variables given{{"PSX_USER_DIR", utf8(data)}};
    given.emplace_back(DISRUPTOR_LAUNCH_LANGUAGE, language ? language->language + "\n" + utf8(language->image) : "English");
    if (const std::string_view name = platform::window_class(); !name.empty()) given.emplace_back("SDL_APP_ID", std::string(name));
    launch.environment = platform::with(platform::environment(), given);
    launch.output = data / "startup.log";
    launch.heading = started_line();
    for (const auto line : std::views::split(system_note(), std::string_view("\r\n")))
        if (!line.empty()) launch.heading += "launcher: " + std::string(line.begin(), line.end()) + "\r\n";
    try {
        return std::make_unique<platform::Process>(launch);
    } catch (const std::system_error& error) {
        throw std::runtime_error("The game could not be started: " + std::string(error.what()) + "\n"
#ifdef _WIN32
                                 "Check that the game is installed whole and the Microsoft Visual C++ x64 Redistributable too\n"
#else
                                 "Check that the game is installed whole and the folder of your files can be written\n"
#endif
                                 "Help in the side bar has the steps");
    }
}

} // namespace

fs::path executable_directory() { return platform::executable_path().parent_path(); }

void check_disc_location(const fs::path& program, const fs::path& mount) {
    if (user_folder::copy_of(program) == user_folder::Copy::plain) return;
    user_folder::require_update_safe_folder(program, mount);
    user_folder::require_update_safe_folder(program, resolve_image(mount));
}

fs::path checked_installed_disc(const fs::path& program, const fs::path& data) {
    user_folder::require_update_safe_folder(program, data / "input/discs");
    const fs::path mount = find_installed_disc(data);
    if (!mount.empty()) check_disc_location(program, mount);
    return mount;
}

VerifiedDisc import_player_disc(const fs::path& program, const fs::path& data, const fs::path& selected,
                                const std::atomic_bool& cancelled, const Progress& progress) {
    const fs::path library = user_folder::ensure_directory(program, data / "input/discs");
    auto verified = import_disc(selected, library, cancelled, progress);
    check_disc_location(program, verified.mount);
    user_folder::require_update_safe_folder(program, verified.data);
    return verified;
}

void check_package(const fs::path& program, const DiscProfile& profile) {
    if (!fs::is_regular_file(program / "mods/bundled" / disruptor::kGamePackage / "1.0.0/manifest.toml"))
        throw std::runtime_error(disruptor::game_package_problem(false, false));
    for (const auto& name : {platform::game_executable(program).filename(), fs::path(profile.game_config), fs::path(L"bios/openbios.bin")}) {
        if (!fs::is_regular_file(program / name))
            throw std::runtime_error("The game is incomplete\n"
                                     "Install it again: the launcher, the game, game.toml and the bios folder belong together");
    }
    if (fs::file_size(program / "bios/openbios.bin") != 524288)
        throw std::runtime_error("The bundled OpenBIOS is incomplete\nInstall the game again");
}

GameProcess::GameProcess(const fs::path& program, const fs::path& data, const VerifiedDisc& disc, const std::optional<RegionDisc>& language)
    : process_(start_game(program, data, disc, language)), started_(std::chrono::steady_clock::now()) {}

GameProcess::~GameProcess() = default;

std::optional<unsigned long> GameProcess::ended() const {
    const std::optional<int> code = process_->ended();
    if (!code) return std::nullopt;
    return static_cast<unsigned long>(static_cast<unsigned int>(*code));
}

unsigned long long GameProcess::seconds() const {
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count());
}

} // namespace disruptor::launcher
