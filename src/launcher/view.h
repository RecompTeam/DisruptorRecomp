#pragma once

#include "activity.h"
#include "choices.h"
#include "guide.h"
#include "release_notes.h"
#include "session.h"
#include "updater.h"

#include "game/settings/pad_layout.h"

#include <array>
#include <cstdint>

struct SDL_Gamepad;

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct ImFont;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace disruptor::launcher {

enum class Screen { home, settings, help, about };

class View {
public:
    View(Session& session, Updater& updater, Activity& activity, std::filesystem::path choices_file);
    ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    void style(float scale);
    void show(Screen screen, int page, int tab = 0) {
        screen_ = screen;
        page_ = page;
        section_ = page;
        controls_tab_ = tab;
    }
    void open_updates() { updates_open_ = true; }
    void for_picture(float pixels) {
        rows_scroll_ = pixels;
        version_hidden_ = true;
        animate_ = false;
        hands_off_ = true;
    }
    void attach(SDL_Window* window, SDL_Renderer* renderer);
    void dropped(const std::filesystem::path& file) { session_.take_image(file); }
    // While a file is dragged over the window the drop zone says it will take it.
    void dragging(bool over) { dragging_ = over; }
    // A file dialog answers on a thread of its own: the next frame takes the answer.
    enum class Wanted { image, language, earlier, sound };
    struct Picked {
        std::mutex mutex;
        bool open = false;
        Wanted wanted = Wanted::image;
        std::optional<std::string> path;
    };
    // One frame of the window. True when the launcher must close for an update to be applied.
    bool draw();
    // A key button of the Controls page waits for a key: the window's key events go to take_key instead of ImGui.
    bool listening() const { return !listening_.empty(); }
    void take_key(const char* name);
    // A button of the Gamepad group waits for a press on a gamepad, which it opens until then.
    bool listening_pad() const { return !listening_pad_.empty(); }
    // A capture waits, or the button it took is still held: the gamepad neither moves nor presses in the window.
    bool pad_blocked() const { return !listening_pad_.empty() || pad_quiet_; }
    // A picture's --virtual-pad: the Gamepad page behaves as with one gamepad connected.
    void pretend_pad() { pretend_pad_ = true; }
    void take_pad_button(int button);
    void take_pad_axis(std::uint32_t pad, int axis, int value);
    void stop_pad_capture();
    // A frame laid something out from the frame before it and must be drawn again at once.
    bool redraw() { return std::exchange(redraw_, false); }
    // The screen, page and tab shown and what waits for a key, for the log of a check driven by --input.
    std::string describe() const;

    const LauncherChoices& choices() const { return choices_; }
    // Where the last frame drew the title bar and its buttons, for the window's hit test.
    const TitleBar& title_bar() const { return bar_; }

private:
    void caption();
    void sidebar(const UpdateView& update);
    // The next or the previous page in the side bar's order, the cursor on its entry: Ctrl+Tab and a gamepad's bumpers.
    void step_page(int step);
    bool home(const UpdateView& update);
    void banner(float height, bool dialog_open);
    void first_run(bool dialog_open);
    void cards(bool dialog_open);
    // The last task's line between `left` and `right`. False, with nothing drawn, when it has no room there.
    bool last_activity(float left, float right, float middle);
    bool update_strip(const UpdateView& update);
    std::string update_action_label(const UpdateView& update) const;
    // The one button a state of the updater offers. True when the launcher must close for the update to be applied.
    bool update_action(const UpdateView& update);
    void settings();
    // In place of the rows while settings.toml cannot be read: what that means, start over, the file's folder.
    void unreadable_settings(float width);
    void achievements_account();
    // The Troubleshooting page's steps to a bug report, what a report holds and the button that makes one.
    void report_steps(bool first = false);
    void rows();
    void coming_soon();
    // One setting: its name, hints and control on the right, a hanging one indented under the row it needs.
    void setting_row(const Placement& placement, const Option& option, float control, bool hanging);
    void key_table();
    // Two keys a row for count rows from first, under the keyboard's: a key an action also has is faint, the action wins.
    void pick_table(const char* heading, std::size_t first, std::size_t count, const KeyLayout& pad);
    settings::PickKeys pick_keys() const;
    // The tables under the setting that turns them on, with their own note, hints and default.
    void pick_section();
    void unlock_sound_row();
    void take_unlock_sound(const std::filesystem::path& chosen);
    void default_unlock_sound();
    // Take it on a note: a taken key moves where it was asked for, the owner gets the other's old key or none.
    void take_offer();
    // The offer's key is still where its note says.
    bool offer_holds() const;
    // Two saved sets of keys under the Keys table.
    void profiles();
    void store_profile(int slot);
    // What a Default button or a Save over a profile replaces: done once the player says yes in its window.
    enum class Replacing { keys, picks, buttons, profile_1, profile_2 };
    void ask_to_replace(Replacing what);
    void replace_popup();
    void take_pick_key(const std::string& field, const std::string& name);
    void tag(const char* text, unsigned hex);
    void hint_lines(std::string_view text, float width);
    void pad_status();
    void pad_buttons();
    void finish_pad(const std::string& source);
    std::string pad_name(const std::string& source) const;
    void page_title(const char* text);
    void group_heading(const char* name, bool first);
    bool toggle(const char* id, bool* value);
    bool segmented(const char* id, int* index, std::span<const char* const> choices, float width);
    // A value on its way to `target` over `seconds`, eased out: while it moves the window draws again. At once without animations.
    float eased(unsigned id, float target, float seconds);
    // A modal's opacity: it fades in, and leaves at once.
    float fade_in(const char* name);
    bool updates_popup(const UpdateView& update);
    void help();
    // `compact` sets a card's notes tighter than the Help page's guide.
    void blocks(const std::vector<Block>& list, float width, bool compact = false);
    bool news(const UpdateView& update);
    // A line of the guide wrapped to `width`: its links open, its code spans copy themselves.
    void rich(const Line& line, float width, bool strong = false, bool faint = false, float leading = 1.5f, unsigned tint = 0);
    void about();
    void details_popup();
    void earlier_popup();
    void pick(Wanted wanted);
    void take_picked();
    void load_art();
    bool button(const char* glyph, const char* label, bool primary = false);
    bool link(const char* label);
    // A page, a folder: neither opens for a picture, whose steps must not reach outside the window.
    void open_url(const char* address);
    void open_folder(const std::filesystem::path& folder);
    // True for a picture: the action is named for its log instead of done.
    bool held_off(const char* action);
    void heading(const char* text);

    Session& session_;
    Updater& updater_;
    Activity& activity_;
    std::filesystem::path choices_file_;
    LauncherChoices choices_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    ImFont* body_ = nullptr;
    ImFont* bold_ = nullptr;
    ImFont* heavy_ = nullptr;
    float scale_ = 1.0f;
    Screen screen_ = Screen::home;
    int page_ = 0;
    int held_page_ = -1;
    float rows_scroll_ = 0.0f;
    float rows_at_ = 0.0f;
    bool version_hidden_ = false;
    bool dragging_ = false;
    bool details_ = false;
    bool updates_open_ = false;
    float cards_height_ = 0.0f;
    bool redraw_ = false;
    std::string listening_;
    std::string key_note_;
    std::string pick_note_;
    std::string sound_note_;
    bool sound_note_good_ = false;
    bool key_note_good_ = false;  // the note says something was done, not refused
    struct Offer {
        std::string key;
        std::string target;  // where the key was asked for: a pick's field or an action's
        std::string owner;   // the field the note names
    };
    std::optional<Offer> offer_;
    bool keys_read_ = false;
    bool pads_tried_ = false;
    std::string listening_pad_;
    std::string pad_note_;
    bool pad_map_read_ = false;
    std::map<std::uint32_t, std::array<bool, 2>> trigger_armed_;
    settings::PadFamily pad_family_ = settings::PadFamily::xbox;
    std::vector<SDL_Gamepad*> capture_pads_;
    bool pads_ = false;
    bool pretend_pad_ = false;
    bool pad_quiet_ = false;
    bool status_below_ = false; // the banner had no room for the last task's line: it stands under the banner
    std::size_t scene_ = 0;
    std::filesystem::path art_disc_;
    std::vector<SDL_Texture*> scenes_;
    SDL_Texture* first_run_ = nullptr;
    SDL_Texture* app_icon_ = nullptr;
    std::size_t seen_lines_ = 0;
    Guide guide_;
    std::optional<ReleaseNotes> this_notes_;
    std::string news_source_;
    std::vector<Block> news_blocks_;
    std::string notes_source_;  // the Updates window's release notes
    std::vector<Block> notes_blocks_;
    int section_ = 0;
    bool section_changed_ = true;
    std::string copied_;
    double copied_at_ = 0.0;
    TitleBar bar_;
    int caption_pressed_ = -1;
    int caption_hovered_ = -1;
    double caption_hovered_since_ = 0.0;
    std::map<const Option*, int> held_; // a slider's position while it is dragged, written when it is let go
    const Option* active_slider_ = nullptr;
    bool rows_scroll_bar_ = false; // the settings rows scrolled at the last frame
    float value_width_ = 0.0f; // the widest slider value of the page shown
    std::optional<Replacing> replacing_;
    bool replace_asked_ = false;
    bool slider_focused_ = false;  // this frame's keyboard or gamepad cursor is on a slider
    bool focus_sidebar_ = false;   // the side bar's entry of the page shown takes the cursor at its next frame
    bool enter_page_ = false;      // the page's first control takes the cursor: its side bar entry was pressed by key or button
    // Where the cursor was at the end of the last frame: Escape and a gamepad's B take it from a page back to the side bar.
    unsigned sidebar_window_ = 0;
    bool cursor_in_page_ = false, popup_last_frame_ = false, active_last_frame_ = false;
    struct Motion {
        float from = 0.0f, to = 0.0f;
        double since = 0.0;
    };
    std::map<unsigned, Motion> motions_;
    bool animate_ = true;
    bool hands_off_ = false; // a picture's steps start no game, open no page, folder or file dialog and sign nobody in
    std::string held_off_;   // the last action a picture held off, for the log
    std::vector<char> far_east_; // a system font for CJK letters in a shown path, kept for the font atlas
    struct HintLine {
        Line whole;     // as written, a choice's name in bold before its colon
        Line name;      // that name without the colon, for a column of names
        Line rest;      // what follows the name, or a line that goes on under the name above it
        bool goes_on = false;
    };
    struct Hint {
        std::vector<HintLine> lines;
        bool columns = false;  // a line goes on under a name: the names stand in a column of their own
    };
    std::map<std::string, Hint, std::less<>> hints_;
    int controls_tab_ = 0;
    std::array<char, 64> account_name_{};
    std::array<char, 256> account_password_{};
};

} // namespace disruptor::launcher
