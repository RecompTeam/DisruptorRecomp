#pragma once

#include <filesystem>
#include <optional>

namespace disruptor::launcher {

// Where the window was when it last closed, in screen coordinates.
struct WindowPlace {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
};

// What the launcher alone keeps, in launcher.ini beside settings.toml.
struct LauncherChoices {
    bool check_for_updates = true;
    std::optional<WindowPlace> window;
};

LauncherChoices read_choices(const std::filesystem::path& file);
bool write_choices(const std::filesystem::path& file, const LauncherChoices& choices);

} // namespace disruptor::launcher
