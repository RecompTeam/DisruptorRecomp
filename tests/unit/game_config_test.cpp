#include "config_loader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

static std::string with_margin_line(const std::string& config, const std::string& line) {
    const std::string shipped = "depth24_trailing_margin = 0\n";
    const auto at = config.find(shipped);
    require(at != std::string::npos && config.find(shipped, at + 1) == std::string::npos,
            "game.toml must set the trailing margin once");
    return config.substr(0, at) + line + config.substr(at + shipped.size());
}

static int margin_of(const fs::path& path, const std::string& config) {
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << config;
    }
    return PSXRecompV4::load_game_config(path).runtime.video_depth24_trailing_margin;
}

static bool refused(const fs::path& path, const std::string& config) {
    try {
        margin_of(path, config);
    } catch (const std::runtime_error& e) {
        return std::string(e.what()).find("[video] depth24_trailing_margin out of range (0..64)") != std::string::npos;
    }
    return false;
}

int main() {
    const fs::path source = DISRUPTOR_SOURCE_DIR;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("disruptor-game-config-test-" + std::to_string(stamp));
    fs::create_directories(root);

    try {
        for (const char *name : {"game.toml", "game-widescreen.toml", "game-capture.toml"})
            require(PSXRecompV4::load_game_config(source / name).runtime.video_depth24_trailing_margin == 0,
                    std::string(name) + " must keep every column of a 24-bit frame");

        const std::string shipped = read_file(source / "game.toml");
        const fs::path edited = root / "game.toml";
        require(margin_of(edited, with_margin_line(shipped, "")) == 8,
                "a title that does not set the margin must keep the framework's 8 columns");
        require(margin_of(edited, with_margin_line(shipped, "depth24_trailing_margin = 64\n")) == 64,
                "a margin of 64 must be taken");
        require(refused(edited, with_margin_line(shipped, "depth24_trailing_margin = 65\n")),
                "a margin above 64 must be refused");
        require(refused(edited, with_margin_line(shipped, "depth24_trailing_margin = -1\n")),
                "a negative margin must be refused");

        fs::remove_all(root);
        std::cout << "game config: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "game config: FAIL: " << e.what() << "\n";
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
