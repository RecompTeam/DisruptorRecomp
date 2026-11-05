#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/* The screens of saves: LOAD GAME with the game's four saved games over the eleven save state slots F1 to F11, and the
 * pause's grid of the slots alone. What a key or a click does there, where the cards go, and the screens' own words in
 * the language disc's tongue, letters and digits only as the game's small font has. */
namespace disruptor::state_browser {

inline constexpr int kSlots = 11, kColumns = 4, kRows = 3;

enum class Word {
    title, empty, load, save, erase, back, yes, no, today, yesterday, main_menu, other_build, hardcore, regular, hard,
    mission, ask_load, ask_save, ask_erase, progress_lost, saving, loading, cross, circle, square, triangle, shift_enter,
    start
};

/* `{}` stands for the slot's key. French and German without accents: the font has none. */
inline std::string_view word(Word which, std::string_view language) {
    struct Words {
        std::string_view english, french, german;
    };
    constexpr std::array<Words, 28> words = {{
        {"SAVE STATES", "SAUVEGARDES RAPIDES", "SCHNELLSPEICHER"},
        {"EMPTY", "VIDE", "LEER"},
        {"LOAD", "CHARGER", "LADEN"},
        {"SAVE", "SAUVER", "SPEICHERN"},
        {"DELETE", "EFFACER", "LOESCHEN"},
        {"BACK", "RETOUR", "ZURUECK"},
        {"YES", "OUI", "JA"},
        {"NO", "NON", "NEIN"},
        {"TODAY", "AUJOURD HUI", "HEUTE"},
        {"YESTERDAY", "HIER", "GESTERN"},
        {"MAIN MENU", "MENU PRINCIPAL", "HAUPTMENUE"},
        {"OTHER BUILD", "AUTRE VERSION", "ANDERE VERSION"},
        {"HARDCORE KEEPS SAVE STATES FROM LOADING", "LE MODE HARDCORE EMPECHE LE CHARGEMENT", "HARDCORE LAESST NICHTS LADEN"},
        {"REGULAR", "NORMAL", "NORMAL"},
        {"HARD", "DIFFICILE", "SCHWER"},
        {"MISSION", "MISSION", "MISSION"},
        {"LOAD {}", "CHARGER {}", "{} LADEN"},
        {"OVERWRITE {}", "REMPLACER {}", "{} UEBERSCHREIBEN"},
        {"DELETE {}", "EFFACER {}", "{} LOESCHEN"},
        {"PLAY SINCE THE LAST SAVE IS LOST", "LE JEU DEPUIS LA DERNIERE SAUVEGARDE EST PERDU", "DAS SPIEL SEIT DEM LETZTEN SPEICHERN GEHT VERLOREN"},
        {"SAVING", "SAUVEGARDE", "SPEICHERT"},
        {"LOADING", "CHARGEMENT", "LAEDT"},
        {"CROSS", "CROIX", "KREUZ"},
        {"CIRCLE", "ROND", "KREIS"},
        {"SQUARE", "CARRE", "QUADRAT"},
        {"TRIANGLE", "TRIANGLE", "DREIECK"},
        {"SHIFT ENTER", "MAJ ENTREE", "UMSCHALT EINGABE"},
        {"START", "COMMENCER", "STARTEN"},
    }};
    const Words &known = words[static_cast<size_t>(which)];
    return language == "French" ? known.french : language == "German" ? known.german : known.english;
}

inline std::string with_key(Word which, std::string_view key, std::string_view language) {
    std::string text(word(which, language));
    const size_t at = text.find("{}");
    if (at != std::string::npos) text.replace(at, 2, key);
    return text;
}

inline std::string key_of(int slot) { return "F" + std::to_string(slot + 1); }

/* The US disc's names. A language disc's come from its menu text when the front end showed it. */
inline constexpr std::array<std::string_view, 13> kMissionNames = {
    "THE GAUNTLET", "CHEMICAL FACTORY", "ROOFTOPS", "JUPITER STATION", "TRITON",  "MARS",    "ANTARCTICA",
    "IO",           "REACTOR",          "ORBITING HABITAT", "DREAM",  "PRISON",  "FORTRESS"};

/* Only what the small font draws: capitals, digits, the colon and spaces. */
inline bool plain(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char letter) {
        return (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == ' ' || letter == ':';
    });
}

inline std::string upper(std::string_view text) {
    std::string out(text);
    for (char &letter : out)
        if (letter >= 'a' && letter <= 'z') letter = static_cast<char>(letter - 'a' + 'A');
    return out;
}

/* A mission's name, the number when the name is unknown or has a letter the font lacks. */
inline std::string mission_name(uint32_t mission, std::string_view known, std::string_view language) {
    const std::string name = upper(known);
    if (plain(name)) return name;
    if (language.empty() && mission >= 1u && mission <= kMissionNames.size()) return std::string(kMissionNames[mission - 1u]);
    return std::string(word(Word::mission, language)) + " " + std::to_string(mission);
}

/* A slot's lines under its picture: its key and where it was saved, the difficulty or why it does not load, and when. */
struct Caption {
    bool present = false, loads = false, menus = false, regular = true;
    std::string place, saved;
};

inline std::array<std::string, 3> caption(int slot, const Caption &card, std::string_view language) {
    const std::string key = key_of(slot);
    if (!card.present) return {key, std::string(word(Word::empty, language)), {}};
    const std::string place = card.menus ? std::string(word(Word::main_menu, language)) : card.place;
    std::string second;
    if (!card.loads) second = word(Word::other_build, language);
    else if (!card.menus) second = word(card.regular ? Word::regular : Word::hard, language);
    if (second.empty()) return {key + " " + place, card.saved, {}};
    return {key + " " + place, second, card.saved};
}

inline std::string two_digits(int value) { return (value < 10 ? "0" : "") + std::to_string(value); }

struct Date {
    int year = 0, month = 1, day = 1, hour = 0, minute = 0;
};

/* When a slot was saved: today and yesterday by name, the year only when it is not this one. */
inline std::string when(const Date &saved, int days_ago, int this_year, std::string_view language) {
    constexpr std::array<std::array<std::string_view, 12>, 3> months = {{
        {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"},
        {"JANV", "FEVR", "MARS", "AVR", "MAI", "JUIN", "JUIL", "AOUT", "SEPT", "OCT", "NOV", "DEC"},
        {"JAN", "FEB", "MAERZ", "APR", "MAI", "JUNI", "JULI", "AUG", "SEPT", "OKT", "NOV", "DEZ"},
    }};
    const std::string time = two_digits(saved.hour) + ":" + two_digits(saved.minute);
    if (days_ago == 0) return std::string(word(Word::today, language)) + " " + time;
    if (days_ago == 1) return std::string(word(Word::yesterday, language)) + " " + time;
    const size_t tongue = language == "French" ? 1u : language == "German" ? 2u : 0u;
    const std::string_view month = months[tongue][static_cast<size_t>(std::clamp(saved.month, 1, 12) - 1)];
    std::string text = std::to_string(saved.day) + " " + std::string(month);
    if (saved.year != this_year) text += " " + std::to_string(saved.year);
    return text + " " + time;
}

enum class Move { up, down, left, right };
enum class Place { front_end, pause };

/* A cell: a save state's slot 0 to 10, one of the game's four saved games from kGames, or RETURN TO MAIN MENU. */
inline constexpr int kGames = 100, kGameSlots = 4, kBack = 200;

constexpr bool is_state(int cell) { return cell >= 0 && cell < kSlots; }
constexpr bool is_game(int cell) { return cell >= kGames && cell < kGames + kGameSlots; }

/* The rows of cells a place shows, top down: LOAD GAME's saved games, its slots in two rows of six, RETURN; the pause's
 * slots four by three. */
inline std::vector<std::vector<int>> rows_of(Place place) {
    if (place == Place::pause) return {{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10}};
    return {{kGames, kGames + 1, kGames + 2, kGames + 3}, {0, 1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}, {kBack}};
}

/* The next cell: a row goes round, and a step up or down lands on the cell of the next row nearest by place, rows going
 * round too. */
inline int step(int cell, Move move, Place place) {
    const std::vector<std::vector<int>> rows = rows_of(place);
    for (size_t r = 0; r < rows.size(); ++r) {
        const std::vector<int> &row = rows[r];
        for (size_t i = 0; i < row.size(); ++i) {
            if (row[i] != cell) continue;
            const size_t wide = row.size();
            if (move == Move::left) return row[(i + wide - 1) % wide];
            if (move == Move::right) return row[(i + 1) % wide];
            const std::vector<int> &to = rows[move == Move::up ? (r + rows.size() - 1) % rows.size() : (r + 1) % rows.size()];
            const double at = (static_cast<double>(i) + 0.5) * static_cast<double>(to.size()) / static_cast<double>(wide) - 0.5;
            return to[static_cast<size_t>(std::clamp<long>(std::lround(at), 0L, static_cast<long>(to.size()) - 1))];
        }
    }
    return rows.front().front();
}

/* less and more step a saved game's mission, elsewhere they move as left and right. */
enum class Key { up, down, left, right, choose, save, erase, back, less, more };
enum class Ask { none, load, save, erase };
enum class Do { nothing, load, save, erase, close, play, leave };

struct Slot {
    bool present = false;
    bool loads = false; /* this build's: a state of another build stays where it is */
};

/* One of the game's saved games: whether its slot holds one, the mission it goes on from and the furthest reached. */
struct Game {
    bool present = false;
    uint32_t level = 1, most = 1;
};

struct Outcome {
    Do act = Do::nothing;
    int slot = -1;
    uint32_t mission = 0;
};

/* The cursor, the question it asks and what a key, a click or an answer does. On LOAD GAME a state loads at once,
 * nothing is lost there, a saved game starts the reached mission its card shows, and back is RETURN TO MAIN MENU. In the
 * pause a load and an overwrite ask first, an empty card saves, and back closes the screen. */
struct Model {
    Place place = Place::front_end;
    int cursor = 0;
    Ask asking = Ask::none;
    bool yes = false;
    bool loads_refused = false;
    bool read = false; /* the slots' files are read: until then every card shows empty */
    std::array<uint32_t, kGameSlots> missions{}; /* a saved game's stepped mission, 0 for the one it was left on */

    using States = std::array<Slot, kSlots>;
    using Games = std::array<Game, kGameSlots>;

    bool may_load(const Slot &slot) const { return slot.present && slot.loads && !loads_refused; }

    uint32_t mission_of(int game, const Games &games) const {
        const Game &saved = games[static_cast<size_t>(game)];
        const uint32_t stepped = missions[static_cast<size_t>(game)];
        return std::clamp(stepped != 0u ? stepped : saved.level, 1u, std::max(saved.most, 1u));
    }

    /* Through the reached missions, round again. False for a saved game with one. */
    bool step_mission(int game, int by, const Games &games) {
        const Game &saved = games[static_cast<size_t>(game)];
        if (!saved.present || saved.most <= 1u) return false;
        const uint32_t now = mission_of(game, games);
        missions[static_cast<size_t>(game)] = by < 0 ? (now > 1u ? now - 1u : saved.most) : (now < saved.most ? now + 1u : 1u);
        return true;
    }

    Outcome ask(Ask question) {
        asking = question;
        yes = false;
        return {};
    }

    Outcome choose(const States &states, const Games &games) {
        if (cursor == kBack) return {Do::leave, -1};
        if (is_game(cursor)) {
            const Game &game = games[static_cast<size_t>(cursor - kGames)];
            if (!game.present) return {};
            return {Do::play, cursor - kGames, mission_of(cursor - kGames, games)};
        }
        if (!is_state(cursor)) return {};
        const Slot &slot = states[static_cast<size_t>(cursor)];
        if (place == Place::pause && !slot.present) return {Do::save, cursor};
        if (!may_load(slot)) return {};
        if (place == Place::pause) return ask(Ask::load);
        return {Do::load, cursor};
    }

    Outcome save(const States &states) {
        if (place != Place::pause || !is_state(cursor)) return {};
        if (states[static_cast<size_t>(cursor)].present) return ask(Ask::save);
        return {Do::save, cursor};
    }

    Outcome erase(const States &states) {
        return is_state(cursor) && states[static_cast<size_t>(cursor)].present ? ask(Ask::erase) : Outcome{};
    }

    Outcome answer(bool agreed) {
        const Ask question = asking;
        asking = Ask::none;
        if (!agreed) return {};
        switch (question) {
        case Ask::load: return {Do::load, cursor};
        case Ask::save: return {Do::save, cursor};
        case Ask::erase: return {Do::erase, cursor};
        case Ask::none: break;
        }
        return {};
    }

    Outcome press(Key key, const States &states, const Games &games = {}) {
        if (!read && key != Key::back) return {};
        if (asking != Ask::none) {
            switch (key) {
            case Key::left:
            case Key::right:
            case Key::less:
            case Key::more: yes = !yes; return {};
            case Key::choose: return answer(yes);
            case Key::back: return answer(false);
            default: return {};
            }
        }
        switch (key) {
        case Key::up: cursor = step(cursor, Move::up, place); return {};
        case Key::down: cursor = step(cursor, Move::down, place); return {};
        case Key::left: cursor = step(cursor, Move::left, place); return {};
        case Key::right: cursor = step(cursor, Move::right, place); return {};
        case Key::choose: return choose(states, games);
        case Key::save: return save(states);
        case Key::erase: return erase(states);
        case Key::back: return place == Place::front_end ? Outcome{Do::leave, -1} : Outcome{Do::close, -1};
        case Key::less:
        case Key::more:
            if (is_game(cursor)) step_mission(cursor - kGames, key == Key::less ? -1 : 1, games);
            else cursor = step(cursor, key == Key::less ? Move::left : Move::right, place);
            return {};
        }
        return {};
    }

    /* A click on a cell: it is chosen as a press of its key would choose it. */
    Outcome click(int cell, const States &states, const Games &games = {}) {
        const bool here = is_state(cell) || (place == Place::front_end && (is_game(cell) || cell == kBack));
        if (!read || asking != Ask::none || !here) return {};
        cursor = cell;
        return choose(states, games);
    }

    /* A click on a saved game's arrow: the game is chosen and its mission steps. */
    void click_step(int game, int by, const Games &games) {
        if (!read || asking != Ask::none || place != Place::front_end || game < 0 || game >= kGameSlots) return;
        cursor = kGames + game;
        step_mission(game, by, games);
    }
};

/* A state or a saved game chosen on the screen goes through black: it is asked for once all is dark, one that never comes
 * lightens back to the screen. Times in milliseconds. */
inline constexpr double kDarkenMs = 200.0, kLightenMs = 300.0, kDarkMostMs = 3000.0;

struct LoadFade {
    enum class Step { none, darkening, dark, lightening };
    enum class Now { nothing, ask, landed, missed };
    Step step = Step::none;
    int slot = -1;
    double since = 0.0;

    void start(int chosen, double now) {
        step = Step::darkening;
        slot = chosen;
        since = now;
    }
    bool active() const { return step != Step::none; }
    bool holds() const { return step == Step::darkening || step == Step::dark; }

    Now tick(double now, bool landed, bool ended) {
        switch (step) {
        case Step::darkening:
            if (now - since < kDarkenMs) return Now::nothing;
            step = Step::dark;
            since = now;
            return Now::ask;
        case Step::dark:
            if (!landed && !ended && now - since < kDarkMostMs) return Now::nothing;
            step = Step::lightening;
            since = now;
            return landed ? Now::landed : Now::missed;
        case Step::lightening:
            if (now - since >= kLightenMs) step = Step::none;
            return Now::nothing;
        case Step::none: break;
        }
        return Now::nothing;
    }

    float darkness(double now) const {
        const auto eased = [](double share) {
            share = std::clamp(share, 0.0, 1.0);
            return static_cast<float>(share * share * (3.0 - 2.0 * share));
        };
        switch (step) {
        case Step::darkening: return eased((now - since) / kDarkenMs);
        case Step::dark: return 1.0f;
        case Step::lightening: return eased(1.0 - (now - since) / kLightenMs);
        case Step::none: break;
        }
        return 0.0f;
    }
};

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

/* A slot's three lines of 8 texels under its picture, with their gaps. */
inline constexpr int kCardLines = 2 + 8 + 2 + 8 + 2 + 8;

/* Where the screen's parts go in a window of `width` by `height` pixels: the cards' pictures at `aspect`, three lines of
 * text under each, the title above and the keys' line below. Letters are 8 texels high, `texel` pixels each. */
struct Layout {
    int texel = 1;
    Rect title, footer;
    std::array<Rect, kSlots> cards{}, pictures{};
};

inline Layout layout(int width, int height, float aspect) {
    Layout out;
    if (width <= 0 || height <= 0) return out;
    aspect = std::clamp(aspect, 4.0f / 3.0f, 21.0f / 9.0f);
    out.texel = std::max(1, static_cast<int>(std::lround(height / 540.0)));
    const float t = static_cast<float>(out.texel), line = 8.0f * t, gap = 6.0f * t;
    const float title_high = 2.0f * line + 2.0f * gap, footer_high = line + 2.0f * gap;
    const float text_high = static_cast<float>(kCardLines) * t;
    const float room_wide = static_cast<float>(width) * 0.94f, room_high = static_cast<float>(height) - title_high - footer_high - 2.0f * gap;
    const float by_width = (room_wide - 3.0f * gap) / kColumns;
    const float by_height = ((room_high - 2.0f * gap) / kRows - text_high) * aspect;
    const float picture_wide = std::max(16.0f, std::floor(std::min(by_width, by_height)));
    const float picture_high = std::floor(picture_wide / aspect);
    const float card_high = picture_high + text_high;
    const float grid_wide = kColumns * picture_wide + 3.0f * gap, grid_high = kRows * card_high + 2.0f * gap;
    const float left = std::floor((static_cast<float>(width) - grid_wide) / 2.0f);
    const float top = std::floor(title_high + gap + std::max(0.0f, (room_high - grid_high) / 2.0f));
    out.title = {left, gap, grid_wide, 2.0f * line};
    out.footer = {left, static_cast<float>(height) - footer_high + gap, grid_wide, line};
    for (int cell = 0; cell < kSlots; ++cell) {
        const float inset = cell / kColumns == kRows - 1 ? std::floor((picture_wide + gap) / 2.0f) : 0.0f; /* the last three in the middle */
        const float x = left + inset + static_cast<float>(cell % kColumns) * (picture_wide + gap);
        const float y = top + static_cast<float>(cell / kColumns) * (card_high + gap);
        out.cards[static_cast<size_t>(cell)] = {x, y, picture_wide, card_high};
        out.pictures[static_cast<size_t>(cell)] = {x, y, picture_wide, picture_high};
    }
    return out;
}

/* The part of a picture that fills a box of `aspect` without bars, as texture coordinates: its middle. */
struct Crop {
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};

inline Crop cover(float picture_aspect, float box_aspect) {
    Crop crop;
    if (picture_aspect <= 0.0f || box_aspect <= 0.0f) return crop;
    if (picture_aspect > box_aspect) {
        crop.u0 = (1.0f - box_aspect / picture_aspect) / 2.0f;
        crop.u1 = 1.0f - crop.u0;
    } else {
        crop.v0 = (1.0f - picture_aspect / box_aspect) / 2.0f;
        crop.v1 = 1.0f - crop.v0;
    }
    return crop;
}

/* The card under a point, -1 for none. */
inline int card_at(const Layout &shape, float x, float y) {
    for (int cell = 0; cell < kSlots; ++cell)
        if (shape.cards[static_cast<size_t>(cell)].contains(x, y)) return cell;
    return -1;
}

/* LOAD GAME in a window, its places in the 240 rows of the front end's frame, `top` the window's row of the frame's first
 * and `unit` a frame row's height in pixels: under the logo the title and the saved games, SAVE STATES as large as the
 * title, the slots in two rows of six as wide as the saved games' row, the keys' line and RETURN TO MAIN MENU at the
 * bottom. `texel` is the small letters' and the cards' big letters', `big` the titles' and RETURN's, as large as the
 * game's. A slot's picture fills its card: wider than the frame its middle rows show. */
struct FrontLayout {
    int texel = 1, big = 1, icon = 1, caption = 1; /* caption: the slots' lines', smaller where the longest does not fit */
    float title = 0.0f, states_title = 0.0f, keys = 0.0f, back = 0.0f; /* rows in pixels */
    float game_top = 0.0f;                                              /* a saved game's icon under its card's top */
    std::array<Rect, kGameSlots> games{};
    std::array<Rect, kSlots> cards{}, pictures{};
};

inline constexpr float kFrontTitle = 58.0f, kFrontGames = 78.0f, kFrontGamesEnd = 114.0f, kFrontStatesTitle = 118.0f,
                       kFrontStates = 136.0f, kFrontStatesEnd = 206.0f, kFrontKeys = 209.0f, kFrontBack = 218.0f;
inline constexpr int kIconWide = 24, kIconHigh = 18, kBigHigh = 14;
/* A saved game's lines under its icon: the name and the mission in the menu font, the difficulty in the small one. */
inline constexpr int kGameLines = 2 + kBigHigh + 1 + kBigHigh + 2 + 8;
inline constexpr float kWidestCard = 3.5f;
/* The longest slot line in the game's widths: F10 CHEMICAL FACTORY, and a German date with its year. */
inline constexpr int kCaptionTexels = 122;

inline FrontLayout front_layout(int width, float top, float unit, float aspect) {
    FrontLayout out;
    if (width <= 0 || unit <= 0.0f) return out;
    aspect = std::clamp(aspect, 4.0f / 3.0f, 21.0f / 9.0f);
    const auto row = [top, unit](float frame) { return std::floor(top + frame * unit); };
    out.texel = std::max(1, static_cast<int>(std::lround(unit / 2.25f)));
    out.big = std::max(1, static_cast<int>(std::floor(unit)));
    const float t = static_cast<float>(out.texel), gap = 6.0f * t, room = static_cast<float>(width) * 0.96f;
    out.title = row(kFrontTitle);
    out.states_title = row(kFrontStatesTitle);
    out.keys = row(kFrontKeys);
    out.back = row(kFrontBack);
    const float games_top = row(kFrontGames), games_high = row(kFrontGamesEnd) - games_top;
    /* At least 3 texels above the icon and 4 under the difficulty, clear of the chosen card's 2 texel frame. */
    out.icon = std::max(1, static_cast<int>(std::floor((games_high - static_cast<float>(kGameLines + 7) * t) / kIconHigh)));
    const float spare = games_high - static_cast<float>(kIconHigh * out.icon) - static_cast<float>(kGameLines + 7) * t;
    out.game_top = 3.0f * t + std::max(0.0f, std::floor(spare / 2.0f));
    const float game_wide = std::floor((room - 3.0f * gap) / kGameSlots);
    const float games_left = std::floor((static_cast<float>(width) - (kGameSlots * game_wide + 3.0f * gap)) / 2.0f);
    for (int slot = 0; slot < kGameSlots; ++slot)
        out.games[static_cast<size_t>(slot)] = {games_left + static_cast<float>(slot) * (game_wide + gap), games_top, game_wide, games_high};
    const float states_top = row(kFrontStates), states_high = row(kFrontStatesEnd) - states_top;
    const float card_high = std::floor((states_high - gap) / 2.0f), card_wide = std::floor((room - 5.0f * gap) / 6.0f);
    out.caption = out.texel;
    while (out.caption > 1 && static_cast<float>(kCaptionTexels * out.caption) > card_wide) --out.caption;
    const float caption = static_cast<float>(out.caption), text_high = static_cast<float>(kCardLines) * caption;
    const float picture_high = std::max(8.0f, std::min(card_high - text_high, std::floor(card_wide / aspect)));
    const float lines_wide = static_cast<float>(kCaptionTexels) * caption;
    const float picture_wide = std::max(16.0f, std::min(card_wide, std::max(lines_wide, std::floor(picture_high * kWidestCard))));
    /* The second row's five stand between the first's six, half a slot in from each end. */
    const float states_left = std::floor((static_cast<float>(width) - (6.0f * picture_wide + 5.0f * gap)) / 2.0f);
    for (int cell = 0; cell < kSlots; ++cell) {
        const float inset = cell < 6 ? 0.0f : std::floor((picture_wide + gap) / 2.0f);
        const float x = states_left + inset + static_cast<float>(cell % 6) * (picture_wide + gap);
        const float y = states_top + static_cast<float>(cell / 6) * (card_high + gap);
        out.cards[static_cast<size_t>(cell)] = {x, y, picture_wide, picture_high + text_high};
        out.pictures[static_cast<size_t>(cell)] = {x, y, picture_wide, picture_high};
    }
    return out;
}

/* The cell of LOAD GAME under a point: a saved game, a slot, RETURN in `back`, -1 for none. */
inline int front_cell_at(const FrontLayout &shape, const Rect &back, float x, float y) {
    for (int slot = 0; slot < kGameSlots; ++slot)
        if (shape.games[static_cast<size_t>(slot)].contains(x, y)) return kGames + slot;
    for (int cell = 0; cell < kSlots; ++cell)
        if (shape.cards[static_cast<size_t>(cell)].contains(x, y)) return cell;
    return back.contains(x, y) ? kBack : -1;
}

}  // namespace disruptor::state_browser
