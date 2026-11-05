#include "game/ui/state_browser.h"

#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

namespace {

namespace browser = disruptor::state_browser;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

std::array<browser::Slot, browser::kSlots> slots_with(std::initializer_list<int> present, std::initializer_list<int> others = {}) {
    std::array<browser::Slot, browser::kSlots> slots{};
    for (const int slot : present) slots[static_cast<size_t>(slot)] = {true, true};
    for (const int slot : others) slots[static_cast<size_t>(slot)] = {true, false};
    return slots;
}

void test_words() {
    expect(browser::word(browser::Word::title, "") == "SAVE STATES" && browser::word(browser::Word::title, "German") == "SCHNELLSPEICHER",
           "the title in the disc's language");
    expect(browser::with_key(browser::Word::ask_load, "F3", "") == "LOAD F3" && browser::with_key(browser::Word::ask_erase, "F3", "German") == "F3 LOESCHEN",
           "a question names the slot by its key where the language puts it");
    for (int which = 0; which <= static_cast<int>(browser::Word::shift_enter); ++which)
        for (const char *language : {"", "French", "German"}) {
            const std::string text = browser::with_key(static_cast<browser::Word>(which), "F1", language);
            expect(browser::plain(text), "every word is in letters the small font has");
        }
    expect(browser::key_of(0) == "F1" && browser::key_of(10) == "F11", "slots are named by the keys that load them");
}

void test_missions_and_dates() {
    expect(browser::mission_name(3u, "", "") == "ROOFTOPS", "the US disc's name for mission 3");
    expect(browser::mission_name(3u, "Les toits", "French") == "LES TOITS", "a language disc's own name in capitals");
    expect(browser::mission_name(3u, "R\x8F" "VE", "French") == "MISSION 3", "a name with a letter the font lacks gives the number");
    expect(browser::mission_name(40u, "", "") == "MISSION 40", "an unknown mission gives its number");
    const browser::Date saved{2026, 11, 4, 9, 5};
    expect(browser::when(saved, 0, 2026, "") == "TODAY 09:05" && browser::when(saved, 1, 2026, "German") == "GESTERN 09:05",
           "today and yesterday by name");
    expect(browser::when(saved, 5, 2026, "") == "4 NOV 09:05" && browser::when(saved, 5, 2027, "French") == "4 NOV 2026 09:05",
           "older days by date, the year when it is not this one");
}

void test_grid() {
    using browser::Move;
    using browser::Place;
    const auto step = [](int cell, Move move, Place place = Place::pause) { return browser::step(cell, move, place); };
    expect(step(3, Move::right) == 0 && step(0, Move::left) == 3, "a row goes round");
    expect(step(10, Move::right) == 8 && step(8, Move::left) == 10, "the pause's last row has three cards");
    expect(step(7, Move::down) == 10 && step(10, Move::up) == 7, "down from the fourth column the nearest of the three");
    expect(step(9, Move::down) == 2 && step(0, Move::up) == 8, "rows go round, to the nearest card");
    const int games = browser::kGames;
    expect(step(games, Move::down, Place::front_end) == 0 && step(games + 3, Move::down, Place::front_end) == 5,
           "LOAD GAME: under the saved games the six slots, by place");
    expect(step(5, Move::down, Place::front_end) == 10 && step(10, Move::down, Place::front_end) == browser::kBack,
           "the second row of five, then RETURN");
    expect(step(browser::kBack, Move::down, Place::front_end) == games + 2 && step(browser::kBack, Move::up, Place::front_end) == 8,
           "RETURN goes round to the saved games, up to the middle slot");
    expect(step(games + 3, Move::right, Place::front_end) == games && step(browser::kBack, Move::left, Place::front_end) == browser::kBack,
           "a row goes round, RETURN stays");
    for (const Place place : {Place::pause, Place::front_end})
        for (const std::vector<int> &row : browser::rows_of(place))
            for (const int cell : row)
                for (const Move move : {Move::up, Move::down, Move::left, Move::right}) {
                    const int next = step(cell, move, place);
                    bool known = false;
                    for (const std::vector<int> &other : browser::rows_of(place))
                        for (const int each : other) known = known || each == next;
                    expect(known, "a step never leaves the place's cells");
                }
}

void test_front_end() {
    browser::Model model;
    const auto slots = slots_with({0, 2}, {1});
    expect(model.press(browser::Key::choose, slots).act == browser::Do::nothing && model.click(0, slots).act == browser::Do::nothing &&
               model.press(browser::Key::back, slots).act == browser::Do::leave,
           "before the slots are read only back is taken, and back leaves LOAD GAME");
    model.read = true;
    browser::Outcome out = model.press(browser::Key::choose, slots);
    expect(out.act == browser::Do::load && out.slot == 0, "on LOAD GAME a state loads at once");
    model.cursor = 1;
    expect(model.press(browser::Key::choose, slots).act == browser::Do::nothing, "another build's state does not load");
    model.cursor = 3;
    expect(model.press(browser::Key::choose, slots).act == browser::Do::nothing, "an empty card does nothing there");
    expect(model.press(browser::Key::save, slots).act == browser::Do::nothing, "and nothing saves there");
    model.cursor = 2;
    model.loads_refused = true;
    expect(model.press(browser::Key::choose, slots).act == browser::Do::nothing, "hardcore keeps a state from loading");
    model.loads_refused = false;
    expect(model.press(browser::Key::erase, slots).act == browser::Do::nothing && model.asking == browser::Ask::erase && !model.yes,
           "delete asks first, NO chosen");
    expect(model.press(browser::Key::choose, slots).act == browser::Do::nothing && model.asking == browser::Ask::none, "NO deletes nothing");
    model.press(browser::Key::erase, slots);
    model.press(browser::Key::right, slots);
    out = model.press(browser::Key::choose, slots);
    expect(out.act == browser::Do::erase && out.slot == 2, "YES deletes");
    model.press(browser::Key::erase, slots);
    expect(model.press(browser::Key::back, slots).act == browser::Do::nothing && model.asking == browser::Ask::none, "back answers NO");
    out = model.click(2, slots);
    expect(out.act == browser::Do::load && model.cursor == 2, "a click on a card loads it");
    model.cursor = browser::kBack;
    expect(model.press(browser::Key::choose, slots).act == browser::Do::leave, "RETURN leaves LOAD GAME");
}

void test_saved_games() {
    browser::Model model;
    model.read = true;
    const auto slots = slots_with({});
    browser::Model::Games games{};
    games[0] = {true, 3u, 5u};
    games[1] = {true, 1u, 1u};
    model.cursor = browser::kGames + 2;
    expect(model.press(browser::Key::choose, slots, games).act == browser::Do::nothing && model.asking == browser::Ask::none,
           "an empty saved game does nothing");
    model.cursor = browser::kGames + 1;
    browser::Outcome out = model.press(browser::Key::choose, slots, games);
    expect(out.act == browser::Do::play && out.slot == 1 && out.mission == 1u, "a saved game with one mission reached starts at once");
    model.cursor = browser::kGames;
    out = model.press(browser::Key::choose, slots, games);
    expect(out.act == browser::Do::play && out.slot == 0 && out.mission == 3u && model.asking == browser::Ask::none,
           "with more reached it starts at once too, at the mission it was left on");
    model.press(browser::Key::more, slots, games);
    model.press(browser::Key::more, slots, games);
    expect(model.mission_of(0, games) == 5u && model.cursor == browser::kGames, "more steps the card's mission up to the furthest, the cursor stays");
    model.press(browser::Key::more, slots, games);
    expect(model.mission_of(0, games) == 1u, "and goes round");
    model.press(browser::Key::less, slots, games);
    expect(model.mission_of(0, games) == 5u, "less goes back round");
    model.press(browser::Key::right, slots, games);
    expect(model.cursor == browser::kGames + 1 && model.mission_of(0, games) == 5u, "Right moves to the next saved game, the stepped mission stays");
    model.press(browser::Key::less, slots, games);
    expect(model.cursor == browser::kGames + 1 && model.mission_of(1, games) == 1u, "a saved game with one mission has nothing to step");
    model.cursor = browser::kGames;
    out = model.press(browser::Key::choose, slots, games);
    expect(out.act == browser::Do::play && out.slot == 0 && out.mission == 5u, "Cross starts the mission the card shows");
    model.click_step(0, 1, games);
    expect(model.cursor == browser::kGames && model.mission_of(0, games) == 1u, "a click on an arrow chooses the game and steps it");
    model.cursor = 3;
    model.press(browser::Key::more, slots, games);
    expect(model.cursor == 4 && model.mission_of(0, games) == 1u, "on the slots less and more move as Left and Right");
    expect(model.click(browser::kGames + 1, slots, games).act == browser::Do::play && model.cursor == browser::kGames + 1,
           "a click on a saved game chooses it");
    browser::Model pause;
    pause.place = browser::Place::pause;
    pause.read = true;
    expect(pause.click(browser::kGames, slots, games).act == browser::Do::nothing && pause.click(browser::kBack, slots, games).act == browser::Do::nothing &&
               pause.cursor == 0 && pause.asking == browser::Ask::none,
           "the pause has no saved games and no RETURN: a click there moves nothing and asks nothing");
}

void test_pause() {
    browser::Model model;
    model.place = browser::Place::pause;
    model.read = true;
    const auto slots = slots_with({0});
    expect(model.press(browser::Key::choose, slots).act == browser::Do::nothing && model.asking == browser::Ask::load,
           "in the pause a load asks first");
    model.answer(true);
    model.cursor = 0;
    expect(model.asking == browser::Ask::none, "an answer ends the question");
    model.press(browser::Key::choose, slots);
    model.yes = true;
    const browser::Outcome out = model.press(browser::Key::choose, slots);
    expect(out.act == browser::Do::load && out.slot == 0, "YES loads");
    model.cursor = 4;
    expect(model.press(browser::Key::choose, slots).act == browser::Do::save, "an empty card saves at once");
    expect(model.press(browser::Key::save, slots).act == browser::Do::save, "and so does save there");
    model.cursor = 0;
    expect(model.press(browser::Key::save, slots).act == browser::Do::nothing && model.asking == browser::Ask::save, "saving over a state asks");
    expect(model.click(1, slots).act == browser::Do::nothing && model.cursor == 0, "a question takes no click on a card");
    model.answer(false);
    expect(model.press(browser::Key::back, slots).act == browser::Do::close, "back closes the pause's screen");
}

void test_captions() {
    expect(browser::caption(1, {}, "") == std::array<std::string, 3>{"F2", "EMPTY", ""}, "an empty slot says so under its key");
    expect(browser::caption(3, {true, true, false, false, "TRITON", "2 NOV 10:31"}, "") ==
               std::array<std::string, 3>{"F4 TRITON", "HARD", "2 NOV 10:31"},
           "a state in play: its key and mission, the difficulty, when");
    expect(browser::caption(0, {true, true, true, true, "", "TODAY 09:05"}, "")[1] == "TODAY 09:05", "the main menu's has no difficulty");
    expect(browser::caption(0, {true, false, false, true, "MARS", "1 NOV 08:00"}, "French")[1] == "AUTRE VERSION",
           "another build's says so where the difficulty would be");
}

void test_load_fade() {
    using Fade = browser::LoadFade;
    Fade fade;
    expect(!fade.active() && fade.darkness(0.0) == 0.0f && fade.tick(0.0, true, true) == Fade::Now::nothing, "no fade until a load");
    fade.start(3, 1000.0);
    expect(fade.holds() && fade.darkness(1000.0) == 0.0f, "a chosen load begins clear and holds the screen");
    expect(fade.tick(1000.0 + browser::kDarkenMs - 1.0, true, true) == Fade::Now::nothing && fade.darkness(1100.0) > 0.0f &&
               fade.darkness(1100.0) < 1.0f,
           "nothing is asked while it darkens");
    expect(fade.tick(1200.0, false, false) == Fade::Now::ask && fade.slot == 3 && fade.darkness(1200.0) == 1.0f,
           "the load is asked once all is dark");
    expect(fade.tick(1300.0, false, false) == Fade::Now::nothing && fade.holds(), "the dark waits for the state");
    expect(fade.tick(1350.0, true, false) == Fade::Now::landed && !fade.holds() && fade.darkness(1350.0) == 1.0f,
           "the state came: the game lightens out of the dark");
    expect(fade.darkness(1350.0 + browser::kLightenMs / 2.0) > 0.0f && fade.darkness(1350.0 + browser::kLightenMs / 2.0) < 1.0f,
           "half way it is half dark");
    fade.tick(1350.0 + browser::kLightenMs, false, false);
    expect(!fade.active() && fade.darkness(2000.0) == 0.0f, "and then the fade is over");
    fade.start(1, 0.0);
    fade.tick(browser::kDarkenMs, false, false);
    expect(fade.tick(browser::kDarkenMs + 10.0, false, true) == Fade::Now::missed, "a load that came to nothing lightens back to the screen");
    fade.start(1, 0.0);
    fade.tick(browser::kDarkenMs, false, false);
    expect(fade.tick(browser::kDarkenMs + browser::kDarkMostMs, false, false) == Fade::Now::missed, "and so does one that never answers");
}

void test_layout() {
    const browser::Crop wide = browser::cover(4.0f / 3.0f, 16.0f / 9.0f);
    expect(wide.u0 == 0.0f && wide.u1 == 1.0f && std::abs(wide.v0 - 0.125f) < 1e-6f && std::abs(wide.v1 - 0.875f) < 1e-6f,
           "a 4:3 picture fills a 16:9 card, its middle three quarters of rows");
    const browser::Crop tall = browser::cover(21.0f / 9.0f, 16.0f / 9.0f);
    expect(tall.v0 == 0.0f && std::abs(tall.u0 - (1.0f - (16.0f / 9.0f) / (21.0f / 9.0f)) / 2.0f) < 1e-6f, "a wider one its middle columns");
    for (const auto &[width, height] : {std::pair{1920, 1080}, std::pair{1280, 720}, std::pair{3840, 2160}, std::pair{1024, 768}, std::pair{2560, 1080}}) {
        const browser::Layout shape = browser::layout(width, height, 16.0f / 9.0f);
        bool inside = true, apart = true;
        for (int cell = 0; cell < browser::kSlots; ++cell) {
            const browser::Rect &card = shape.cards[static_cast<size_t>(cell)];
            inside = inside && card.x >= 0.0f && card.y > shape.title.y + shape.title.h && card.x + card.w <= width &&
                     card.y + card.h <= shape.footer.y &&
                     card.h == shape.pictures[static_cast<size_t>(cell)].h + browser::kCardLines * static_cast<float>(shape.texel);
            if (cell % browser::kColumns != 0)
                apart = apart && card.x >= shape.cards[static_cast<size_t>(cell - 1)].x + shape.cards[static_cast<size_t>(cell - 1)].w;
        }
        expect(inside, "every card lies between the title and the keys' line, inside the window");
        expect(apart, "cards do not overlap");
        expect(browser::card_at(shape, shape.cards[5].x + 1.0f, shape.cards[5].y + 1.0f) == 5 && browser::card_at(shape, 0.0f, 0.0f) == -1,
               "a point finds its card");
        const float before = shape.cards[8].x - shape.cards[0].x, after = shape.cards[3].x + shape.cards[3].w - (shape.cards[10].x + shape.cards[10].w);
        expect(std::abs(before - after) <= 1.0f && before > 0.0f, "the pause's last row of three stands in the middle");
    }
    expect(browser::layout(1920, 1080, 4.0f / 3.0f).texel == 2 && browser::layout(3840, 2160, 4.0f / 3.0f).texel == 4,
           "letters grow with the window");
    for (const auto &[width, height] : {std::pair{1920, 1080}, std::pair{1280, 720}, std::pair{3840, 2160}, std::pair{1440, 1080}, std::pair{2560, 1080}, std::pair{2560, 1440}, std::pair{1080, 1440}, std::pair{1600, 900},
                                         std::pair{1366, 768}, std::pair{1440, 810}}) {
        const float unit = static_cast<float>(height) / 240.0f;
        const browser::FrontLayout shape = browser::front_layout(width, 0.0f, unit, 16.0f / 9.0f);
        const float t = static_cast<float>(shape.texel), big = 14.0f * static_cast<float>(shape.big);
        bool ordered = shape.title + big < shape.games[0].y && shape.games[0].y + shape.games[0].h < shape.states_title &&
                       shape.states_title + big < shape.cards[0].y && shape.cards[10].y + shape.cards[10].h <= shape.keys && shape.keys < shape.back &&
                       shape.back + big <= static_cast<float>(height);
        bool inside = true, apart = true, fits = true;
        for (int cell = 0; cell < browser::kSlots; ++cell) {
            const browser::Rect &card = shape.cards[static_cast<size_t>(cell)], &picture = shape.pictures[static_cast<size_t>(cell)];
            const float caption = static_cast<float>(shape.caption);
            inside = inside && card.x >= 0.0f && card.x + card.w <= width && card.h == picture.h + browser::kCardLines * caption;
            if (cell % 6 != 0) apart = apart && card.x >= shape.cards[static_cast<size_t>(cell - 1)].x + shape.cards[static_cast<size_t>(cell - 1)].w;
            if (cell >= 6) apart = apart && card.y >= shape.cards[static_cast<size_t>(cell - 6)].y + shape.cards[static_cast<size_t>(cell - 6)].h;
            if (cell >= 6) apart = apart && card.x > shape.cards[static_cast<size_t>(cell - 6)].x && card.x < shape.cards[static_cast<size_t>(cell - 5)].x;
            const float wide = picture.w / picture.h;
            const float lines_wide = static_cast<float>(browser::kCaptionTexels) * caption;
            fits = fits && wide >= 16.0f / 9.0f - 0.02f && wide <= std::max(browser::kWidestCard, lines_wide / picture.h) + 0.02f &&
                   card.w >= lines_wide && shape.caption <= shape.texel;
        }
        for (int slot = 0; slot < browser::kGameSlots; ++slot) {
            const browser::Rect &game = shape.games[static_cast<size_t>(slot)];
            inside = inside && game.x >= 0.0f && game.x + game.w <= width;
            const float content = static_cast<float>(browser::kIconHigh * shape.icon) + browser::kGameLines * t;
            inside = inside && shape.game_top >= 3.0f * t && shape.game_top + content + 4.0f * t <= game.h;
        }
        expect(ordered, "LOAD GAME: title, saved games, SAVE STATES, slots, keys and RETURN top down, RETURN inside the window");
        const float before = shape.cards[6].x - shape.cards[0].x, after = shape.cards[5].x + shape.cards[5].w - (shape.cards[10].x + shape.cards[10].w);
        expect(std::abs(before - after) <= 1.0f && before > 0.0f, "the second row of five stands in the middle of the first's six");
        expect(inside && apart, "its cards inside the window, apart, a saved game's icon and lines inside its card clear of the frame");
        expect(fits, "a slot's picture no narrower than the frame nor wider than the widest or its lines, which the longest fits");
        const bool common = (width == 1280 && height == 720) || (width == 1600 && height == 900) || (width == 1920 && height == 1080) ||
                            (width == 2560 && height == 1440) || (width == 3840 && height == 2160);
        if (common) expect(shape.caption == shape.texel, "at the usual 16:9 sizes a slot's lines are as large as the screen's other small letters");
        const browser::Rect back{100.0f, shape.back, 50.0f, 20.0f};
        expect(browser::front_cell_at(shape, back, shape.games[1].x + 1.0f, shape.games[1].y + 1.0f) == browser::kGames + 1 &&
                   browser::front_cell_at(shape, back, shape.cards[7].x + 1.0f, shape.cards[7].y + 1.0f) == 7 &&
                   browser::front_cell_at(shape, back, 101.0f, shape.back + 1.0f) == browser::kBack,
               "a point finds its saved game, slot or RETURN");
    }
}

}  // namespace

int main() {
    test_words();
    test_missions_and_dates();
    test_grid();
    test_front_end();
    test_saved_games();
    test_pause();
    test_captions();
    test_load_fade();
    test_layout();
    if (g_failures != 0) return 1;
    std::cout << "disruptor state browser: PASS\n";
    return 0;
}
