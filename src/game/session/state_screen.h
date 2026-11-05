#pragma once

#include "game/input/menu_pointer.h"
#include "game/session/state_picture.h"
#include "game/ui/state_browser.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/* The save state screen's side in the game: the font it draws with and the pause's row, both read from the game, the
 * pictures kept beside the states and the cards read from the slots' files, off the threads that run and draw. */
namespace disruptor::state_screen {

/* The 8 by 8 font's glyph of a character, as 0x80044AE8 picks it: small letters, capitals, digits from 52, the colon 62. */
constexpr int tiny_glyph(char letter) {
    const int code = static_cast<unsigned char>(letter);
    if (code >= 'a') return code - 'a';
    if (code >= 'A') return code - 'A' + 26;
    if (code >= '0') return code - '0' + 52;
    return -1;
}

/* A font of the game's: each glyph's bytes are palette indices, 0 clear. The menu font picks its glyphs as the item draw
 * does, the 8 by 8 one as its own routine. */
struct Font {
    int wide = 0, high = 0, space = 4;
    bool menu = false;
    std::vector<uint8_t> glyphs;
    std::array<int8_t, 64> widths{};
    std::array<uint16_t, 256> palette{};

    bool ready() const { return wide > 0 && high > 0 && glyphs.size() == static_cast<size_t>(64 * wide * high); }
    int glyph(char letter) const {
        const int shape = menu ? menu_pointer::glyph(letter) : tiny_glyph(letter);
        return shape >= 0 && shape < 64 ? shape : -1;
    }
};

/* One of the game's save slots as LOAD GAME shows it: its byte (0 empty, 1 a saved game, 2 unavailable), the name and
 * the mission or EMPTY in the game's words and letters, the mission it goes on from and the furthest reached. */
struct GameSlot {
    uint8_t state = 0;
    std::string name, second;
    bool hard = false;
    uint32_t level = 1, most = 1;
};

/* What LOAD GAME would draw this frame, and what the screen needs to draw it so: the missions' names and icons, the
 * menu items' colours and the count they blink by. */
struct GameSaves {
    std::string title, back, empty, unavailable; /* the game's words for a slot without a save */
    bool message = false;
    std::array<GameSlot, 4> slots{};
    std::array<std::string, 13> missions{};
    std::array<state_files::Picture, 13> icons{};
    std::array<std::array<uint8_t, 3>, 8> shades{};
    uint32_t blink = 0;
};

/* What the screen asks LOAD GAME to do at its handler's next entry. */
struct GameAsk {
    enum class Kind { none, play, back } kind = Kind::none;
    int slot = 0;
    uint32_t mission = 1;
};

/* A run of texels of one colour along a row of the 320 by 240 frame. */
struct Run {
    int x = 0, y = 0, length = 0;
    uint8_t r = 0, g = 0, b = 0;
};

struct Card {
    bool present = false, loads = false;
    int64_t saved = 0;
    uint32_t mission = 0;
    uint8_t difficulty = 0;
    state_files::Place place = state_files::Place::unknown;
    state_files::Picture picture;
};

/* The emulation thread's side. */
void publish_font(Font font);
void publish_menu_font(Font font);
void publish_game_saves(GameSaves saves);
GameAsk take_game_ask();
/* A saved game asked for could not start: the screen opens again at once. */
void note_ask_refused();
void publish_pause_row(std::vector<Run> runs);
void note_mission_names(const std::array<std::string, 13> &names);
void ask_open(state_browser::Place place);
void note_context(state_browser::Place place);

/* The screen's side. While it is asked and not yet ready to open it holds the input already. LOAD GAME is the screen's
 * while it is ready to draw: the game's own is drawn only without it. */
void set_screen_ready(bool ready);
bool screen_ready();
Font font();
Font menu_font();
std::optional<GameSaves> game_saves();
/* The game drew another screen than LOAD GAME: its saves are no longer shown. */
void note_load_left();
void ask_game(GameAsk ask);
bool take_ask_refused();
std::vector<Run> pause_row();
bool opening();
std::optional<state_browser::Place> take_open();
bool context_alive(state_browser::Place place);
std::string mission_name(uint32_t mission);

/* Reads the slots again in the background. */
void reload();
/* The cards when a reading newer than `seen` ended. */
std::optional<std::array<Card, state_browser::kSlots>> cards_after(uint64_t &seen);

/* The slot's load, made on the emulation thread's next frame. */
void ask_load(int slot);
/* Taken once each: a state came in by any load, and the asked load came to nothing. */
bool take_landed();
bool take_load_ended();
/* Starts the slot's save, or deletes it with what lies beside it. False when it cannot be done now. */
bool save(int slot);
bool erase(int slot);
bool busy();

}  // namespace disruptor::state_screen

extern "C" {
void disruptor_state_screen_saving(const char *state);
void disruptor_state_screen_saved(const char *state, int written);
/* A state was loaded: a picture still to be taken would show it, not the state saved. */
void disruptor_state_screen_loaded(void);
/* Once a frame after the input: a picture asked for the screen is taken, the pause and the host's layer kept out of it. */
void disruptor_state_screen_frame(void);
/* While a picture is taken: the pause menu and everything the host draws over the game stay out of it. */
int disruptor_state_screen_hiding(void);
}
