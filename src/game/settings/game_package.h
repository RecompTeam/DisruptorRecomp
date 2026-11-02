#ifndef DISRUPTOR_GAME_PACKAGE_H
#define DISRUPTOR_GAME_PACKAGE_H

namespace disruptor {

inline constexpr const char *kGamePackage = "disruptor.game";

/* Why a session cannot start, or nullptr. Without its own package the game would run with none of its fixes. */
inline const char *game_package_problem(bool netplay, bool package_active) {
    if (netplay || package_active) return nullptr;
    return "The game's own package is missing or turned off.\n"
           "It belongs in mods/bundled/disruptor.game beside the program.\n"
           "Reinstall the game or update it from the launcher.";
}

} // namespace disruptor

#endif
