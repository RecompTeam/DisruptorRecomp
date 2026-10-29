#include "game/settings/game_mark.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using namespace disruptor::game_mark;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() /
                          ("disruptor-game-mark-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path user = root / "Player Files";
    const fs::path other = root / "other";
    fs::create_directories(user / "inner");
    fs::create_directories(other);

    expect(!marked(user), "a folder no game has marked is free");
    {
        std::optional<Mark> first(std::in_place, user);
        expect(marked(user), "a running game marks its folder");
        expect(!marked(other), "a game marks only its own folder");
        {
            const Mark second(user / "inner" / "..");
            expect(marked(user), "a second game on the folder, written another way, marks it too");
        }
        expect(marked(user), "the folder stays marked while one game is left");
        first.reset();
        expect(!marked(user), "the mark goes with the last game");
    }
    {
        const Mark again(user);
        expect(marked(user / "inner" / ".."), "the folder written another way is seen as marked");
    }
    expect(!marked(user), "a folder is free again after its game");
    expect(!marked(root / "missing"), "a folder that does not exist is free");

    std::error_code ignored;
    fs::remove_all(root, ignored);
    if (g_failures == 0) std::cout << "game mark: PASS\n";
    return g_failures == 0 ? 0 : 1;
}
