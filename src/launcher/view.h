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
    }
    void attach(SDL_Window* window, SDL_Renderer* renderer);
    void dropped(const std::filesystem::path& file) { session_.take_image(file); }
    // While a file is dragged over the window the drop zone says it will take it.
    void dragging(bool over) { dragging_ = over; }
    // A file dialog answers on a thread of its own: the next frame takes the answer.
    enum class Wanted { image, language, earlier };
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
    void take_pad_button(int button);
    void take_pad_axis(std::uint32_t pad, int axis, int value);
    void stop_pad_capture();
    // A frame laid something out from the frame before it and must be drawn again at once.
    bool redraw() { return std::exchange(redraw_, false); }

    const LauncherChoices& choices() const { return choices_; }
    // Where the last frame drew the title bar and its buttons, for the window's hit test.
    const TitleBar& title_bar() const { return bar_; }

private:
    void caption();
    void sidebar(const UpdateView& update);
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
    void rows();
    void key_table();
    // Two keys a row for count rows from first, under the keyboard's: a key an action also has is faint, the action wins.
    void pick_table(const char* heading, std::size_t first, std::size_t count, const KeyLayout& pad);
    settings::PickKeys pick_keys() const;
    // The tables under the setting that turns them on, with their own note, hints and default.
    void pick_section();
    // Take it on a note: a taken key moves where it was asked for, the owner gets the other's old key or none.
    void take_offer();
    // The offer's key is still where its note says.
    bool offer_holds() const;
    // Two saved sets of keys under the Keys table.
    void profiles();
    void take_pick_key(const std::string& field, const std::string& name);
    void tag(const char* text, unsigned hex);
    void hint_lines(std::string_view text, float width);
    void pad_status();
    void pad_buttons();
    void finish_pad(const std::string& source);
    std::string pad_name(const std::string& source) const;
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
    bool version_hidden_ = false;
    bool dragging_ = false;
    bool details_ = false;
    bool updates_open_ = false;
    float cards_height_ = 0.0f;
    bool redraw_ = false;
    std::string listening_;
    std::string key_note_;
    std::string pick_note_;
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
    struct Motion {
        float from = 0.0f, to = 0.0f;
        double since = 0.0;
    };
    std::map<unsigned, Motion> motions_;
    bool animate_ = true;
    std::vector<char> far_east_; // a system font for CJK letters in a shown path, kept for the font atlas
    std::map<std::string, std::vector<Line>, std::less<>> hints_;
    int controls_tab_ = 0;
};

} // namespace disruptor::launcher
