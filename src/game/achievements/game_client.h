#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a hardcore session may refuse, for disruptor_achievements_allows. */
enum {
    DISRUPTOR_ACHIEVEMENTS_LOAD_STATE = 0,
    DISRUPTOR_ACHIEVEMENTS_CHEAT = 1,
    DISRUPTOR_ACHIEVEMENTS_START_MISSION = 2,
};

/* At the settings' load: whether the player keeps RetroAchievements on for this run. */
void disruptor_achievements_configure(int enabled);
/* Once a guest VBlank after the pad is read: signs in and finds the game on the first ones, then checks achievements. */
void disruptor_achievements_frame(void);
/* A line for the menu: off, not signed in, or the game with how many achievements are unlocked. Any thread. */
void disruptor_achievements_status(char *line, int size);
int disruptor_achievements_enabled(void);
int disruptor_achievements_hardcore(void);
/* Any thread. Zero when a hardcore session refuses the act, which the player is then told on screen. */
int disruptor_achievements_allows(int act);
/* On the emulation thread, around a save state at `state`: its achievement progress is kept in a file beside it. */
void disruptor_achievements_state_saving(const char *state);
void disruptor_achievements_state_saved(const char *state, int written);
void disruptor_achievements_state_loaded(const char *state);
/* Any thread: forgets the account and ends the session at the next frame. */
void disruptor_achievements_sign_out(void);
/* The audio thread: mixes the unlock sound into the game's output. */
void disruptor_achievements_audio(int16_t *stereo, int frames, int rate);

#ifdef __cplusplus
}

#include "game/achievements/session.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PSXRecompV4 { struct GameUserSettings; }

namespace disruptor::achievements {

// How unlocks are shown, from the settings.
struct Presentation {
    bool popups = true;
    int seconds = 12;
    int corner = 0;  // an index into kDisruptorPopupCorners
    bool progress = false;
    bool sound = true;
};

// The settings' achievements fields other than the switch itself, at their load and whenever one changes. Encore
// and unofficial achievements take effect when a session starts.
void configure(const PSXRecompV4::GameUserSettings& settings);
[[nodiscard]] Presentation presentation();
// What to show since the last call, oldest first. The render thread.
[[nodiscard]] std::vector<Notice> take_popups();
// The achievement whose count moved last, while its indicator is up.
[[nodiscard]] std::optional<Notice> progress_indicator();
// The set for the menu's list, kept fresh while the list is drawn. Any thread.
[[nodiscard]] std::vector<Entry> achievement_list();
// A picture's PNG once fetched or read from the folder, otherwise null. Any thread.
[[nodiscard]] std::shared_ptr<const std::string> badge(const std::string& url);
// The name the server knows the player by, once signed in this run, otherwise empty. Any thread.
[[nodiscard]] std::string player();

} // namespace disruptor::achievements
#endif
