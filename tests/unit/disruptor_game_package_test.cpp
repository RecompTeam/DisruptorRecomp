#include "game/settings/game_package.h"

#include <cstring>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

} // namespace

int main() {
    using disruptor::game_package_problem;
    expect(game_package_problem(false, true) == nullptr, "a session with the package starts");
    expect(game_package_problem(true, false) == nullptr, "a netplay session, which clears every package, starts");
    expect(game_package_problem(true, true) == nullptr, "a netplay session with the package starts");
    const char *problem = game_package_problem(false, false);
    expect(problem != nullptr, "a session without the package stops");
    expect(problem && std::strstr(problem, "mods/bundled/disruptor.game"), "the problem names where the package belongs");
    expect(std::strcmp(disruptor::kGamePackage, "disruptor.game") == 0, "the package id is the manifest's");
    if (g_failures) return 1;
    std::cout << "game package: ok\n";
    return 0;
}
