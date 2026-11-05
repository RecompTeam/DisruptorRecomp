#include "game/input/menu_pointer.h"
#include "game/input/menu_rows.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <string_view>
#include <vector>

namespace {

namespace rows = disruptor::menu_rows;
namespace pointer = disruptor::menu_pointer;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Guest {
    std::map<uint32_t, uint32_t> words;
    std::map<uint32_t, uint8_t> bytes;
    uint32_t read_word(uint32_t address) const {
        const auto found = words.find(address);
        return found == words.end() ? 0u : found->second;
    }
    uint8_t read_byte(uint32_t address) const {
        const auto found = bytes.find(address);
        return found == bytes.end() ? uint8_t{0} : found->second;
    }
};

constexpr uint32_t kText = 0x80100000u, kOffsets = 0x80110000u;

Guest with_menu_text() {
    Guest guest;
    guest.words[rows::kMenuText] = kText;
    guest.words[rows::kMenuOffsets] = kOffsets;
    guest.words[kOffsets + 180u] = 0x180u;
    guest.words[kOffsets + 184u] = 0x184u;
    return guest;
}

void test_placement() {
    expect(pointer::placed(0, 64, 100).x == 110 && pointer::placed(0, 64, 100).w == 100, "x 0 centres the sprite on 160");
    expect(pointer::placed(-208, 80, 50).x == 158, "a negative x is the sprite's right edge");
    expect(pointer::placed(220, 80, 50).x == 220 && pointer::placed(220, 80, 50).h == pointer::kRowHigh, "a positive x its left edge");
    const std::string_view text = "AB C";
    const int width = pointer::guest_text_width(0x80000000u, [&](uint32_t address) -> uint8_t {
        if (address >= pointer::kGlyphWidths) return 10u;
        const uint32_t at = address - 0x80000000u;
        return at < text.size() ? static_cast<uint8_t>(text[at]) : uint8_t{0};
    });
    expect(width == 36, "a guest string is as wide as its glyphs and spaces");
    expect(pointer::small_glyph('a') == 0 && pointer::small_glyph('A') == 26 && pointer::small_glyph('Z') == 51 &&
               pointer::small_glyph('0') == 52,
           "the pause font has its small letters first, then the capitals, then the rest from their codes");
    const int small = pointer::small_text_width("Ab 1", [](uint32_t address) -> uint8_t {
        const uint32_t shape = address - pointer::kSmallGlyphWidths;
        return shape == 26 ? 7 : shape == 1 ? 5 : shape == 53 ? 6 : 0;
    });
    expect(small == 7 + 5 + pointer::kSmallSpace + 6, "a pause line is as wide as its glyphs, a space four");
}

void test_table() {
    bool inside_screens = true, writes_known = true, told_apart = true;
    for (size_t index = 0; index < rows::kRows.size(); ++index) {
        const rows::Row &row = rows::kRows[index];
        inside_screens = inside_screens && (row.screen == rows::kPause ? rows::pause_draws(row.returns) : rows::draws_for(row.screen, row.returns));
        for (const rows::Write &write : row.writes)
            writes_known = writes_known && (write.address == 0u || write.address == rows::kCursor ||
                                            write.address == rows::kSlotSide || write.address == rows::kLetter ||
                                            (row.screen == rows::kPause && write.address == rows::kPauseCursor) ||
                                            (row.screen == rows::kPause && write.address == rows::kStates));
        for (size_t other = index + 1; other < rows::kRows.size(); ++other) {
            const rows::Row &next = rows::kRows[other];
            if (next.returns != row.returns || next.screen != row.screen) continue;
            told_apart = told_apart && (next.string != row.string || next.when[0].address != row.when[0].address ||
                                        next.when[0].low != row.when[0].low);
        }
    }
    expect(inside_screens, "every row is drawn from its own screen's function");
    expect(writes_known, "pointing writes only the cursors the screens move");
    expect(told_apart, "two rows of one call differ by the string drawn or a condition");
    bool states_left = true;
    for (const rows::Row &row : rows::kRows) {
        if (row.screen != rows::kPause) continue;
        const bool ours = row.returns == rows::kPauseStatesDrawn;
        bool on = false, off = false;
        for (const rows::Write &write : row.writes)
            if (write.address == rows::kStates) (write.value != 0 ? on : off) = true;
        const bool asks = row.when[0].address == rows::kPauseAsks && row.when[0].low != 0u;
        const bool steps = (row.left != 0u || row.right != 0u) && !asks;
        if (steps) states_left = states_left && (ours ? on && !off : off && !on);
    }
    expect(states_left, "SAVE STATES is chosen by its own row alone, and every other line of the pause leaves it");
    const rows::Row *pause_states = rows::match(rows::kPauseStatesDrawn, rows::kPause, 0u, 0u, Guest{});
    expect(pause_states && pause_states->row == rows::kPauseStatesRow && pause_states->writes[0].address == rows::kPauseCursor &&
               pause_states->writes[0].value == 0 && pause_states->left == rows::kCross,
           "the pause's SAVE STATES keeps the game's cursor on RESUME and a press there holds Cross");
    expect(!rows::draws_for(rows::kOptions, 0x8001C170u) && !rows::draws_for(rows::kDeath, 0x8001F394u),
           "the main menu's draw and another screen's are not this screen's");
}

void test_match() {
    Guest guest = with_menu_text();
    const rows::Row *volume = rows::match(0x8001F268u, rows::kOptions, 0u, 0u, guest);
    expect(volume && volume->row == 0 && volume->left == rows::kRight && volume->right == rows::kLeft,
           "a volume's bar steps it up with the left button and down with the right");
    expect(!rows::match(0x8001F268u, rows::kDeath, 0u, 0u, guest), "a row is its screen's only");
    const rows::Row *movie = rows::match(0x8001AE08u, rows::kDeath, 0u, kText + 0x184u, guest);
    const rows::Row *restart = rows::match(0x8001AE08u, rows::kDeath, 0u, kText + 0x180u, guest);
    expect(movie && movie->row == 2 && restart && restart->row == 1, "one call draws REVIEW MOVIE or RESTART MISSION, by its string");
    expect(!rows::match(0x8001AE08u, rows::kDeath, 0u, kText + 0x100u, guest), "and another string is neither");

    guest.bytes[rows::kSlots + 2u] = 1u;
    const rows::Row *mission = rows::match(0x8001D818u, rows::kLoadGame, 2u, 0u, guest);
    expect(mission && rows::row_of(*mission, 2u) == 2 && mission->left == rows::kLeft,
           "a saved game's mission is its slot's row, Left raises it");
    expect(!rows::match(0x8001D818u, rows::kLoadGame, 1u, 0u, guest), "an empty slot's word is no row");
    guest.words[rows::kLoadMessage] = 3u;
    expect(!rows::match(0x8001D818u, rows::kLoadGame, 2u, 0u, guest) && rows::match(0x8001D6F8u, rows::kLoadGame, 0u, 0u, guest),
           "while LOAD GAME shows a message only RETURN TO MAIN MENU is a row");

    guest.words[rows::kCursor] = 2u;
    const rows::Row *slots_back = rows::match(0x8001CA50u, rows::kNewGame, 0u, 0u, guest);
    expect(slots_back && slots_back->writes[0].address == rows::kSlotSide && slots_back->writes[0].value == 1,
           "beside the slots RETURN TO MAIN MENU is the slot side's");
    guest.words[rows::kCursor] = 6u;
    const rows::Row *name_back = rows::match(0x8001CA50u, rows::kNewGame, 0u, 0u, guest);
    expect(name_back && name_back->writes[0].address == rows::kCursor && name_back->writes[0].value == 7,
           "beside the name it is the cursor's seventh place");
    guest.words[rows::kCursor] = 2u;
    guest.words[rows::kOverwrite] = 1u;
    expect(!rows::match(0x8001CA50u, rows::kNewGame, 0u, 0u, guest) && !rows::match(0x8001CD64u, rows::kNewGame, 1u, 0u, guest),
           "while NEW GAME asks to overwrite, the slots and RETURN TO MAIN MENU are no rows");
    guest.words[rows::kCursor] = 4u;
    guest.words[rows::kOverwrite] = 2u;
    const rows::Row *difficulty_back = rows::match(0x8001CA50u, rows::kNewGame, 0u, 0u, guest);
    expect(difficulty_back && difficulty_back->writes[0].address == rows::kSlotSide && difficulty_back->writes[0].value == 1,
           "beside the difficulty, after YES to overwrite left the word at 2, RETURN TO MAIN MENU is the slot side's");

    Guest paused;
    const rows::Row *resume = rows::match(0x80042DE0u, rows::kPause, 0u, 0u, paused);
    const rows::Row *volume_line = rows::match(0x80042EB0u, rows::kPause, 0u, 0u, paused);
    expect(resume && resume->left == rows::kCross && resume->writes[0].address == rows::kPauseCursor && resume->writes[0].value == 0 &&
               volume_line && volume_line->left == rows::kRight && volume_line->right == rows::kLeft,
           "the pause menu's lines: RESUME MISSION chosen with Cross, a volume stepped");
    expect(!rows::match(0x80042C70u, rows::kPause, 0u, 0u, paused), "YES is no row while ABORT does not ask");
    paused.words[rows::kPauseAsks] = 1u;
    const rows::Row *no = rows::match(0x80042CA0u, rows::kPause, 0u, 0u, paused);
    expect(no && no->writes[0].value == 1 && !rows::match(0x80042DE0u, rows::kPause, 0u, 0u, paused),
           "while it asks NO is a row, the lines under the question are none");
    expect(!rows::match(0x80042DE0u, rows::kOptions, 0u, 0u, guest) && !rows::pause_draws(0x8001F394u) && rows::pause_draws(0x80042E1Cu),
           "the pause menu's rows are its own function's");
    guest.words[rows::kOverwrite] = 0u;
    guest.words[rows::kCursor] = 4u;
    expect(!rows::match(0x8001CD64u, rows::kNewGame, 1u, 0u, guest) && rows::match(0x8001CAD0u, rows::kNewGame, 0u, 0u, guest),
           "beside the difficulty a slot drawn a pass before is no row, the difficulty is");
    guest.words[rows::kCursor] = 2u;
    expect(!rows::match(0x8001CAD0u, rows::kNewGame, 0u, 0u, guest) && !rows::match(0x8001D06Cu, rows::kNewGame, 1u, 0u, guest) &&
               !rows::match(0x8001CE5Cu, rows::kNewGame, 0u, 0u, guest),
           "beside the slots the difficulty, the name and an answer not asked for are none");
    guest.words[rows::kCursor] = 5u;
    const rows::Row *letter = rows::match(0x8001D06Cu, rows::kNewGame, 5u, 0u, guest);
    expect(letter && rows::row_of(*letter, 5u) == 25 && rows::value_of(letter->writes[1], 5u) == 5u &&
               rows::value_of(letter->writes[0], 5u) == 5u && letter->left == rows::kUp && letter->right == rows::kDown,
           "a letter of the name is its own row, the counter its place, the buttons step it");
}

void test_hits() {
    const std::vector<rows::Drawn> drawn = {
        {0x8001F248u, 0, pointer::placed(-208, 80, 100), 0, 0},
        {0x8001F268u, 0, pointer::placed(220, 80, 30), rows::kRight, rows::kLeft},
        {0x8001F394u, 3, pointer::placed(0, 204, 120), rows::kCross, 0},
    };
    expect(rows::row_under(drawn, {214.0, 85.0}) == 0, "between a title and its value the pointer is on their row");
    expect(rows::row_under(drawn, {160.0, 210.0}) == 3 && !rows::row_under(drawn, {160.0, 150.0}), "and off every row on none");
    expect(rows::row_under(drawn, {160.0, 193.0}) == 3 && rows::row_under(drawn, {84.0, 86.0}) == 0,
           "eleven pixels above a row's letters, or 24 beside them, the pointer is still on it");
    const std::vector<rows::Drawn> close = {
        {0x8001C2D8u, 0, pointer::placed(56, 86, 100), rows::kCross, 0},
        {0x8001C2D8u, 1, pointer::placed(56, 108, 100), rows::kCross, 0},
    };
    expect(rows::row_under(close, {100.0, 106.0}) == 1 && rows::row_under(close, {100.0, 101.0}) == 0,
           "between two rows within reach of both the nearer takes the pointer");
    expect(rows::press_at(drawn, 0, {254.0, 86.0}, false) == rows::kRight, "a press just past a value is the value's");
    expect(rows::press_at(drawn, 0, {230.0, 85.0}, false) == rows::kRight && rows::press_at(drawn, 0, {230.0, 85.0}, true) == rows::kLeft,
           "a press on the value holds its buttons");
    expect(rows::press_at(drawn, 0, {150.0, 85.0}, false) == 0u && rows::press_at(drawn, 0, {214.0, 85.0}, false) == 0u,
           "a press on the title, or beside it, holds the title's, none");
    expect(rows::press_at(drawn, 3, {160.0, 210.0}, false) == rows::kCross && rows::press_at(drawn, 3, {160.0, 210.0}, true) == 0u,
           "RETURN TO MAIN MENU is chosen with the left button only");
}

void test_backs() {
    bool chosen = true;
    for (const rows::Back &back : rows::kBacks) {
        bool found = false;
        for (const rows::Row &row : rows::kRows) found = found || (row.screen == back.screen && row.row == back.row && row.left == rows::kCross);
        chosen = chosen && found;
    }
    expect(chosen, "every back row is a row of its screen chosen with Cross");
    Guest guest;
    const rows::Row *sound = rows::match(0x8001F248u, rows::kOptions, 0u, 0u, guest);
    const rows::Row *back = rows::match(0x8001F394u, rows::kOptions, 0u, 0u, guest);
    const std::vector<rows::Drawn> options = {{0x8001F248u, 0, pointer::placed(-208, 80, 100), 0, 0, sound},
                                              {0x8001F394u, 3, pointer::placed(0, 204, 120), rows::kCross, 0, back}};
    const rows::Drawn *options_back = rows::back_draw(rows::kOptions, options, guest);
    expect(options_back && options_back->row == 3 && options_back->left == rows::kCross, "Esc on GENERAL OPTIONS is its RETURN TO MAIN MENU");
    expect(!rows::back_draw(rows::kDeath, options, guest) && !rows::back_draw(rows::kFormat, options, guest),
           "another screen's rows are no back of this one, and the format screen has none");
    expect(!rows::back_draw(rows::kOptions, std::span(options).first(1), guest), "a pass without RETURN has no back");

    Guest paused;
    const rows::Row *resume = rows::match(rows::kResumeDrawn, rows::kPause, 0u, 0u, paused);
    paused.words[rows::kPauseAsks] = 1u;
    const rows::Row *no = rows::match(0x80042CA0u, rows::kPause, 0u, 0u, paused);
    const std::vector<rows::Drawn> pause = {{rows::kResumeDrawn, 0, pointer::placed(0, 100, 80), rows::kCross, 0, resume},
                                            {0x80042CA0u, 6, pointer::placed(169, 126, 20), rows::kCross, 0, no}};
    const rows::Drawn *answer = rows::back_draw(rows::kPause, pause, paused);
    expect(answer && answer->row == 6, "while ABORT asks Esc is its NO");
    paused.words[rows::kPauseAsks] = 0u;
    const rows::Drawn *resumed = rows::back_draw(rows::kPause, pause, paused);
    expect(resumed && resumed->row == 0, "else the pause's RESUME MISSION");

    Guest asking;
    asking.words[rows::kCursor] = 2u;
    asking.words[rows::kOverwrite] = 1u;
    const rows::Row *question = rows::match(0x8001CE14u, rows::kNewGame, 0u, 0u, asking);
    asking.words[rows::kOverwrite] = 0u;
    const rows::Row *slots_back = rows::match(0x8001CA50u, rows::kNewGame, 0u, 0u, asking);
    asking.words[rows::kOverwrite] = 1u;
    const std::vector<rows::Drawn> overwrite = {{0x8001CA50u, 10, pointer::placed(0, 204, 120), rows::kCross, 0, slots_back},
                                                {0x8001CE14u, 12, pointer::placed(28, 124, 200), rows::kCross, rows::kLeft, question}};
    expect(!rows::back_draw(rows::kNewGame, overwrite, asking), "while NEW GAME asks to overwrite, its RETURN drawn a pass before is no back");
}

}  // namespace

int main() {
    test_placement();
    test_table();
    test_match();
    test_hits();
    test_backs();
    if (g_failures != 0) return 1;
    std::cout << "disruptor menu rows: PASS\n";
    return 0;
}
