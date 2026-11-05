#include "game/session/state_screen.h"

#include "game/session/state_files.h"

#include "boot_state.h"
#include "cpu_state.h"
#include "game/achievements/progress_file.h"
#include "game/language/language.h"
#include "game/session/capsule.h"
#include "mod_plugins.h"
#include "psx_memory.h"
#include "savestate.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

extern "C" int gl_renderer_snap_shown(int width);
extern "C" int gl_renderer_snap_taken(unsigned char *rgb, int room, int *width, int *height);
extern "C" void gl_renderer_snap_cancel(void);
extern "C" void psx_host_video_get_display_aspect(int *numerator, int *denominator);
extern "C" int disruptor_front_end_shown(void);
extern "C" uint8_t *memory_get_ram_ptr(void);
extern "C" void disruptor_states_row_reset(void);

namespace disruptor::state_screen {
namespace {

namespace fs = std::filesystem;
namespace browser = disruptor::state_browser;
namespace files = disruptor::state_files;

using Clock = std::chrono::steady_clock;
constexpr std::chrono::milliseconds kStillShown{200}, kRowFresh{100};
constexpr int kPictureWide = 480;
constexpr int kHideFirst = 2, kGiveUp = 45; /* frames: the shown picture lags the game's by up to two */
constexpr uint32_t kPauseDraw = 0x80042BB8u;
constexpr uintmax_t kFileMost = 64u << 20;
constexpr std::array<std::string_view, 4> kBeside = {".shown", ".thumb", ".hdres", ".shown.part"};

std::mutex g_mutex;
Font g_font, g_menu_font;
GameSaves g_saves;
Clock::time_point g_saves_at{};
GameAsk g_game_ask;
std::atomic<bool> g_screen_ready{false}, g_ask_refused{false};
std::vector<Run> g_pause_row;
Clock::time_point g_pause_row_at{};
std::array<std::string, 13> g_names;
std::optional<browser::Place> g_asked, g_ready;
std::atomic<bool> g_opening{false};
std::array<Clock::time_point, 2> g_context{};
std::array<Card, browser::kSlots> g_cards;
uint64_t g_generation = 0;
files::Picture g_scene;
std::atomic<bool> g_watching{false};

/* A picture of the screen asked for a state just saved, or for the scene behind the pause. The emulation thread's. */
struct Snap {
    enum class Kind { none, save, scene } kind = Kind::none;
    std::string state;
    uint32_t ram_crc = 0;
    files::Place place = files::Place::unknown;
    int frames = 0;
    bool asked = false;
};
Snap g_snap;
std::atomic<bool> g_hiding{false};
std::vector<unsigned char> g_pixels;
std::atomic<bool> g_save_with_scene{false};
std::atomic<int> g_load_asked{-1};
std::atomic<bool> g_load_staged{false}, g_landed{false}, g_load_ended{false};

/* Files are read and written on a thread of their own, one job after another. */
class Worker {
public:
    void push(std::function<void()> job) {
        {
            std::scoped_lock lock(mutex_);
            jobs_.push_back(std::move(job));
            if (!thread_.joinable()) thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
        }
        wake_.notify_one();
    }

private:
    void run(std::stop_token stop) {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                if (!wake_.wait(lock, stop, [this] { return !jobs_.empty(); })) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::function<void()>> jobs_;
    std::jthread thread_;
};

Worker &worker() {
    static Worker each;
    return each;
}

std::string whole(const fs::path &file) {
    std::error_code failed;
    const uintmax_t size = fs::file_size(file, failed);
    if (failed || size == 0u || size > kFileMost) return {};
    std::ifstream input(file, std::ios::binary);
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) return {};
    return bytes;
}

std::string slot_path(int slot) {
    char path[600] = {};
    return savestate_slot_path(slot, path, sizeof(path)) ? std::string(path) : std::string();
}

std::array<Card, browser::kSlots> read_cards() {
    std::array<Card, browser::kSlots> out;
    uint32_t bios = 0, entry = 0;
    savestate_get_integrity(&bios, &entry);
    for (int slot = 0; slot < browser::kSlots; ++slot) {
        const std::string path = slot_path(slot);
        if (path.empty()) continue;
        const std::string bytes = whole(fs::path(path));
        if (bytes.empty()) continue;
        Card &card = out[static_cast<size_t>(slot)];
        card.present = true;
        card.loads = boot_state_check_buffer(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), bios, entry, nullptr, 0) != 0;
        int64_t saved = 0;
        if (savestate_slot_mtime(slot, &saved)) card.saved = saved;
        const std::optional<files::Contents> contents = files::read_contents(bytes);
        if (!contents) continue;
        card.mission = contents->mission;
        card.difficulty = contents->difficulty;
        card.picture = contents->display;
        std::optional<files::Shown> shown = files::decode(whole(fs::path(files::beside(path, ".shown"))));
        if (shown && shown->ram_crc == contents->ram_crc) {
            card.place = shown->place;
            card.picture = std::move(shown->picture);
        }
    }
    return out;
}

/* Kept with the checksum of the RAM the state was saved with: one whose state is no longer in the slot is never shown. */
void write_shown(const std::string &state, uint32_t ram_crc, files::Picture picture, files::Place place) {
    const std::string bytes = files::encode({ram_crc, place, std::move(picture)});
    const std::string target = files::beside(state, ".shown");
    if (bytes.empty() || target.empty()) return;
    const fs::path part(target + ".part");
    {
        std::ofstream output(part, std::ios::binary | std::ios::trunc);
        if (!output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return;
    }
    std::error_code failed;
    fs::rename(part, fs::path(target), failed);
    if (failed) fs::remove(part, failed);
}

void finish(files::Picture picture) {
    if (g_snap.kind == Snap::Kind::save && !picture.empty()) {
        worker().push([state = g_snap.state, crc = g_snap.ram_crc, picture = std::move(picture), place = g_snap.place]() mutable {
            write_shown(state, crc, std::move(picture), place);
            if (g_watching.load()) reload();
        });
    } else if (g_snap.kind == Snap::Kind::scene) {
        std::scoped_lock lock(g_mutex);
        g_scene = std::move(picture);
        g_ready = browser::Place::pause;
    }
    g_snap = {};
    g_hiding.store(false);
}

/* While the picture is taken the pause menu is not drawn: the scene behind it is what the card shows. */
void pause_draw_entry(CPUState *cpu, uint32_t) {
    if (g_hiding.load()) psx_mod_finish_function(cpu);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_state_screen) {
    if (!psx_mod_register_function_entry_plugin("disruptor.state_screen.pause", kPauseDraw, pause_draw_entry))
        std::fprintf(stderr, "disruptor: no pause hook for the save state pictures, they show the pause menu\n");
}

}  // namespace

void publish_font(Font font) {
    std::scoped_lock lock(g_mutex);
    g_font = std::move(font);
}

void publish_menu_font(Font font) {
    std::scoped_lock lock(g_mutex);
    g_menu_font = std::move(font);
}

void publish_game_saves(GameSaves saves) {
    std::scoped_lock lock(g_mutex);
    g_saves = std::move(saves);
    g_saves_at = Clock::now();
}

GameAsk take_game_ask() {
    std::scoped_lock lock(g_mutex);
    return std::exchange(g_game_ask, GameAsk{});
}

void note_ask_refused() { g_ask_refused.store(true); }

bool take_ask_refused() { return g_ask_refused.exchange(false); }

void set_screen_ready(bool ready) { g_screen_ready.store(ready); }

bool screen_ready() { return g_screen_ready.load(); }

Font menu_font() {
    std::scoped_lock lock(g_mutex);
    return g_menu_font;
}

std::optional<GameSaves> game_saves() {
    std::scoped_lock lock(g_mutex);
    if (Clock::now() - g_saves_at >= kStillShown) return std::nullopt;
    return g_saves;
}

void note_load_left() {
    std::scoped_lock lock(g_mutex);
    g_saves_at = {};
}

void ask_game(GameAsk ask) {
    std::scoped_lock lock(g_mutex);
    g_game_ask = ask;
}

void publish_pause_row(std::vector<Run> runs) {
    std::scoped_lock lock(g_mutex);
    g_pause_row = std::move(runs);
    g_pause_row_at = Clock::now();
}

void note_mission_names(const std::array<std::string, 13> &names) {
    std::scoped_lock lock(g_mutex);
    g_names = names;
}

void ask_open(browser::Place place) {
    std::scoped_lock lock(g_mutex);
    if (g_opening.exchange(true)) return;
    g_asked = place;
}

void note_context(browser::Place place) {
    std::scoped_lock lock(g_mutex);
    g_context[static_cast<size_t>(place)] = Clock::now();
}

Font font() {
    std::scoped_lock lock(g_mutex);
    return g_font;
}

std::vector<Run> pause_row() {
    std::scoped_lock lock(g_mutex);
    return Clock::now() - g_pause_row_at < kRowFresh ? g_pause_row : std::vector<Run>{};
}

bool opening() { return g_opening.load(); }

std::optional<browser::Place> take_open() {
    std::scoped_lock lock(g_mutex);
    const std::optional<browser::Place> ready = std::exchange(g_ready, std::nullopt);
    if (ready) g_opening.store(false);
    return ready;
}

bool context_alive(browser::Place place) {
    std::scoped_lock lock(g_mutex);
    return Clock::now() - g_context[static_cast<size_t>(place)] < kStillShown;
}

std::string mission_name(uint32_t mission) {
    std::string known;
    {
        std::scoped_lock lock(g_mutex);
        if (mission >= 1u && mission <= g_names.size()) known = g_names[mission - 1u];
    }
    return browser::mission_name(mission, known, disruptor_language_disc_name());
}

void reload() {
    g_watching.store(true);
    worker().push([] {
        std::array<Card, browser::kSlots> cards = read_cards();
        std::scoped_lock lock(g_mutex);
        g_cards = std::move(cards);
        ++g_generation;
    });
}

std::optional<std::array<Card, browser::kSlots>> cards_after(uint64_t &seen) {
    std::scoped_lock lock(g_mutex);
    if (g_generation == seen) return std::nullopt;
    seen = g_generation;
    return g_cards;
}

bool busy() { return savestate_pending() != 0 || disruptor_capsule_holds_states() != 0; }

void ask_load(int slot) { g_load_asked.store(slot); }

bool take_landed() { return g_landed.exchange(false); }

bool take_load_ended() { return g_load_ended.exchange(false); }

bool save(int slot) {
    if (busy() || slot < 0 || slot >= browser::kSlots) return false;
    g_save_with_scene.store(true);
    if (savestate_request_save(slot)) return true;
    g_save_with_scene.store(false);
    return false;
}

bool erase(int slot) {
    if (busy() || slot < 0 || slot >= browser::kSlots) return false;
    const std::string path = slot_path(slot);
    if (path.empty()) return false;
    std::error_code failed;
    const bool gone = fs::remove(fs::path(path), failed) && !failed;
    if (!gone) return false;
    for (const std::string_view ending : kBeside) {
        const std::string other = files::beside(path, ending);
        if (!other.empty()) fs::remove(fs::path(other), failed);
    }
    achievements::erase_progress(achievements::progress_file_for(fs::path(path)));
    return true;
}

}  // namespace disruptor::state_screen

namespace screen = disruptor::state_screen;

extern "C" void disruptor_state_screen_saving(const char *state) {
    if (!state || !*state || disruptor_capsule_holds_states()) return;
    const std::string beside = disruptor::state_files::beside(state, ".shown");
    std::error_code failed;
    if (!beside.empty()) std::filesystem::remove(std::filesystem::path(beside), failed);
}

extern "C" void disruptor_state_screen_saved(const char *state, int written) {
    using screen::Snap;
    const bool with_scene = screen::g_save_with_scene.exchange(false);
    if (!written || !state || !*state || disruptor_capsule_holds_states()) return;
    const disruptor::state_files::Place place =
        disruptor_front_end_shown() ? disruptor::state_files::Place::menus : disruptor::state_files::Place::play;
    /* The RAM is as the state holds it until the guest runs on: its checksum ties the picture to this state. */
    const uint32_t crc = static_cast<uint32_t>(crc32(0L, memory_get_ram_ptr(), static_cast<uInt>(memory_get_ram_bytes())));
    if (with_scene) {
        disruptor::state_files::Picture scene;
        {
            std::scoped_lock lock(screen::g_mutex);
            scene = screen::g_scene;
        }
        screen::worker().push([path = std::string(state), crc, scene = std::move(scene), place]() mutable {
            if (!scene.empty()) screen::write_shown(path, crc, std::move(scene), place);
            if (screen::g_watching.load()) screen::reload();
        });
        return;
    }
    if (disruptor_capsule_recording() || screen::g_snap.kind != Snap::Kind::none) return;
    screen::g_snap = {Snap::Kind::save, state, crc, place, 0, false};
    screen::g_hiding.store(true);
}

extern "C" void disruptor_state_screen_frame(void) {
    using screen::Snap;
    /* The load poll runs on this thread too: a staged load no longer pending has landed or failed by now. */
    if (const int slot = screen::g_load_asked.exchange(-1); slot >= 0) {
        const bool staged = !screen::busy() && slot < disruptor::state_browser::kSlots && savestate_request_load(slot) != 0;
        if (staged) disruptor_states_row_reset();
        screen::g_load_staged.store(staged);
        if (!staged) screen::g_load_ended.store(true);
    } else if (screen::g_load_staged.load() && savestate_pending() == 0) {
        screen::g_load_staged.store(false);
        screen::g_load_ended.store(true);
    }
    Snap &snap = screen::g_snap;
    if (snap.kind == Snap::Kind::none) {
        std::optional<disruptor::state_browser::Place> asked;
        {
            std::scoped_lock lock(screen::g_mutex);
            asked = std::exchange(screen::g_asked, std::nullopt);
            if (asked) screen::g_scene = {};
            if (asked && (*asked == disruptor::state_browser::Place::front_end || disruptor_capsule_recording())) screen::g_ready = asked;
        }
        if (!asked || *asked == disruptor::state_browser::Place::front_end || disruptor_capsule_recording()) return;
        /* Over the pause the card of a save made from the screen shows the scene behind the pause, taken before it opens. */
        snap = {Snap::Kind::scene, {}, 0u, disruptor::state_files::Place::play, 0, false};
        screen::g_hiding.store(true);
        return;
    }
    ++snap.frames;
    if (!snap.asked) {
        if (snap.frames >= screen::kHideFirst && gl_renderer_snap_shown(screen::kPictureWide)) snap.asked = true;
    } else {
        int width = 0, height = 0;
        screen::g_pixels.resize(static_cast<size_t>(screen::kPictureWide) * screen::kPictureWide * 3u);
        if (gl_renderer_snap_taken(screen::g_pixels.data(), static_cast<int>(screen::g_pixels.size()), &width, &height)) {
            int numerator = 4, denominator = 3;
            psx_host_video_get_display_aspect(&numerator, &denominator);
            screen::finish(disruptor::state_files::without_bars(screen::g_pixels.data(), static_cast<uint32_t>(width),
                                                                static_cast<uint32_t>(height), numerator, denominator));
            return;
        }
    }
    if (snap.frames <= screen::kGiveUp) return;
    if (snap.asked) gl_renderer_snap_cancel();
    screen::finish({});
}

extern "C" void disruptor_state_screen_loaded(void) {
    using screen::Snap;
    {
        /* The screens the game showed before the load are gone at once, not when their last draw grows old. */
        std::scoped_lock lock(screen::g_mutex);
        screen::g_saves_at = {};
        screen::g_context = {};
    }
    screen::g_load_staged.store(false);
    screen::g_landed.store(true);
    Snap &snap = screen::g_snap;
    if (snap.kind == Snap::Kind::none) return;
    if (snap.asked) gl_renderer_snap_cancel();
    if (snap.kind == Snap::Kind::scene) screen::g_opening.store(false);
    snap = {};
    screen::g_hiding.store(false);
}

extern "C" int disruptor_state_screen_hiding(void) { return screen::g_hiding.load() ? 1 : 0; }
