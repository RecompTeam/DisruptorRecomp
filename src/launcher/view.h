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
    void show(Screen screen, int page) {
        screen_ = screen;
        page_ = page;
        section_ = page;
    }
    void open_updates() { updates_open_ = true; }
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
    void last_activity();
    bool update_strip(const UpdateView& update);
    std::string update_action_label(const UpdateView& update) const;
    // The one button a state of the updater offers. True when the launcher must close for the update to be applied.
    bool update_action(const UpdateView& update);
    void settings();
    void rows();
    void key_table();
    void tag(const char* text, unsigned hex);
    void hint_lines(std::string_view text, float width);
    void pad_status();
    void pad_buttons();
    void finish_pad(const std::string& source);
    std::string pad_name(const std::string& source) const;
    void group_heading(const char* name, bool first);
    bool toggle(const char* id, bool* value);
    bool segmented(const char* id, int* index, std::span<const char* const> choices, float width);
    bool updates_popup(const UpdateView& update);
    void help();
    // `compact` sets a card's notes tighter than the Help page's guide.
    void blocks(const std::vector<Block>& list, float width, bool compact = false);
    bool news(const UpdateView& update);
    // A line of the guide wrapped to `width`: its links open, its code spans copy themselves.
    void rich(const Line& line, float width, bool strong = false, bool faint = false, float leading = 1.5f);
    void about();
    void details_popup();
    void earlier_popup();
    void notes(const std::string& markdown, std::size_t most_lines = 0);
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
    bool dragging_ = false;
    bool details_ = false;
    bool updates_open_ = false;
    float cards_height_ = 0.0f;
    bool redraw_ = false;
    std::string listening_;
    std::string key_note_;
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
    int section_ = 0;
    bool section_changed_ = true;
    std::string copied_;
    double copied_at_ = 0.0;
    TitleBar bar_;
    int caption_pressed_ = -1;
    int caption_hovered_ = -1;
    double caption_hovered_since_ = 0.0;
    std::map<const Option*, int> held_; // a slider's position while it is dragged, written when it is let go
};

} // namespace disruptor::launcher
