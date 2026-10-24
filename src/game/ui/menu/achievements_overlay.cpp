#include "game/ui/menu/menu.h"

namespace disruptor::menu {

namespace {

namespace achievements = disruptor::achievements;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kAtOnce = 3;
constexpr float kFade = 0.25f;

struct Up {
    achievements::Notice notice;
    Clock::time_point since;
};

struct Picture {
    unsigned texture = 0;
    bool tried = false;
};

std::mutex g_mutex;
std::deque<achievements::Notice> g_waiting;
std::vector<Up> g_up;
std::map<std::string, Picture> g_pictures;  // the render thread's alone

// The PNG as an RGB picture for the menu's renderer, 0 when it cannot be read. SDL3 alone reads PNG.
unsigned picture_of(const std::string& png) {
#if defined(PSX_SDL3)
    SDL_Surface* read = SDL_LoadPNG_IO(SDL_IOFromConstMem(png.data(), png.size()), true);
    if (!read) return 0;
    SDL_Surface* rgb = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(read);
    if (!rgb) return 0;
    std::vector<unsigned char> rows(static_cast<std::size_t>(rgb->w) * rgb->h * 3);
    for (int row = 0; row < rgb->h; ++row)
        std::memcpy(rows.data() + static_cast<std::size_t>(row) * rgb->w * 3,
                    static_cast<const unsigned char*>(rgb->pixels) + static_cast<std::size_t>(row) * rgb->pitch,
                    static_cast<std::size_t>(rgb->w) * 3);
    const unsigned texture = gl_renderer_picture(rows.data(), rgb->w, rgb->h);
    SDL_DestroySurface(rgb);
    return texture;
#else
    (void)png;
    return 0;
#endif
}

void gather() {
    std::vector<achievements::Notice> fresh = achievements::take_popups();
    const std::lock_guard lock(g_mutex);
    for (auto& notice : fresh) g_waiting.push_back(std::move(notice));
}

float seconds_since(Clock::time_point since) {
    return std::chrono::duration<float>(Clock::now() - since).count();
}

std::string heading(const achievements::Notice& notice) {
    using achievements::NoticeKind;
    switch (notice.kind) {
    case NoticeKind::unlocked:
        return std::string(notice.unofficial ? "Unofficial achievement" : "Achievement unlocked") + ", " +
               std::to_string(notice.points) + (notice.points == 1 ? " point" : " points");
    case NoticeKind::mastered: return "Every achievement unlocked";
    case NoticeKind::game_found: return disruptor_achievements_hardcore() ? "RetroAchievements, hardcore"
                                                                          : "RetroAchievements, casual";
    default: return {};
    }
}

std::string title_of(const achievements::Notice& notice) {
    switch (notice.kind) {
    case achievements::NoticeKind::mastered: return "Mastered " + notice.title;
    case achievements::NoticeKind::offline: return "Offline";
    case achievements::NoticeKind::online: return "Back online";
    default: return notice.title;
    }
}

std::string text_of(const achievements::Notice& notice) {
    if (notice.kind == achievements::NoticeKind::offline) return "Unlocks are sent when the connection is back";
    if (notice.kind == achievements::NoticeKind::online) return "The waiting unlocks were sent";
    return notice.text;
}

ImVec2 corner_point(int corner, int width, int height, float margin) {
    const bool right = corner == 0 || corner == 2;
    const bool bottom = corner >= 2;
    return {right ? width - margin : margin, bottom ? height - margin : margin};
}

// One card, its corner at `at`. Returns its height.
float card(const char* id, ImVec2 at, int corner, float scale, float alpha, const std::string& badge_url,
           const std::string& small, const std::string& title, const std::string& text, float width) {
    const bool right = corner == 0 || corner == 2;
    const bool bottom = corner >= 2;
    ImGui::SetNextWindowPos(at, ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, 4096.0f));
    ImGui::SetNextWindowBgAlpha(0.88f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 9.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * scale);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(scale);
    const float side = 48.0f * scale;
    if (const unsigned texture = achievement_picture(badge_url)) {
        ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(side, side));
        ImGui::SameLine(0.0f, 10.0f * scale);
    }
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(width - 10.0f * scale);
    if (!small.empty()) ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "%s", small.c_str());
    ImGui::TextUnformatted(title.c_str());
    if (!text.empty()) ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.70f, 1.0f), "%s", text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    const float height = ImGui::GetWindowHeight();
    ImGui::End();
    ImGui::PopStyleVar(3);
    return height;
}

}  // namespace

unsigned achievement_picture(const std::string& url) {
    if (url.empty()) return 0;
    Picture& picture = g_pictures[url];
    if (picture.tried) return picture.texture;
    const std::shared_ptr<const std::string> png = achievements::badge(url);
    if (!png) return 0;
    picture.tried = true;
    picture.texture = picture_of(*png);
    return picture.texture;
}

void release_achievement_pictures() {
    for (const auto& [url, picture] : g_pictures) gl_renderer_picture_free(picture.texture);
    g_pictures.clear();
}

bool achievements_overlay_shown() {
    gather();
    const achievements::Presentation shown = achievements::presentation();
    const std::lock_guard lock(g_mutex);
    const float stay = static_cast<float>(shown.seconds) + kFade;
    std::erase_if(g_up, [&](const Up& up) { return seconds_since(up.since) > stay; });
    while (g_up.size() < kAtOnce && !g_waiting.empty()) {
        g_up.push_back({std::move(g_waiting.front()), Clock::now()});
        g_waiting.pop_front();
    }
    return !g_up.empty() || achievements::progress_indicator().has_value();
}

void draw_achievements_overlay(int width, int height) {
    if (!achievements_overlay_shown()) return;
    const achievements::Presentation shown = achievements::presentation();
    std::vector<Up> up;
    {
        const std::lock_guard lock(g_mutex);
        up = g_up;
    }
    const float scale = std::clamp(static_cast<float>(height) / 720.0f, 1.0f, 3.0f);
    const float margin = 16.0f * scale;
    const float gap = 8.0f * scale;
    const bool bottom = shown.corner >= 2;
    ImVec2 at = corner_point(shown.corner, width, height, margin);
    int index = 0;
    for (const Up& card_up : up) {
        const float age = seconds_since(card_up.since);
        const float left = static_cast<float>(shown.seconds) - age;
        const float alpha = std::clamp(std::min(age, left) / kFade, 0.0f, 1.0f);
        const std::string id = "##achievement" + std::to_string(index++);
        const float tall = card(id.c_str(), at, shown.corner, scale, alpha, card_up.notice.badge, heading(card_up.notice),
                                title_of(card_up.notice), text_of(card_up.notice), 340.0f * scale);
        at.y += (bottom ? -1.0f : 1.0f) * (tall + gap);
    }
    if (const std::optional<achievements::Notice> indicator = achievements::progress_indicator())
        (void)card("##achievement_progress", at, shown.corner, scale * 0.85f, 1.0f, indicator->badge, {},
                   indicator->text, indicator->title, 260.0f * scale * 0.85f);
}

}  // namespace disruptor::menu
