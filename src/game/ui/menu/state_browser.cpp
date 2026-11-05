#include "game/ui/menu/menu.h"

namespace disruptor::menu {
namespace {

namespace screen = disruptor::state_screen;
namespace browser = disruptor::state_browser;
namespace files = disruptor::state_files;

using Clock = std::chrono::steady_clock;
constexpr std::chrono::milliseconds kLeftAfter{500}, kGoneMost{1000};

enum class Phase { closed, open, closing };

/* A shade the game lays over its letters, 1 for its own colours. */
struct Shade {
    float r = 1.0f, g = 1.0f, b = 1.0f;
};
constexpr Shade kFull{}, kDim{60.0f / 128.0f, 60.0f / 128.0f, 60.0f / 128.0f}, kLit{1.5f, 1.5f, 0.0f};
/* The menu items' colours by the item draw's index: 0 an item, 1 the chosen one, 6 a hard game. Titles take 0: yellow reads as chosen. */
constexpr int kItem = 0, kChosen = 1, kHard = 6;

struct Ui {
    Phase phase = Phase::closed;
    browser::Model model;
    std::array<screen::Card, browser::kSlots> cards{};
    std::array<unsigned, browser::kSlots> textures{};
    std::array<bool, browser::kSlots> renew{};
    std::array<unsigned, 13> icons{};
    std::array<std::vector<uint8_t>, 13> icon_rgb{};
    screen::GameSaves saves;
    uint64_t seen = 0;
    bool read = false;
    bool waiting = false;
    /* LOAD GAME was left by a choice: it opens again only once the game has left it, or after a second. */
    bool leaving = false;
    Clock::time_point opened{}, closing{};
    std::set<int> held;
    std::optional<std::pair<float, float>> pointed;
    browser::LoadFade fade;
    std::optional<screen::GameAsk> playing; /* the saved game the fade starts, none for a state */
    std::vector<std::pair<browser::Rect, browser::Key>> keys;
    browser::Rect yes, no, back;
    std::array<std::pair<browser::Rect, browser::Rect>, browser::kGameSlots> steps{}; /* a saved game's arrows */
};

std::mutex g_mutex;
Ui g_ui;

double ms_now() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

browser::Model::States slots_of(const Ui &ui) {
    browser::Model::States slots{};
    for (size_t slot = 0; slot < slots.size(); ++slot) slots[slot] = {ui.cards[slot].present, ui.cards[slot].loads};
    return slots;
}

browser::Model::Games games_of(const Ui &ui) {
    browser::Model::Games games{};
    for (size_t slot = 0; slot < games.size(); ++slot) {
        const screen::GameSlot &game = ui.saves.slots[slot];
        games[slot] = {game.state == 1u && !ui.saves.message, game.level, game.most};
    }
    return games;
}

void open(Ui &ui, browser::Place place) {
    ui.model = {};
    ui.model.place = place;
    if (place == browser::Place::front_end) {
        ui.model.cursor = 0;
        for (int slot = 0; slot < browser::kGameSlots; ++slot)
            if (ui.saves.slots[static_cast<size_t>(slot)].state == 1u && !ui.saves.message) {
                ui.model.cursor = browser::kGames + slot;
                break;
            }
    }
    ui.phase = Phase::open;
    screen::reload();
    ui.read = false;
    ui.waiting = false;
    ui.held.clear();
    ui.pointed.reset();
    ui.opened = Clock::now();
}

void close(Ui &ui) {
    ui.phase = Phase::closing;
    ui.closing = Clock::now();
}

/* LOAD GAME is up and the screen ready, the pause asked for the screen, the game left it, or the keys of a choice went up. */
void poll(Ui &ui) {
#if defined(PSX_SDL3)
    screen::set_screen_ready(g_menu.imgui_ready);
#endif
    const std::optional<screen::GameSaves> saves = screen::game_saves();
    if (saves) ui.saves = *saves;
    const bool front = saves && screen::context_alive(browser::Place::front_end);
    const bool landed = screen::take_landed(), refused = screen::take_ask_refused(), ended = screen::take_load_ended();
    if (landed && ui.phase == Phase::open) close(ui);
    /* A saved game has come once the game no longer draws LOAD GAME. */
    switch (ui.fade.tick(ms_now(), ui.playing ? !front : landed, ui.playing ? refused : ended)) {
    case browser::LoadFade::Now::ask:
        if (ui.playing) screen::ask_game(*ui.playing);
        else screen::ask_load(ui.fade.slot);
        break;
    case browser::LoadFade::Now::landed:
        if (ui.playing && ui.phase == Phase::open) close(ui);
        ui.playing.reset();
        break;
    case browser::LoadFade::Now::missed: ui.playing.reset(); break;
    case browser::LoadFade::Now::nothing: break;
    }
    if (refused || (ui.leaving && (!front || Clock::now() - ui.closing > kGoneMost))) ui.leaving = false;
    if (ui.phase == Phase::closed) {
        const std::optional<browser::Place> place = screen::take_open();
        if (place && g_menu.imgui_ready) open(ui, *place);
        else if (front && !ui.leaving && g_menu.imgui_ready) open(ui, browser::Place::front_end);
    }
    if (ui.phase == Phase::open && Clock::now() - ui.opened > kLeftAfter && !screen::context_alive(ui.model.place)) ui.phase = Phase::closed;
    /* Keys and buttons seen down stay the screen's until they go up: the game would take a held one for a new press. */
    if (ui.phase == Phase::closing && ui.held.empty()) ui.phase = Phase::closed;
    if (std::optional<std::array<screen::Card, browser::kSlots>> cards = screen::cards_after(ui.seen); cards) {
        ui.cards = std::move(*cards);
        ui.renew.fill(true);
        ui.read = true;
        ui.model.read = true;
    }
    if (ui.waiting && !screen::busy()) {
        ui.waiting = false;
        screen::reload();
    }
    ui.model.loads_refused = disruptor_achievements_hardcore() != 0;
}

void act(Ui &ui, browser::Outcome outcome) {
    switch (outcome.act) {
    case browser::Do::load:
        if (!screen::busy()) ui.fade.start(outcome.slot, ms_now());
        break;
    case browser::Do::save:
        if (screen::save(outcome.slot)) ui.waiting = true;
        break;
    case browser::Do::erase:
        if (screen::erase(outcome.slot)) screen::reload();
        break;
    case browser::Do::play:
        ui.playing = screen::GameAsk{screen::GameAsk::Kind::play, outcome.slot, outcome.mission};
        ui.fade.start(-1, ms_now());
        break;
    case browser::Do::leave:
        screen::ask_game({screen::GameAsk::Kind::back, 0, 1u});
        ui.leaving = true;
        close(ui);
        break;
    case browser::Do::close: close(ui); break;
    case browser::Do::nothing: break;
    }
}

std::optional<browser::Key> bound_key(SDL_Scancode code) {
    constexpr std::array<std::pair<const char *, browser::Key>, 8> fields = {{{"up", browser::Key::up},
                                                                              {"down", browser::Key::down},
                                                                              {"left", browser::Key::left},
                                                                              {"right", browser::Key::right},
                                                                              {"cross", browser::Key::choose},
                                                                              {"circle", browser::Key::back},
                                                                              {"square", browser::Key::save},
                                                                              {"triangle", browser::Key::erase}}};
    for (const auto &[field, key] : fields) {
        const std::optional<size_t> index = disruptor::settings::key_field(field);
        if (index && psx_keybinds_get_button(1, static_cast<int>(*index)) == code) return key;
    }
    return std::nullopt;
}

/* The screen's own keys first, the player's keys of the pad's buttons after, then on LOAD GAME the strafe keys' sides. */
std::optional<browser::Key> key_of_scancode(SDL_Scancode code, int held, browser::Place place) {
    switch (code) {
    case SDL_SCANCODE_UP: return browser::Key::up;
    case SDL_SCANCODE_DOWN: return browser::Key::down;
    case SDL_SCANCODE_LEFT: return browser::Key::left;
    case SDL_SCANCODE_RIGHT: return browser::Key::right;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return (held & KMOD_SHIFT) != 0 ? browser::Key::save : browser::Key::choose;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE: return browser::Key::back;
    case SDL_SCANCODE_DELETE: return browser::Key::erase;
    default: break;
    }
    if (const std::optional<browser::Key> bound = bound_key(code); bound) return bound;
    const int side = place == browser::Place::front_end ? disruptor_strafe_side(static_cast<int>(code)) : 0;
    if (side != 0) return side < 0 ? browser::Key::less : browser::Key::more;
    return std::nullopt;
}

/* The pad's controls by the player's map, as the game reads the pad: a control the map leaves out is its usual button. */
struct PadControl {
    const char *field, *usual;
    browser::Key key;
};
constexpr std::array<PadControl, 10> kPadControls = {{{"up", "dpup", browser::Key::up},
                                                     {"down", "dpdown", browser::Key::down},
                                                     {"left", "dpleft", browser::Key::left},
                                                     {"right", "dpright", browser::Key::right},
                                                     {"cross", "a", browser::Key::choose},
                                                     {"circle", "b", browser::Key::back},
                                                     {"square", "x", browser::Key::save},
                                                     {"triangle", "y", browser::Key::erase},
                                                     {"l1", "leftshoulder", browser::Key::less},
                                                     {"r1", "rightshoulder", browser::Key::more}}};

const char *pad_source_of(const PadControl &control) {
    const char *mapped = psx_input_pad_source(control.field);
    return mapped ? mapped : control.usual;
}

#if defined(PSX_SDL3)
std::optional<browser::Key> key_of_button(Uint8 button) {
    const char *name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
    if (!name) return std::nullopt;
    for (const PadControl &control : kPadControls)
        if (std::strcmp(pad_source_of(control), name) == 0) return control.key;
    return std::nullopt;
}

/* The printed name of the pad button a face control is on, a PlayStation pad's in the disc's words. */
std::string pad_name(const PadControl &control, std::string_view language) {
    const disruptor::settings::PadSource *known = disruptor::settings::pad_source(pad_source_of(control));
    if (!known) return {};
    const disruptor::settings::PadFamily family = disruptor::settings::family_shown(g_menu.pad_in_use);
    const std::string printed = disruptor::settings::printed(*known, family);
    constexpr std::array<std::pair<std::string_view, browser::Word>, 4> faces = {{{"Cross", browser::Word::cross},
                                                                                 {"Circle", browser::Word::circle},
                                                                                 {"Square", browser::Word::square},
                                                                                 {"Triangle", browser::Word::triangle}}};
    for (const auto &[name, which] : faces)
        if (printed == name) return std::string(browser::word(which, language));
    return disruptor::password_hint::key_word(printed);
}
#endif

/* Where the 240 rows of the front end's frame lie in the window: its first row and a row's height. */
std::pair<float, float> frame_rows(int width, int height) {
    int numerator = 4, denominator = 3;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    const std::optional<menu_pointer::Point> first = menu_pointer::window_point(0.0, 0.0, width, height, numerator, denominator);
    const std::optional<menu_pointer::Point> last = menu_pointer::window_point(0.0, 240.0, width, height, numerator, denominator);
    if (!first || !last) return {0.0f, static_cast<float>(height) / 240.0f};
    return {static_cast<float>(first->y), static_cast<float>(last->y - first->y) / 240.0f};
}

float display_aspect() {
    int numerator = 4, denominator = 3;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    return static_cast<float>(numerator) / static_cast<float>(denominator);
}

browser::FrontLayout front_shape(int width, int height) {
    const auto [top, unit] = frame_rows(width, height);
    return browser::front_layout(width, top, unit, display_aspect());
}

void press(Ui &ui, browser::Key key) {
    if (ui.phase != Phase::open || ui.waiting || ui.fade.holds()) return;
    act(ui, ui.model.press(key, slots_of(ui), games_of(ui)));
}

void click(Ui &ui, bool right) {
    if (ui.phase != Phase::open || ui.waiting || ui.fade.holds()) return;
    if (right) {
        act(ui, ui.model.press(browser::Key::back, slots_of(ui), games_of(ui)));
        return;
    }
    double x = 0.0, y = 0.0;
    int width = 0, height = 0;
    if (!disruptor_mouse_menu_pointer(&x, &y, &width, &height)) return;
    const float px = static_cast<float>(x), py = static_cast<float>(y);
    if (ui.model.asking != browser::Ask::none) {
        if (ui.yes.contains(px, py)) act(ui, ui.model.answer(true));
        else if (ui.no.contains(px, py)) act(ui, ui.model.answer(false));
        return;
    }
    for (const auto &[rect, key] : ui.keys)
        if (rect.contains(px, py)) {
            act(ui, ui.model.press(key, slots_of(ui), games_of(ui)));
            return;
        }
    if (ui.model.place == browser::Place::front_end)
        for (int game = 0; game < browser::kGameSlots; ++game) {
            const auto &[less, more] = ui.steps[static_cast<size_t>(game)];
            if (!less.contains(px, py) && !more.contains(px, py)) continue;
            ui.model.click_step(game, less.contains(px, py) ? -1 : 1, games_of(ui));
            return;
        }
    const int cell = ui.model.place == browser::Place::front_end
                         ? browser::front_cell_at(front_shape(width, height), ui.back, px, py)
                         : browser::card_at(browser::layout(width, height, display_aspect()), px, py);
    act(ui, ui.model.click(cell, slots_of(ui), games_of(ui)));
}

/* The game's own shade of an item by its colour index, 128 its colours as they are; a chosen one blinks as the game's. */
Shade game_shade(const screen::GameSaves &saves, int index) {
    const std::array<uint8_t, 3> &rgb = saves.shades[static_cast<size_t>(std::clamp(index, 0, 7))];
    if (rgb[0] == 0u && rgb[1] == 0u && rgb[2] == 0u) return kFull;
    return {static_cast<float>(rgb[0]) / 128.0f, static_cast<float>(rgb[1]) / 128.0f, static_cast<float>(rgb[2]) / 128.0f};
}

Shade item_shade(const screen::GameSaves &saves, bool chosen, bool hard) {
    if (chosen && saves.blink % 32u < 17u) return game_shade(saves, kChosen);
    return game_shade(saves, hard ? kHard : kItem);
}

ImU32 colour(uint16_t entry, const Shade &shade) {
    const auto channel = [](uint32_t five, float by) { return static_cast<int>(std::min(255.0f, static_cast<float>(five << 3) * by)); };
    return IM_COL32(channel(entry & 31u, shade.r), channel(entry >> 5 & 31u, shade.g), channel(entry >> 10 & 31u, shade.b), 255);
}

/* Without the game's font the host's own letters stand in, as tall: the screen is never shown without words. */
float text_width(const screen::Font &font, std::string_view text, int texel) {
    if (!font.ready()) {
        const std::string whole(text);
        return ImGui::GetFont()->CalcTextSizeA(static_cast<float>(8 * texel), FLT_MAX, 0.0f, whole.c_str()).x;
    }
    int width = 0;
    for (const char letter : text) {
        const int glyph = font.glyph(letter);
        width += letter == ' ' || glyph < 0 ? font.space : font.widths[static_cast<size_t>(glyph)];
    }
    return static_cast<float>(width * texel);
}

/* The letters as the game's font has them, each texel a square of `texel` pixels, runs along a row drawn as one. */
void draw_text(ImDrawList *draw, const screen::Font &font, std::string_view text, float x, float y, int texel, const Shade &shade) {
    const float size = static_cast<float>(texel);
    if (!font.ready()) {
        const std::string whole(text);
        draw->AddText(ImGui::GetFont(), 8.0f * size, ImVec2(x, y), colour(0x7FFFu, shade), whole.c_str());
        return;
    }
    for (const char letter : text) {
        const int glyph = font.glyph(letter);
        if (letter == ' ' || glyph < 0) {
            x += static_cast<float>(font.space * texel);
            continue;
        }
        if (letter == ':' && !font.menu) {
            /* The small font has no colon: its glyph 62 is a full stop in play and other data in the menus. */
            const ImU32 dot = colour(font.palette[4], shade);
            for (const int row : {1, 4})
                draw->AddRectFilled(ImVec2(x + size, y + static_cast<float>(row) * size), ImVec2(x + 3.0f * size, y + static_cast<float>(row + 2) * size), dot);
            x += static_cast<float>(font.widths[static_cast<size_t>(glyph)] * texel);
            continue;
        }
        for (int row = 0; row < font.high; ++row) {
            int column = 0;
            while (column < font.wide) {
                const uint8_t index = font.glyphs[static_cast<size_t>((glyph * font.high + row) * font.wide + column)];
                int end = column + 1;
                while (end < font.wide && font.glyphs[static_cast<size_t>((glyph * font.high + row) * font.wide + end)] == index) ++end;
                const uint16_t entry = font.palette[index];
                if (index != 0u && (entry & 0x7FFFu) != 0u)
                    draw->AddRectFilled(ImVec2(x + static_cast<float>(column) * size, y + static_cast<float>(row) * size),
                                        ImVec2(x + static_cast<float>(end) * size, y + static_cast<float>(row + 1) * size), colour(entry, shade));
                column = end;
            }
        }
        x += static_cast<float>(font.widths[static_cast<size_t>(glyph)] * texel);
    }
}

void draw_centred(ImDrawList *draw, const screen::Font &font, std::string_view text, float middle, float y, int texel, const Shade &shade) {
    draw_text(draw, font, text, std::floor(middle - text_width(font, text, texel) / 2.0f), y, texel, shade);
}

/* Cut from the end until it fits. */
std::string fitted(const screen::Font &font, std::string text, float room, int texel) {
    while (!text.empty() && text_width(font, text, texel) > room) text.pop_back();
    return text;
}

std::string saved_words(int64_t saved, std::string_view language) {
    const std::time_t when = static_cast<std::time_t>(saved), now = std::time(nullptr);
    std::tm then{}, today{};
#ifdef _WIN32
    if (localtime_s(&then, &when) != 0 || localtime_s(&today, &now) != 0) return {};
#else
    if (!localtime_r(&when, &then) || !localtime_r(&now, &today)) return {};
#endif
    std::tm then_noon = then, today_noon = today;
    then_noon.tm_hour = today_noon.tm_hour = 12;
    then_noon.tm_min = today_noon.tm_min = then_noon.tm_sec = today_noon.tm_sec = 0;
    const double apart = std::difftime(std::mktime(&today_noon), std::mktime(&then_noon)) / 86400.0;
    return browser::when({then.tm_year + 1900, then.tm_mon + 1, then.tm_mday, then.tm_hour, then.tm_min},
                         static_cast<int>(std::lround(apart)), today.tm_year + 1900, language);
}

std::array<std::string, 3> card_lines(const screen::Card &card, int slot, std::string_view language) {
    if (!card.present) return browser::caption(slot, {}, language);
    const bool menus = card.place == files::Place::menus;
    return browser::caption(slot,
                            {true, card.loads, menus, card.difficulty == files::kRegular, menus ? std::string() : screen::mission_name(card.mission),
                             saved_words(card.saved, language)},
                            language);
}

/* The names of what presses each of the screen's choices now: the pad's buttons after a pad press, the keys else. */
std::array<std::string, 4> choice_keys(std::string_view language) {
#if defined(PSX_SDL3)
    if (g_menu.pad_in_use.pad_last())
        return {pad_name(kPadControls[4], language), pad_name(kPadControls[5], language), pad_name(kPadControls[6], language),
                pad_name(kPadControls[7], language)};
#endif
    std::array<std::string, 4> faces = face_names(language);
    const std::array<std::string, 4> fixed = {"ENTER", "ESC", std::string(browser::word(browser::Word::shift_enter, language)), "DELETE"};
    for (size_t index = 0; index < faces.size(); ++index)
        if (faces[index].empty()) faces[index] = fixed[index];
    return faces;
}

/* The names of what steps a saved game's mission: the pad's shoulders after a pad press, the strafe keys else. */
std::string step_keys(std::string_view language) {
#if defined(PSX_SDL3)
    if (g_menu.pad_in_use.pad_last()) return pad_name(kPadControls[8], language) + " " + pad_name(kPadControls[9], language);
#endif
    std::string names;
    for (const int field : {PSX_KB_L2, PSX_KB_R2}) {
        const SDL_Scancode code = psx_keybinds_get_button(1, field);
        if (code == SDL_SCANCODE_UNKNOWN) continue;
        names += (names.empty() ? "" : " ") + disruptor::password_hint::key_word(SDL_GetScancodeName(code), language);
    }
    return names;
}

/* The keys of what the chosen cell takes, in a line that a click on a word presses too. */
void draw_keys(ImDrawList *draw, Ui &ui, const screen::Font &font, const browser::Rect &line, int texel, std::string_view language) {
    ui.keys.clear();
    if (ui.waiting) {
        draw_centred(draw, font, browser::word(browser::Word::saving, language), line.x + line.w / 2.0f, line.y, texel, kFull);
        return;
    }
    const std::array<std::string, 4> names = choice_keys(language);
    const auto words = [&](size_t name, browser::Word which) { return names[name] + " " + std::string(browser::word(which, language)); };
    struct Item {
        std::string text;
        std::optional<browser::Key> key;
    };
    std::vector<Item> items;
    const bool pause = ui.model.place == browser::Place::pause;
    const int cell = ui.model.cursor;
    if (browser::is_state(cell)) {
        const browser::Slot slot = slots_of(ui)[static_cast<size_t>(cell)];
        /* LOAD GAME has no line for it under its title: the note stands where LOAD would. */
        if (!pause && ui.model.loads_refused && slot.present && slot.loads) items.push_back({std::string(browser::word(browser::Word::hardcore, language)), {}});
        if (ui.model.may_load(slot)) items.push_back({words(0, browser::Word::load), browser::Key::choose});
        else if (pause && !slot.present) items.push_back({words(0, browser::Word::save), browser::Key::choose});
        if (pause && slot.present) items.push_back({words(2, browser::Word::save), browser::Key::save});
        if (slot.present) items.push_back({words(3, browser::Word::erase), browser::Key::erase});
    } else if (browser::is_game(cell) && games_of(ui)[static_cast<size_t>(cell - browser::kGames)].present) {
        items.push_back({words(0, browser::Word::start), browser::Key::choose});
        const std::string steps = step_keys(language);
        if (games_of(ui)[static_cast<size_t>(cell - browser::kGames)].most > 1u && !steps.empty())
            items.push_back({steps + " " + std::string(browser::word(browser::Word::mission, language)), browser::Key::more});
    }
    /* LOAD GAME has RETURN TO MAIN MENU on it. */
    if (pause) items.push_back({words(1, browser::Word::back), browser::Key::back});
    const float gap = static_cast<float>(4 * font.space * texel);
    float wide = 0.0f;
    for (const Item &item : items) wide += text_width(font, item.text, texel) + (wide > 0.0f ? gap : 0.0f);
    float x = std::floor(line.x + (line.w - wide) / 2.0f);
    for (const Item &item : items) {
        const float width = text_width(font, item.text, texel);
        draw_text(draw, font, item.text, x, line.y, texel, item.key ? kFull : kDim);
        if (item.key) ui.keys.push_back({{x, line.y - static_cast<float>(2 * texel), width, static_cast<float>(12 * texel)}, *item.key});
        x += width + gap;
    }
}

/* A box over the cards with its lines, the last of which the caller fills: its top and the row of that line. */
std::pair<browser::Rect, float> question_box(ImDrawList *draw, const screen::Font &font, const std::vector<std::string> &lines, float last_wide,
                                             float middle, float centre, int texel) {
    const float line = static_cast<float>(font.high * texel), pad = static_cast<float>(8 * texel);
    float wide = last_wide;
    for (const std::string &text : lines) wide = std::max(wide, text_width(font, text, texel));
    const float box_wide = wide + 2.0f * pad, box_high = static_cast<float>(lines.size() + 1) * (line + pad) + pad;
    const browser::Rect box{std::floor(middle - box_wide / 2.0f), std::floor(centre - box_high / 2.0f), box_wide, box_high};
    draw->AddRectFilled(ImVec2(box.x, box.y), ImVec2(box.x + box.w, box.y + box.h), IM_COL32(8, 8, 12, 245));
    draw->AddRect(ImVec2(box.x, box.y), ImVec2(box.x + box.w, box.y + box.h), IM_COL32(150, 150, 0, 255), 0.0f, 0, static_cast<float>(texel));
    float y = box.y + pad;
    for (const std::string &text : lines) {
        draw_centred(draw, font, text, middle, y, texel, kFull);
        y += line + pad;
    }
    return {box, y};
}

void draw_question(ImDrawList *draw, Ui &ui, const screen::Font &font, float middle, float centre, int texel, std::string_view language) {
    if (ui.model.asking == browser::Ask::none) return;
    const std::string key = browser::key_of(ui.model.cursor);
    std::vector<std::string> lines;
    switch (ui.model.asking) {
    case browser::Ask::load:
        lines.push_back(browser::with_key(browser::Word::ask_load, key, language));
        lines.emplace_back(browser::word(browser::Word::progress_lost, language));
        break;
    case browser::Ask::save: lines.push_back(browser::with_key(browser::Word::ask_save, key, language)); break;
    case browser::Ask::erase: lines.push_back(browser::with_key(browser::Word::ask_erase, key, language)); break;
    default: break;
    }
    const std::string yes(browser::word(browser::Word::yes, language)), no(browser::word(browser::Word::no, language));
    const float spaced = text_width(font, "    ", texel), yes_wide = text_width(font, yes, texel), no_wide = text_width(font, no, texel);
    const auto [box, y] = question_box(draw, font, lines, yes_wide + spaced + no_wide, middle, centre, texel);
    const float pad = static_cast<float>(8 * texel), line = static_cast<float>(font.high * texel);
    const float x = std::floor(middle - (yes_wide + spaced + no_wide) / 2.0f);
    /* In the menu font the answers take the game's colours: the chosen one blinks yellow as the game's items do. */
    const Shade lit = font.menu ? item_shade(ui.saves, true, false) : kLit, dim = font.menu ? game_shade(ui.saves, kItem) : kDim;
    draw_text(draw, font, yes, x, y, texel, ui.model.yes ? lit : dim);
    draw_text(draw, font, no, x + yes_wide + spaced, y, texel, ui.model.yes ? dim : lit);
    ui.yes = {x - pad / 2.0f, y - pad / 2.0f, yes_wide + pad, line + pad};
    ui.no = {x + yes_wide + spaced - pad / 2.0f, y - pad / 2.0f, no_wide + pad, line + pad};
}

void draw_picture(ImDrawList *draw, unsigned texture, const files::Picture &picture, const browser::Rect &box) {
    if (texture == 0u || picture.empty()) return;
    const browser::Crop crop = browser::cover(static_cast<float>(picture.width) / static_cast<float>(picture.height), box.w / box.h);
    draw->AddImage(static_cast<ImTextureID>(texture), ImVec2(box.x, box.y), ImVec2(box.x + box.w, box.y + box.h), ImVec2(crop.u0, crop.v0),
                   ImVec2(crop.u1, crop.v1));
}

void draw_state_cards(ImDrawList *draw, Ui &ui, const screen::Font &font, const std::array<browser::Rect, browser::kSlots> &pictures,
                      int texel, std::string_view language) {
    const float t = static_cast<float>(texel);
    for (int slot = 0; slot < browser::kSlots; ++slot) {
        const browser::Rect &picture = pictures[static_cast<size_t>(slot)];
        const screen::Card &card = ui.cards[static_cast<size_t>(slot)];
        draw->AddRectFilled(ImVec2(picture.x, picture.y), ImVec2(picture.x + picture.w, picture.y + picture.h), IM_COL32(18, 20, 26, 235));
        draw_picture(draw, ui.textures[static_cast<size_t>(slot)], card.picture, picture);
        const bool chosen = ui.model.cursor == slot;
        draw->AddRect(ImVec2(picture.x, picture.y), ImVec2(picture.x + picture.w, picture.y + picture.h),
                      chosen ? IM_COL32(230, 230, 0, 255) : IM_COL32(70, 74, 86, 255), 0.0f, 0, chosen ? 2.0f * t : t);
        const std::array<std::string, 3> lines = card_lines(card, slot, language);
        const float first = picture.y + picture.h + 2.0f * t;
        draw->AddRectFilled(ImVec2(picture.x, first - t), ImVec2(picture.x + picture.w, picture.y + picture.h + static_cast<float>(browser::kCardLines) * t),
                            IM_COL32(0, 0, 0, 150));
        for (size_t line = 0; line < lines.size(); ++line)
            draw_text(draw, font, fitted(font, lines[line], picture.w, texel), picture.x, first + 10.0f * t * static_cast<float>(line), texel,
                      line == 0 ? (chosen ? kLit : kFull) : kDim);
    }
}

void draw_game_cards(ImDrawList *draw, Ui &ui, const screen::Font &menu, const screen::Font &small, const browser::FrontLayout &shape,
                     std::string_view language) {
    const int texel = shape.texel;
    const float t = static_cast<float>(texel);
    const browser::Model::Games games = games_of(ui);
    for (int slot = 0; slot < browser::kGameSlots; ++slot) {
        const browser::Rect &card = shape.games[static_cast<size_t>(slot)];
        const screen::GameSlot &game = ui.saves.slots[static_cast<size_t>(slot)];
        ui.steps[static_cast<size_t>(slot)] = {};
        const bool chosen = ui.model.cursor == browser::kGames + slot, present = game.state == 1u && !ui.saves.message;
        draw->AddRectFilled(ImVec2(card.x, card.y), ImVec2(card.x + card.w, card.y + card.h), IM_COL32(0, 0, 0, 150));
        draw->AddRect(ImVec2(card.x, card.y), ImVec2(card.x + card.w, card.y + card.h), chosen ? IM_COL32(230, 230, 0, 255) : IM_COL32(70, 74, 86, 255),
                      0.0f, 0, chosen ? 2.0f * t : t);
        const float middle = card.x + card.w / 2.0f, room = card.w - 4.0f * t;
        if (!present) {
            /* The game's own line has its stepping arrows round EMPTY: nothing steps on a slot without a save. */
            const std::string &word = ui.saves.message || game.state > 2u ? game.second
                                      : game.state == 2u && !ui.saves.unavailable.empty() ? ui.saves.unavailable
                                      : !ui.saves.empty.empty()                         ? ui.saves.empty
                                                                                         : game.second;
            draw_centred(draw, menu, fitted(menu, word, room, texel), middle, card.y + (card.h - static_cast<float>(menu.high * texel)) / 2.0f,
                         texel, game_shade(ui.saves, chosen ? kChosen : kItem));
            continue;
        }
        const float icon_wide = static_cast<float>(browser::kIconWide * shape.icon), icon_high = static_cast<float>(browser::kIconHigh * shape.icon);
        const browser::Rect icon{std::floor(middle - icon_wide / 2.0f), card.y + shape.game_top, icon_wide, icon_high};
        const size_t mission = std::clamp<size_t>(ui.model.mission_of(slot, games), 1u, ui.saves.icons.size()) - 1u;
        draw_picture(draw, ui.icons[mission], ui.saves.icons[mission], icon);
        float y = icon.y + icon.h + 2.0f * t;
        const Shade shade = item_shade(ui.saves, chosen, game.hard);
        draw_centred(draw, menu, fitted(menu, game.name, room, texel), middle, y, texel, shade);
        y += static_cast<float>(menu.high * texel) + t;
        const float high = static_cast<float>(menu.high * texel), arrow = std::floor(high * 0.6f), mid = y + high / 2.0f;
        float name_room = room;
        if (games[static_cast<size_t>(slot)].most > 1u) {
            /* The game's line steps with arrows round the mission: the font has none, they are drawn as triangles. */
            const ImU32 tint = chosen ? IM_COL32(230, 230, 0, 255) : IM_COL32(140, 140, 140, 255);
            const float left = card.x + 3.0f * t, right = card.x + card.w - 3.0f * t;
            draw->AddTriangleFilled(ImVec2(left + arrow, mid - arrow / 2.0f), ImVec2(left + arrow, mid + arrow / 2.0f), ImVec2(left, mid), tint);
            draw->AddTriangleFilled(ImVec2(right - arrow, mid - arrow / 2.0f), ImVec2(right, mid), ImVec2(right - arrow, mid + arrow / 2.0f), tint);
            const browser::Rect less{card.x, y - 4.0f * t, arrow + 6.0f * t, high + 8.0f * t};
            ui.steps[static_cast<size_t>(slot)] = {less, {card.x + card.w - less.w, less.y, less.w, less.h}};
            name_room = card.w - 2.0f * (arrow + 6.0f * t);
        }
        draw_centred(draw, menu, fitted(menu, ui.saves.missions[mission], name_room, texel), middle, y, texel, shade);
        y += static_cast<float>(menu.high * texel) + 2.0f * t;
        draw_centred(draw, small, browser::word(game.hard ? browser::Word::hard : browser::Word::regular, language), middle, y, texel, kDim);
    }
}

void renew_textures(Ui &ui) {
    for (size_t slot = 0; slot < ui.textures.size(); ++slot) {
        if (!ui.renew[slot]) continue;
        ui.renew[slot] = false;
        if (ui.textures[slot] != 0u) gl_renderer_picture_free(ui.textures[slot]);
        ui.textures[slot] = 0u;
        const files::Picture &picture = ui.cards[slot].picture;
        if (!picture.empty())
            ui.textures[slot] = gl_renderer_picture(picture.rgb.data(), static_cast<int>(picture.width), static_cast<int>(picture.height));
    }
    for (size_t mission = 0; mission < ui.icons.size(); ++mission) {
        const files::Picture &picture = ui.saves.icons[mission];
        if (picture.empty() || picture.rgb == ui.icon_rgb[mission]) continue;
        if (ui.icons[mission] != 0u) gl_renderer_picture_free(ui.icons[mission]);
        ui.icons[mission] = gl_renderer_picture(picture.rgb.data(), static_cast<int>(picture.width), static_cast<int>(picture.height));
        ui.icon_rgb[mission] = picture.rgb;
    }
}

void free_textures(Ui &ui) {
    for (unsigned &texture : ui.textures) {
        if (texture != 0u) gl_renderer_picture_free(texture);
        texture = 0u;
    }
    for (size_t mission = 0; mission < ui.icons.size(); ++mission) {
        if (ui.icons[mission] != 0u) gl_renderer_picture_free(ui.icons[mission]);
        ui.icons[mission] = 0u;
        ui.icon_rgb[mission].clear();
    }
}

/* The pause's SAVE STATES under RESUME, where the game's lines are drawn: they are not scaled with the HUD. */
void draw_pause_row(int width, int height) {
    const std::vector<screen::Run> runs = screen::pause_row();
    if (runs.empty()) return;
    int numerator = 4, denominator = 3;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    for (const screen::Run &run : runs) {
        const std::optional<menu_pointer::Point> from = menu_pointer::window_point(run.x, run.y, width, height, numerator, denominator);
        const std::optional<menu_pointer::Point> to =
            menu_pointer::window_point(run.x + run.length, run.y + 1, width, height, numerator, denominator);
        if (!from || !to) return;
        draw->AddRectFilled(ImVec2(static_cast<float>(from->x), static_cast<float>(from->y)), ImVec2(static_cast<float>(to->x), static_cast<float>(to->y)),
                            IM_COL32(run.r, run.g, run.b, 255));
    }
}

/* A move of the pointer chooses the cell under it. */
void hover(Ui &ui, int cell, const std::pair<float, float> &at) {
    if (ui.model.asking != browser::Ask::none || ui.waiting) return;
    if (ui.pointed && *ui.pointed != at && cell >= 0) ui.model.cursor = cell;
    ui.pointed = at;
}

void draw_front(ImDrawList *draw, Ui &ui, int width, int height, const std::optional<std::pair<float, float>> &pointer, std::string_view language) {
    const screen::Font small = screen::font(), menu_letters = screen::menu_font();
    const screen::Font &menu = menu_letters.ready() ? menu_letters : small;
    const browser::FrontLayout shape = front_shape(width, height);
    const float middle = static_cast<float>(width) / 2.0f;
    const int texel = shape.texel;
    const std::string back = ui.saves.back.empty() ? std::string("RETURN TO MAIN MENU") : ui.saves.back;
    const float back_wide = text_width(menu, back, shape.big);
    ui.back = {std::floor(middle - back_wide / 2.0f), shape.back, back_wide, static_cast<float>(menu.high * shape.big)};
    if (pointer) hover(ui, browser::front_cell_at(shape, ui.back, pointer->first, pointer->second), *pointer);
    draw_centred(draw, menu, ui.saves.title, middle, shape.title, shape.big, game_shade(ui.saves, kItem));
    draw_game_cards(draw, ui, menu, small, shape, language);
    draw_centred(draw, menu, browser::word(browser::Word::title, language), middle, shape.states_title, shape.big, game_shade(ui.saves, kItem));
    if (ui.read) draw_state_cards(draw, ui, small, shape.pictures, shape.caption, language);
    const browser::Rect line{shape.cards.front().x, shape.keys, shape.cards[5].x + shape.cards[5].w - shape.cards.front().x, 0.0f};
    draw_keys(draw, ui, small, line, texel, language);
    draw_text(draw, menu, back, ui.back.x, ui.back.y, shape.big, item_shade(ui.saves, ui.model.cursor == browser::kBack, false));
    const float centre = (shape.games.front().y + shape.cards.back().y + shape.cards.back().h) / 2.0f;
    if (ui.model.asking != browser::Ask::none)
        draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(width), static_cast<float>(height)), IM_COL32(0, 0, 0, 150));
    draw_question(draw, ui, menu, middle, centre, shape.big, language);
}

void draw_pause(ImDrawList *draw, Ui &ui, int width, int height, const std::optional<std::pair<float, float>> &pointer, std::string_view language) {
    const screen::Font font = screen::font();
    const browser::Layout shape = browser::layout(width, height, display_aspect());
    if (pointer) hover(ui, browser::card_at(shape, pointer->first, pointer->second), *pointer);
    draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(width), static_cast<float>(height)), IM_COL32(0, 0, 0, 244));
    const std::string title(browser::word(browser::Word::title, language));
    draw_centred(draw, font, title, shape.title.x + shape.title.w / 2.0f, shape.title.y, 2 * shape.texel, kFull);
    if (ui.model.loads_refused) {
        const std::string note(browser::word(browser::Word::hardcore, language));
        draw_centred(draw, font, note, shape.title.x + shape.title.w / 2.0f, shape.title.y + shape.title.h + static_cast<float>(shape.texel),
                     shape.texel, kDim);
    }
    if (ui.read) draw_state_cards(draw, ui, font, shape.pictures, shape.texel, language);
    draw_keys(draw, ui, font, shape.footer, shape.texel, language);
    draw_question(draw, ui, font, shape.title.x + shape.title.w / 2.0f, shape.cards[4].y + shape.cards[4].h / 2.0f, shape.texel, language);
}

}  // namespace

bool state_browser_captures() {
    std::scoped_lock lock(g_mutex);
    poll(g_ui);
    return g_ui.phase != Phase::closed || screen::opening();
}

bool state_browser_shown() {
    if (state_browser_captures()) return true;
    {
        std::scoped_lock lock(g_mutex);
        if (g_ui.fade.active()) return true;
    }
    return !screen::pause_row().empty();
}

bool state_browser_event(const SDL_Event *event) {
#if defined(PSX_SDL3)
    if (!event) return false;
    std::scoped_lock lock(g_mutex);
    Ui &ui = g_ui;
    poll(ui);
    if (ui.phase == Phase::closed) return false;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
        if (event->key.scancode == SDL_SCANCODE_GRAVE) return false;
        ui.held.insert(static_cast<int>(event->key.scancode));
        /* A held key repeats: only a step goes on with it, the key that opened the screen chooses nothing in it. */
        if (const std::optional<browser::Key> key = key_of_scancode(event->key.scancode, event->key.mod, ui.model.place);
            key && (!event->key.repeat || *key == browser::Key::up || *key == browser::Key::down || *key == browser::Key::left ||
                    *key == browser::Key::right))
            press(ui, *key);
        return true;
    case SDL_EVENT_KEY_UP: ui.held.erase(static_cast<int>(event->key.scancode)); return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        ui.held.insert(1000 + event->gbutton.button);
        if (const std::optional<browser::Key> key = key_of_button(event->gbutton.button); key) press(ui, *key);
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_UP: ui.held.erase(1000 + event->gbutton.button); return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        ui.held.insert(2000 + event->button.button);
        if (event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT) click(ui, event->button.button == SDL_BUTTON_RIGHT);
        return true;
    case SDL_EVENT_MOUSE_BUTTON_UP: ui.held.erase(2000 + event->button.button); return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST: ui.held.clear(); return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
        std::erase_if(ui.held, [](int held) { return held >= 1000 && held < 2000; });
        return false;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: return true;
    default: return false;
    }
#else
    (void)event;
    return false;
#endif
}

void draw_state_browser(int width, int height) {
    std::scoped_lock lock(g_mutex);
    Ui &ui = g_ui;
    poll(ui);
    if (disruptor_state_screen_hiding()) return;
    /* LOAD GAME stays drawn while it is left: the game draws none of its own until then. */
    const bool front_shown = ui.model.place == browser::Place::front_end && (ui.phase == Phase::closing || ui.leaving) &&
                             screen::game_saves().has_value();
    const float dark = ui.fade.darkness(ms_now());
    const auto draw_dark = [&] {
        if (dark > 0.0f)
            ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(static_cast<float>(width), static_cast<float>(height)),
                                                          IM_COL32(0, 0, 0, static_cast<int>(dark * 255.0f + 0.5f)));
    };
    if (ui.phase != Phase::open && !front_shown) {
        if (ui.phase == Phase::closed) {
            free_textures(ui);
            draw_pause_row(width, height);
        }
        draw_dark();
        return;
    }
    renew_textures(ui);
    const std::string_view language = disruptor_language_disc_name();
    double x = 0.0, y = 0.0;
    int pixels_wide = 0, pixels_high = 0;
    std::optional<std::pair<float, float>> pointer;
    if (disruptor_mouse_menu_pointer(&x, &y, &pixels_wide, &pixels_high)) pointer = std::pair{static_cast<float>(x), static_cast<float>(y)};
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    if (ui.model.place == browser::Place::front_end) draw_front(draw, ui, width, height, pointer, language);
    else draw_pause(draw, ui, width, height, pointer, language);
    if (pointer) draw_arrow_at(pointer->first, pointer->second, width, height);
    draw_dark();
}

}  // namespace disruptor::menu
