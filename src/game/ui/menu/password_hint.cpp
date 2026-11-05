#include "game/ui/menu/menu.h"

namespace disruptor::menu {
namespace {

namespace hint = disruptor::password_hint;
namespace pointer = disruptor::menu_pointer;

constexpr uint32_t kItemDraw = 0x8001A46Cu;
constexpr uint32_t kScreen = 0x800715B0u;
constexpr uint32_t kPassword = 4u;
constexpr uint32_t kReturnDrawn = 0x8001E830u, kReturnDrawnBelow = 0x8001E94Cu; /* RETURN TO MAIN MENU, once a frame */
constexpr std::chrono::milliseconds kStillShown{200};
constexpr std::array<const char *, 4> kFaces = {"cross", "circle", "square", "triangle"};

/* Built on the emulation thread at the screen's draw, drawn by the frame's drawing on another. */
std::mutex g_mutex;
std::vector<hint::Run> g_runs;
std::atomic<std::chrono::steady_clock::rep> g_built{0};

std::string key_of(const char *field, std::string_view language) {
    const std::optional<size_t> index = disruptor::settings::key_field(field);
    const SDL_Scancode key = index ? psx_keybinds_get_button(1, static_cast<int>(*index)) : SDL_SCANCODE_UNKNOWN;
    return key == SDL_SCANCODE_UNKNOWN ? std::string() : hint::key_word(SDL_GetScancodeName(key), language);
}

/* The words of the device pressed last: a PlayStation pad's buttons are the symbols themselves. */
std::vector<hint::Line> lines_now() {
    const std::string_view language = disruptor_language_disc_name();
    std::array<std::string, 4> faces;
#if defined(PSX_SDL3)
    if (g_menu.pad_in_use.pad_last()) {
        const disruptor::settings::PadFamily family = disruptor::settings::family_shown(g_menu.pad_in_use);
        const bool playstation = std::string_view(disruptor::settings::family_name(family)) == "PlayStation";
        for (size_t index = 0; index < kFaces.size() && !playstation; ++index) {
            const char *source = psx_input_pad_source(kFaces[index]);
            const disruptor::settings::PadSource *known = source ? disruptor::settings::pad_source(source) : nullptr;
            if (known) faces[index] = hint::key_word(disruptor::settings::printed(*known, family));
        }
        return hint::lines(faces, std::string(hint::in_language("LEFT", language)), language);
    }
#endif
    for (size_t index = 0; index < kFaces.size(); ++index) faces[index] = key_of(kFaces[index], language);
    return hint::lines(faces, std::string(hint::in_language("BACKSPACE", language)), language);
}

void draw_entry(CPUState *cpu, uint32_t) {
    const uint32_t returns = cpu->gpr[31];
    if ((returns != kReturnDrawn && returns != kReturnDrawnBelow) || psx_mod_read_word(kScreen) != kPassword) return;
    const uint32_t font = psx_mod_read_word(hint::kFont);
    std::vector<hint::Run> runs = hint::raster(
        lines_now(),
        [font](int shape, int column, int row) {
            return static_cast<int>(psx_mod_read_byte(font + static_cast<uint32_t>(shape * hint::kGlyphBytes + row * hint::kGlyphWide + column)));
        },
        [](int symbol, int u, int v) {
            const uint16_t pair = gpu_vram_peek(hint::kPageX + u / 2, hint::kPageY + hint::kSymbolV + hint::kSymbolSize * symbol + v);
            return static_cast<int>(u % 2 ? pair >> 8 : pair & 0xFFu);
        },
        [](int index) { return hint::palette_colour(gpu_vram_peek(hint::kClutX + index, hint::kClutY)); },
        [](int shape) {
            if (shape < 0 || shape >= 64) return 0;
            return static_cast<int>(static_cast<int8_t>(psx_mod_read_byte(pointer::kGlyphWidths + static_cast<uint32_t>(shape))));
        });
    {
        std::scoped_lock lock(g_mutex);
        g_runs = std::move(runs);
    }
    g_built.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_password_hint) {
    if (!psx_mod_register_function_entry_plugin("disruptor.password_hint.draw", kItemDraw, draw_entry))
        std::fprintf(stderr, "disruptor: no password hint hook, the password screen shows no keys\n");
}

}  // namespace

std::array<std::string, 4> face_names(std::string_view language) {
    std::array<std::string, 4> faces;
#if defined(PSX_SDL3)
    if (g_menu.pad_in_use.pad_last()) {
        const disruptor::settings::PadFamily family = disruptor::settings::family_shown(g_menu.pad_in_use);
        if (std::string_view(disruptor::settings::family_name(family)) == "PlayStation") {
            constexpr std::array<disruptor::state_browser::Word, 4> words = {disruptor::state_browser::Word::cross, disruptor::state_browser::Word::circle,
                                                                             disruptor::state_browser::Word::square, disruptor::state_browser::Word::triangle};
            for (size_t index = 0; index < faces.size(); ++index) faces[index] = disruptor::state_browser::word(words[index], language);
            return faces;
        }
        for (size_t index = 0; index < kFaces.size(); ++index) {
            const char *source = psx_input_pad_source(kFaces[index]);
            const disruptor::settings::PadSource *known = source ? disruptor::settings::pad_source(source) : nullptr;
            if (known) faces[index] = hint::key_word(disruptor::settings::printed(*known, family));
        }
        return faces;
    }
#endif
    for (size_t index = 0; index < kFaces.size(); ++index) faces[index] = key_of(kFaces[index], language);
    return faces;
}

bool password_hint_shown() {
    const std::chrono::steady_clock::duration built{g_built.load(std::memory_order_relaxed)};
    return std::chrono::steady_clock::now().time_since_epoch() - built < kStillShown;
}

void draw_password_hint(int width, int height) {
    if (!password_hint_shown()) return;
    std::vector<hint::Run> runs;
    {
        std::scoped_lock lock(g_mutex);
        runs = g_runs;
    }
    int numerator = 4, denominator = 3;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    for (const hint::Run &run : runs) {
        const std::optional<pointer::Point> from = pointer::window_point(run.x, run.y, width, height, numerator, denominator);
        const std::optional<pointer::Point> to =
            pointer::window_point(run.x + run.length, run.y + 1, width, height, numerator, denominator);
        if (!from || !to) return;
        draw->AddRectFilled(ImVec2(static_cast<float>(from->x), static_cast<float>(from->y)),
                            ImVec2(static_cast<float>(to->x), static_cast<float>(to->y)),
                            IM_COL32(run.colour.r, run.colour.g, run.colour.b, 255));
    }
}

}  // namespace disruptor::menu
