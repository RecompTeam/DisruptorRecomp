#include "view.h"

#include "art.h"
#include "game_process.h"
#include "platform.h"
#include "icons.h"
#include "update_flow.h"

#include "game/settings/hint_text.h"
#include "game/settings/pad_layout.h"

#include "imgui.h"
#include "misc/freetype/imgui_freetype.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <charconv>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <random>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#ifndef DISRUPTOR_BUILD_VERSION
#define DISRUPTOR_BUILD_VERSION "unknown"
#endif

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

constexpr const char* kProjectPage = "https://github.com/RecompTeam/DisruptorRecomp";
constexpr const char* kReleasesPage = "https://github.com/RecompTeam/DisruptorRecomp/releases";
constexpr const char* kTeamPage = "https://github.com/RecompTeam";
constexpr const char* kDiscord = "https://discord.gg/hWbFatkXpU";
constexpr const char* kGuidePage = "https://github.com/RecompTeam/DisruptorRecomp/blob/main/docs/PLAYING.md";
constexpr float kFontSize = 16.0f, kSmallSize = 15.0f;
// Inter's thin stems drawn light on dark lose weight in grayscale blending: coverage of the regular face times 1.25.
constexpr float kRegularInk = 1.25f;
constexpr float kSidebarWidth = 216.0f, kCaptionHeight = 40.0f, kCaptionButton = 46.0f, kResizeEdge = 6.0f;

// The game's own colours: its menus are dark green, its logo red.
constexpr unsigned kBackground = 0x0b0f0c, kSurface = 0x121813, kRaised = 0x1a231b, kBorder = 0x263228;
// Text keeps WCAG AA contrast on every panel colour: muted 10.2:1 at worst, faint 5.1:1, red text 5.3:1.
constexpr unsigned kText = 0xe7ede7, kMuted = 0xc8cfc9, kFaint = 0x86948a, kLink = 0x9fd8a6, kRedText = 0xff5a5f;
constexpr unsigned kRed = 0xe3262b, kRedHover = 0xff3a3f, kRedPressed = 0xc41f24;
constexpr unsigned kGreen = 0x6fcf7a, kAmber = 0xe0a43a;
constexpr int kTriggerReleased = 8000, kTriggerPressed = 16384;

enum Resource { regular_font = 3, semibold_font = 4, extrabold_font = 5, icon_font = 6, players_guide = 8, changelog = 9 };

constexpr const char* page_notes[] = {
    "",
    "",
    "",
    "",
    "",
    "Switch on, play until the problem shows, close the game, then press Report a problem",
};
constexpr const char* page_icons[] = {icon::monitor, icon::sparkles, icon::gamepad_2, icon::volume_2, icon::flask_conical,
                                      icon::wrench};
constexpr std::array<SDL_DialogFileFilter, 3> image_filters = {{
    {"Disc images (*.cue;*.bin;*.iso;*.img)", "cue;bin;iso;img"},
    {"CUE sheets (*.cue)", "cue"},
    {"Raw images (*.bin;*.iso;*.img)", "bin;iso;img"},
}};

ImVec4 rgb(unsigned hex, float alpha = 1.0f) {
    return {static_cast<float>((hex >> 16) & 0xff) / 255.0f, static_cast<float>((hex >> 8) & 0xff) / 255.0f,
            static_cast<float>(hex & 0xff) / 255.0f, alpha};
}

ImU32 colour(unsigned hex, float alpha = 1.0f) { return ImGui::GetColorU32(rgb(hex, alpha)); }

ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }

std::string text_of(std::wstring_view text) { return utf8(fs::path(text)); }

// The first line of a message, for a place with room for one.
std::string first_line(std::string_view text) { return std::string(text.substr(0, text.find('\n'))); }

ImTextureID texture_id(SDL_Texture* texture) { return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture)); }

ImFont* font_from_resource(int id, const ImFontConfig& config, const ImWchar* ranges = nullptr) {
    const auto bytes = resource_bytes(id);
    if (bytes.empty()) return nullptr;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(bytes.data()), static_cast<int>(bytes.size()),
                                                      kFontSize, &config, ranges);
}

// A font of the launcher's own, with the icons and then a system font merged in for letters it has none of.
ImFont* load_font(int id, const char* system_file, float ink = 1.0f) {
    ImFontConfig own;
    own.FontDataOwnedByAtlas = false;
    own.RasterizerMultiply = ink;
    ImFont* font = font_from_resource(id, own);
    if (!font) return nullptr;
    ImFontConfig merged;
    merged.MergeMode = true;
    merged.FontDataOwnedByAtlas = false;
    merged.GlyphMinAdvanceX = kFontSize;
    merged.GlyphOffset = ImVec2(0.0f, 2.0f);
    static const ImWchar icon_ranges[] = {static_cast<ImWchar>(icon::first), static_cast<ImWchar>(icon::last), 0};
    font_from_resource(icon_font, merged, icon_ranges);
    const char* windows = SDL_getenv("WINDIR");
    const fs::path fallback = fs::path(windows ? windows : "C:\\Windows") / "Fonts" / system_file;
    std::error_code missing;
    if (fs::is_regular_file(fallback, missing)) {
        ImFontConfig system;
        system.MergeMode = true;
        system.RasterizerMultiply = ink;
        const std::u8string name = fallback.u8string();
        ImGui::GetIO().Fonts->AddFontFromFileTTF(reinterpret_cast<const char*>(name.c_str()), kFontSize, &system);
    }
    return font;
}

void wrapped(std::string_view text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
}

void muted(std::string_view text, unsigned hex = kMuted) {
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(hex));
    wrapped(text);
    ImGui::PopStyleColor();
}

void tooltip(const char* text) {
    if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", text);
}

const char* state_text(UpdateState state) {
    switch (state) {
    case UpdateState::unavailable: return "Off";
    case UpdateState::idle: return "Not checked yet";
    case UpdateState::checking: return "Checking...";
    case UpdateState::current: return "Up to date";
    case UpdateState::available: return "An update is available";
    case UpdateState::downloading: return "Downloading...";
    case UpdateState::ready: return "Downloaded: press Restart and update";
    case UpdateState::failed: return "Failed";
    }
    return "";
}

std::string version_text(const std::string& version) { return version.empty() ? std::string("-") : "v" + version; }

struct UpdateLook {
    const char* glyph;
    unsigned tint;
    std::string title;
    std::string line;
};

// The home page's update card and the Updates window say the same of a state.
UpdateLook update_look(const UpdateView& update) {
    switch (update.state) {
    case UpdateState::unavailable:
        return {icon::info, kMuted, "This copy updates by hand",
                "It was unzipped rather than installed by the updater\nNew releases are on the Releases page"};
    case UpdateState::idle:
        return {icon::refresh_cw, kMuted, "Updates not checked yet", "Check now, or let the launcher check when it starts"};
    case UpdateState::checking: return {icon::loader_circle, kAmber, "Checking for updates...", "Asking GitHub for the newest release"};
    case UpdateState::current: return {icon::circle_check, kGreen, "You have the newest version", version_text(update.installed)};
    case UpdateState::available:
        return {icon::download, kRed, "Version " + update.latest + " is available",
                "About " + size_text(update.download_bytes) + " to download" +
                    (update.delta ? ", only what changed\nIf that cannot be applied, the whole release: " + size_text(update.full_bytes)
                                  : "")};
    case UpdateState::downloading: return {icon::download, kRed, "Downloading version " + update.latest, ""};
    case UpdateState::ready:
        return {icon::circle_check, kGreen, "Version " + update.latest + " is ready to install",
                "Downloaded, each package checked\nThe launcher closes, installs it and opens again"};
    case UpdateState::failed:
        return {icon::circle_alert, 0xff7b72, "The update could not be completed",
                update.error.empty() ? std::string("The launcher's activity, under Details, says why") : "Why: " + first_line(update.error)};
    }
    return {icon::info, kMuted, "", ""};
}

// A slider's format string: the shown value with ImGui's own percent signs doubled.
std::string slider_format(const Option& option, int value) {
    std::string format;
    for (const char letter : text_of(value_text(option, value))) format += letter == '%' ? "%%" : std::string(1, letter);
    return format;
}


// A bordered panel of the raised colour, as tall as its contents when `height` is 0.
void card_begin(const char* id, float width, float height = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(kSurface));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize() * 0.9f));
    const ImGuiChildFlags fit = height > 0.0f ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Borders | fit | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

// How tall the card's contents want it, padding included.
float card_end() {
    const float wanted = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().WindowPadding.y;
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return wanted;
}

void card_label(const char* glyph, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(kMuted));
    ImGui::PushFont(nullptr, kFontSize * 0.8f);
    ImGui::Text("%s  %s", glyph, text);
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

// Three quarters of a circle turning once a second, for work under way.
void spinner(ImDrawList* draw, ImVec2 centre, float radius, ImU32 tint, float thickness) {
    const float start = static_cast<float>(ImGui::GetTime()) * 6.2831853f;
    draw->PathArcTo(centre, radius, start, start + 4.712389f, 24);
    draw->PathStroke(tint, ImDrawFlags_None, thickness);
}

void hand_when_hovered() {
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

void dashed_rect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 tint, float dash, float thickness) {
    const auto line = [&](ImVec2 from, ImVec2 to) {
        const float length = std::hypot(to.x - from.x, to.y - from.y);
        const ImVec2 step((to.x - from.x) / length, (to.y - from.y) / length);
        for (float at = 0.0f; at < length; at += dash * 2.0f) {
            const float end = std::min(at + dash, length);
            draw->AddLine(ImVec2(from.x + step.x * at, from.y + step.y * at), ImVec2(from.x + step.x * end, from.y + step.y * end),
                          tint, thickness);
        }
    };
    line(min, ImVec2(max.x, min.y));
    line(ImVec2(max.x, min.y), max);
    line(max, ImVec2(min.x, max.y));
    line(ImVec2(min.x, max.y), min);
}

// The dialog may answer after the window is gone, so its answer lives as long as the program.
View::Picked g_picked;

void SDLCALL picked_image(void*, const char* const* files, int) {
    std::scoped_lock lock(g_picked.mutex);
    g_picked.open = false;
    if (files && files[0]) g_picked.path = files[0];
}

bool dialog_is_open() {
    std::scoped_lock lock(g_picked.mutex);
    return g_picked.open;
}

} // namespace

View::View(Session& session, Updater& updater, Activity& activity, fs::path choices_file)
    : session_(session), updater_(updater), activity_(activity), choices_file_(std::move(choices_file)),
      choices_(read_choices(choices_file_)) {
    const auto guide = resource_bytes(players_guide);
    guide_ = guide_of(std::string_view(reinterpret_cast<const char*>(guide.data()), guide.size()));
    const auto changes = resource_bytes(changelog);
    this_notes_ = release_notes(std::string_view(reinterpret_cast<const char*>(changes.data()), changes.size()), DISRUPTOR_BUILD_VERSION);
}

View::~View() {
    for (SDL_Texture* texture : scenes_) SDL_DestroyTexture(texture);
    if (first_run_) SDL_DestroyTexture(first_run_);
    if (app_icon_) SDL_DestroyTexture(app_icon_);
}

void View::attach(SDL_Window* window, SDL_Renderer* renderer) {
    window_ = window;
    renderer_ = renderer;
    first_run_ = first_run_texture(renderer);
    app_icon_ = app_icon_texture(renderer);
}

void View::style(float scale) {
    scale_ = scale;
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = {0.0f, 0.0f};
    style.FramePadding = {12.0f, 7.0f};
    style.ItemSpacing = {10.0f, 9.0f};
    style.ItemInnerSpacing = {8.0f, 6.0f};
    style.CellPadding = {8.0f, 7.0f};
    style.WindowRounding = 8.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 14.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.SelectableTextAlign = {0.0f, 0.5f};
    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = rgb(kText);
    colors[ImGuiCol_TextDisabled] = rgb(kMuted);
    colors[ImGuiCol_WindowBg] = rgb(kBackground);
    colors[ImGuiCol_ChildBg] = rgb(0, 0.0f);
    colors[ImGuiCol_PopupBg] = rgb(kSurface);
    colors[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.6f);
    colors[ImGuiCol_Border] = rgb(kBorder);
    colors[ImGuiCol_FrameBg] = rgb(kRaised);
    colors[ImGuiCol_FrameBgHovered] = rgb(0x222d23);
    colors[ImGuiCol_FrameBgActive] = rgb(0x29362a);
    colors[ImGuiCol_Button] = rgb(kRaised);
    colors[ImGuiCol_ButtonHovered] = rgb(0x243026);
    colors[ImGuiCol_ButtonActive] = rgb(0x2c3a2e);
    colors[ImGuiCol_Header] = rgb(kRaised);
    colors[ImGuiCol_HeaderHovered] = rgb(0x1f2a20);
    colors[ImGuiCol_HeaderActive] = rgb(0x243026);
    colors[ImGuiCol_CheckMark] = rgb(kRed);
    colors[ImGuiCol_SliderGrab] = rgb(kRed);
    colors[ImGuiCol_SliderGrabActive] = rgb(kRedHover);
    colors[ImGuiCol_PlotHistogram] = rgb(kRed);
    colors[ImGuiCol_Separator] = rgb(kBorder);
    colors[ImGuiCol_ScrollbarBg] = rgb(0, 0.0f);
    colors[ImGuiCol_ScrollbarGrab] = rgb(0x2a362c);
    colors[ImGuiCol_ScrollbarGrabHovered] = rgb(0x354437);
    colors[ImGuiCol_TextLink] = rgb(kLink);
    colors[ImGuiCol_NavCursor] = rgb(kRedHover);
    colors[ImGuiCol_TableBorderLight] = rgb(kBorder);
    colors[ImGuiCol_TableRowBgAlt] = rgb(0xffffff, 0.02f);
    colors[ImGuiCol_TitleBg] = rgb(kSurface);
    colors[ImGuiCol_TitleBgActive] = rgb(kRaised);
    colors[ImGuiCol_TitleBgCollapsed] = rgb(kSurface);
    colors[ImGuiCol_ResizeGrip] = rgb(kBorder);
    colors[ImGuiCol_ResizeGripHovered] = rgb(0x354437);
    colors[ImGuiCol_ResizeGripActive] = rgb(kRed);
    style.DisabledAlpha = 0.7f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    style.FontSizeBase = kFontSize;
    if (body_) return;
    // Snapped to the pixel grid vertically only, as ClearType does: crisp small text that keeps its spacing.
    ImGui::GetIO().Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
    body_ = load_font(regular_font, "segoeui.ttf", kRegularInk);
    if (!body_) body_ = ImGui::GetIO().Fonts->AddFontDefault();
    bold_ = load_font(semibold_font, "segoeuib.ttf");
    if (!bold_) bold_ = body_;
    ImFontConfig heavy;
    heavy.FontDataOwnedByAtlas = false;
    heavy_ = font_from_resource(extrabold_font, heavy);
    if (!heavy_) heavy_ = bold_;
}

void View::load_art() {
    const fs::path disc = session_.verified().data;
    if (disc.empty() || disc == art_disc_) return;
    art_disc_ = disc;
    for (SDL_Texture* texture : scenes_) SDL_DestroyTexture(texture);
    scenes_.clear();
    for (const Picture& scene : menu_scenes(disc))
        if (SDL_Texture* texture = picture_texture(renderer_, scene)) scenes_.push_back(texture);
    std::random_device seed;
    scene_ = scenes_.empty() ? 0 : std::uniform_int_distribution<std::size_t>(0, scenes_.size() - 1)(seed);
}

bool View::draw() {
    take_picked();
    load_art();
    const UpdateView update = updater_.view();
    if (screen_ != Screen::settings || page_ != static_cast<int>(Page::controls)) {
        listening_.clear();
        keys_read_ = false;
        stop_pad_capture();
        pad_map_read_ = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("launcher", nullptr, flags);
    // A key a modal took this frame, Escape closing Details for one, must not also cancel the task behind it.
    const bool modal_before = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    caption();
    sidebar(update);
    ImGui::SameLine(0.0f, 0.0f);
    const float gutter = 24.0f * scale_;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(gutter, 14.0f * scale_));
    ImGui::BeginChild("page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    bool leave = false;
    switch (screen_) {
    case Screen::home: leave = home(update); break;
    case Screen::settings: settings(); break;
    case Screen::help: help(); break;
    case Screen::about: about(); break;
    }
    earlier_popup();
    details_popup();
    if (updates_popup(update)) leave = true;
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if (!modal_before && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && session_.playable() && !dialog_is_open()) session_.play();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && session_.cancellable()) session_.cancel();
        if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) screen_ = Screen::help;
    }
    // Without the system's frame a line sets the window off from what is behind it.
    if (!window_ || !(SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED))
        ImGui::GetForegroundDrawList()->AddRect(viewport->Pos, viewport->Pos + viewport->Size, colour(kBorder));
    ImGui::End();
    return leave;
}

void View::caption() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = kCaptionHeight * scale_, side = kSidebarWidth * scale_, button = kCaptionButton * scale_;
    draw->AddRectFilled(top, top + ImVec2(side, height), colour(kSurface));
    draw->AddLine(top + ImVec2(side - 1.0f, 0.0f), top + ImVec2(side - 1.0f, height), colour(kBorder));
    float x = top.x + 16.0f * scale_;
    if (app_icon_) {
        const float icon_size = 20.0f * scale_;
        const ImVec2 at(x, top.y + (height - icon_size) * 0.5f);
        draw->AddImageRounded(texture_id(app_icon_), at, at + ImVec2(icon_size, icon_size), ImVec2(0, 0), ImVec2(1, 1),
                              IM_COL32_WHITE, 5.0f * scale_);
        x += icon_size + 10.0f * scale_;
    }
    draw->AddText(bold_, ImGui::GetFontSize(), ImVec2(x, top.y + (height - ImGui::GetFontSize()) * 0.5f), colour(kText),
                  "Disruptor Recompiled");

    const bool maximized = window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED);
    const float left = top.x + width - button * 3.0f;
    bar_ = TitleBar{height, left - ImGui::GetMainViewport()->Pos.x, kResizeEdge * scale_};
    constexpr const char* names[] = {"Minimize", "Maximize", "Close"};
    const float line = std::max(1.0f, std::floor(scale_));
    const float half = std::floor(5.0f * scale_);
    for (int index = 0; index < 3; ++index) {
        const ImVec2 at(left + button * static_cast<float>(index), top.y);
        const ImVec2 end = at + ImVec2(button, height);
        const bool hovered = ImGui::IsMouseHoveringRect(at, end, false);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) caption_pressed_ = index;
        const bool held = hovered && caption_pressed_ == index && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (hovered && caption_pressed_ == index && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && window_) {
            if (index == 0) SDL_MinimizeWindow(window_);
            else if (index == 1 && maximized) SDL_RestoreWindow(window_);
            else if (index == 1) SDL_MaximizeWindow(window_);
            else {
                SDL_Event quit{};
                quit.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit);
            }
        }
        const bool closing = index == 2;
        if (hovered || held)
            draw->AddRectFilled(at, end, colour(closing ? (held ? kRedPressed : kRed) : (held ? 0x29362a : 0x1f2a20)));
        const ImU32 ink = colour(closing && hovered ? 0xffffff : hovered ? kText : kMuted);
        const ImVec2 centre(std::floor(at.x + button * 0.5f) + 0.5f, std::floor(at.y + height * 0.5f) + 0.5f);
        if (index == 0) {
            draw->AddLine(centre + ImVec2(-half, 0.0f), centre + ImVec2(half, 0.0f), ink, line);
        } else if (index == 1 && !maximized) {
            draw->AddRect(centre + ImVec2(-half, -half), centre + ImVec2(half, half), ink, 0.0f, 0, line);
        } else if (index == 1) {
            const float step = std::floor(2.0f * scale_);
            draw->AddRect(centre + ImVec2(-half, -half + step), centre + ImVec2(half - step, half), ink, 0.0f, 0, line);
            draw->AddLine(centre + ImVec2(-half + step, -half + step), centre + ImVec2(-half + step, -half), ink, line);
            draw->AddLine(centre + ImVec2(-half + step, -half), centre + ImVec2(half, -half), ink, line);
            draw->AddLine(centre + ImVec2(half, -half), centre + ImVec2(half, half - step), ink, line);
            draw->AddLine(centre + ImVec2(half, half - step), centre + ImVec2(half - step, half - step), ink, line);
        } else {
            draw->AddLine(centre + ImVec2(-half, -half), centre + ImVec2(half, half), ink, line);
            draw->AddLine(centre + ImVec2(-half, half), centre + ImVec2(half, -half), ink, line);
        }
        if (hovered && caption_hovered_ != index) {
            caption_hovered_ = index;
            caption_hovered_since_ = ImGui::GetTime();
        } else if (!hovered && caption_hovered_ == index) {
            caption_hovered_ = -1;
        }
        if (hovered && !ImGui::IsAnyMouseDown() && ImGui::GetTime() - caption_hovered_since_ > 0.6)
            ImGui::SetTooltip("%s", index == 1 && maximized ? "Restore down" : names[index]);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) caption_pressed_ = -1;
    ImGui::SetCursorScreenPos(top + ImVec2(0.0f, height));
}

void View::sidebar(const UpdateView& update) {
    const float width = kSidebarWidth * scale_;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(kSurface));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale_, 6.0f * scale_));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::BeginChild("sidebar", ImVec2(width, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner = ImGui::GetWindowPos();
    draw->AddLine(ImVec2(corner.x + width - 1.0f, corner.y), ImVec2(corner.x + width - 1.0f, corner.y + ImGui::GetWindowHeight()),
                  colour(kBorder));

    // A short window draws the rows closer so the foot stays in view.
    const bool tight = ImGui::GetMainViewport()->Size.y < 700.0f * scale_;
    const auto item = [&](const char* glyph, const char* label, bool selected, const char* badge = nullptr, float indent = 0.0f,
                          bool open = false) {
        ImGui::PushID(label);
        const float row = (indent > 0.0f ? (tight ? 29.0f : 34.0f) : (tight ? 36.0f : 40.0f)) * scale_;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float wide = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##item", ImVec2(wide, row));
        const bool hovered = ImGui::IsItemHovered();
        hand_when_hovered();
        if (selected || hovered)
            draw->AddRectFilled(at, at + ImVec2(wide, row), colour(selected ? kRaised : 0x172018), 8.0f * scale_);
        if (selected)
            draw->AddRectFilled(at + ImVec2(0.0f, 9.0f * scale_), at + ImVec2(3.0f * scale_, row - 9.0f * scale_), colour(kRed),
                                2.0f * scale_);
        const ImU32 tint = colour(selected || open ? 0xffffff : 0xc5cfc6);
        const float middle = at.y + (row - ImGui::GetFontSize()) * 0.5f;
        ImFont* font = selected || open ? bold_ : body_;
        draw->AddText(font, ImGui::GetFontSize(), ImVec2(at.x + (14.0f + indent) * scale_, middle), tint, glyph);
        draw->AddText(font, ImGui::GetFontSize(), ImVec2(at.x + (44.0f + indent) * scale_, middle), tint, label);
        if (badge) {
            const ImVec2 size = ImGui::CalcTextSize(badge);
            const float right = at.x + wide - 10.0f * scale_;
            const ImVec2 top(right - size.x * 0.8f - 14.0f * scale_, at.y + row * 0.5f - 9.0f * scale_);
            draw->AddRectFilled(top, ImVec2(right, top.y + 18.0f * scale_), colour(kRed), 9.0f * scale_);
            draw->AddText(bold_, ImGui::GetFontSize() * 0.75f, ImVec2(top.x + 7.0f * scale_, top.y + 2.0f * scale_), IM_COL32_WHITE,
                          badge);
        }
        ImGui::PopID();
        return clicked;
    };
    if (item(icon::play, "Play", screen_ == Screen::home)) screen_ = Screen::home;
    if (item(icon::sliders_horizontal, "Settings", false, nullptr, 0.0f, screen_ == Screen::settings)) screen_ = Screen::settings;
    int index = 0;
    for (const wchar_t* label : page_labels()) {
        const std::string text = text_of(label);
        ImGui::PushID(index);
        if (item(page_icons[std::min<std::size_t>(static_cast<std::size_t>(index), std::size(page_icons) - 1)], text.c_str(),
                 screen_ == Screen::settings && page_ == index, nullptr, 14.0f)) {
            screen_ = Screen::settings;
            page_ = index;
        }
        ImGui::PopID();
        ++index;
    }
    const bool waiting = update.state == UpdateState::available || update.state == UpdateState::ready;

    const bool free = !session_.busy() && !session_.game_running();
    const bool dialog_open = dialog_is_open();
    const float utility = 32.0f * scale_;
    const float bottom = utility * 5.0f + ImGui::GetTextLineHeightWithSpacing() * (waiting ? 2.0f : 1.0f) +
                         ImGui::GetStyle().ItemSpacing.y * 6.0f;
    const float gap = 8.0f * scale_;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(gap, ImGui::GetContentRegionAvail().y - bottom));
    const auto small = [&](const char* glyph, const char* label, bool enabled, const char* hint, bool current = false) {
        ImGui::PushID(label);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float wide = ImGui::GetContentRegionAvail().x;
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::InvisibleButton("##utility", ImVec2(wide, utility));
        const bool hovered = ImGui::IsItemHovered();
        if (enabled) hand_when_hovered();
        ImGui::EndDisabled();
        tooltip(hint);
        const ImU32 tint = colour(!enabled ? kFaint : hovered || current ? kText : kMuted);
        const float size = ImGui::GetFontSize() * 0.9f;
        const float middle = at.y + (utility - size) * 0.5f;
        draw->AddText(body_, size, ImVec2(at.x + 14.0f * scale_, middle), tint, glyph);
        draw->AddText(body_, size, ImVec2(at.x + 42.0f * scale_, middle), tint, label);
        ImGui::PopID();
        return clicked && enabled;
    };
    if (small(icon::folder_open, "Saves folder", true, ("Your saves and settings are in " + utf8(session_.data())).c_str()))
        session_.open_saves();
    if (small(icon::folder_input, "Bring saves...", free && !dialog_open,
              "Brings the saves, settings, key bindings and disc of an earlier copy here, such as an unzipped release"))
        pick(Wanted::earlier);
    // A report can be made at any time, an import or a running game included.
    if (small(icon::bug, "Report a problem", true, "Saves the game's logs, your settings and a note of the system for a bug report"))
        session_.make_problem_report();
    if (small(icon::circle_help, "Help", true, "The guide to setting up and playing\nF1 opens it too", screen_ == Screen::help))
        screen_ = Screen::help;
    if (small(icon::info, "About", true, nullptr, screen_ == Screen::about)) screen_ = Screen::about;
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(kFaint));
    ImGui::PushFont(nullptr, kFontSize * 0.8f);
    ImGui::PushStyleColor(ImGuiCol_TextLink, rgb(kFaint));
    if (!version_hidden_) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f * scale_);
        if (ImGui::TextLink(shown_version(DISRUPTOR_BUILD_VERSION).c_str())) updates_open_ = true;
        hand_when_hovered();
        tooltip("Updates: this version, the newest one and the release notes");
    }
    ImGui::PopStyleColor(2);
    // Another page hides the home page's update card: the foot of the side bar says one is waiting.
    if (waiting) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f * scale_);
        ImGui::PushStyleColor(ImGuiCol_TextLink, rgb(kGreen));
        if (ImGui::TextLink(update.state == UpdateState::ready ? "Update ready to install" : "Update available")) screen_ = Screen::home;
        hand_when_hovered();
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

bool View::home(const UpdateView& update) {
    const bool dialog_open = dialog_is_open();
    const bool known = session_.verified().profile || !scenes_.empty();
    const float room = ImGui::GetContentRegionAvail().y;
    banner(std::clamp(room * (known ? 0.4f : 0.48f), 220.0f * scale_, 360.0f * scale_), dialog_open);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
    if (!known) {
        first_run(dialog_open);
        // Before a disc the drop zone needs the room: only an update that waits for the player is shown.
        const bool waiting = update.state == UpdateState::available || update.state == UpdateState::downloading ||
                             update.state == UpdateState::ready;
        return waiting && update_strip(update);
    }
    cards(dialog_open);
    // The update card stays while there is something to do about an update. Otherwise the notes take its place.
    const bool acting = update.state == UpdateState::available || update.state == UpdateState::downloading ||
                        update.state == UpdateState::ready || update.state == UpdateState::failed ||
                        update.state == UpdateState::checking;
    if (!acting && this_notes_ && news(update)) return false;
    const bool leave = update_strip(update);
    news(update);
    return leave;
}

// What's new: a newer release's notes while one waits, this version's from the embedded CHANGELOG.md otherwise.
bool View::news(const UpdateView& update) {
    const bool newer = !update.notes.empty() && (update.state == UpdateState::available || update.state == UpdateState::downloading ||
                                                 update.state == UpdateState::ready);
    if (!newer && !this_notes_) return false;
    const std::string& markdown = newer ? update.notes : this_notes_->markdown;
    const std::string version = newer ? "v" + update.latest : this_notes_->version;
    const float height = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y - 2.0f * scale_;
    if (height < 100.0f * scale_ || markdown.empty()) return false;
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    if (markdown != news_source_) {
        news_source_ = markdown;
        news_blocks_ = parse_markdown(news_source_);
    }
    card_begin("news", 0.0f, height);
    const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    card_label(icon::sparkles, newer ? "WHAT'S NEW IN THE NEXT VERSION" : "WHAT'S NEW IN THIS VERSION");
    ImGui::PushFont(bold_, 0.0f);
    ImGui::TextUnformatted(version.c_str());
    ImGui::PopFont();
    ImGui::EndGroup();
    if (!newer) {
        const char* more = "Update options";
        ImGui::SameLine(right_edge - bold_->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, more).x);
        if (link(more)) updates_open_ = true;
    }
    ImGui::BeginChild("news text", ImVec2(0.0f, 0.0f));
    blocks(news_blocks_, ImGui::GetContentRegionAvail().x - 14.0f * scale_, true);
    ImGui::EndChild();
    card_end();
    return true;
}

void View::banner(float height, bool dialog_open) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 bottom = top + ImVec2(width, height);
    const float rounding = 12.0f * scale_;
    const bool game_art = !scenes_.empty();
    SDL_Texture* art = game_art ? scenes_[scene_ % scenes_.size()] : first_run_;
    if (art) {
        // Whole pixels of the 320 by 240 picture, as many as fill the banner's width, from its top where the logo is.
        const float pixel = std::max(std::ceil(width / 320.0f), std::ceil(height / 240.0f));
        const float shown_x = width / pixel / 320.0f, shown_y = height / pixel / 240.0f;
        const float left = (1.0f - shown_x) * 0.5f;
        const float upper = game_art ? 0.0f : (1.0f - shown_y) * 0.5f;
        draw->AddImageRounded(texture_id(art), top, bottom, ImVec2(left, upper), ImVec2(left + shown_x, upper + shown_y),
                              IM_COL32_WHITE, rounding);
    } else {
        draw->AddRectFilled(top, bottom, colour(kSurface), rounding);
    }
    draw->AddRectFilledMultiColor(ImVec2(top.x, top.y + height * 0.38f), bottom, colour(kBackground, 0.0f),
                                  colour(kBackground, 0.0f), colour(kBackground, 0.94f), colour(kBackground, 0.94f));
    draw->AddRect(top, bottom, colour(kBorder), rounding);
    const float pad = 24.0f * scale_;
    if (!game_art) {
        draw->AddText(heavy_, 42.0f * scale_, top + ImVec2(28.0f * scale_, 26.0f * scale_), colour(0xf2f5f2), "DISRUPTOR");
        std::string spaced;
        for (const char letter : std::string_view("RECOMPILED")) (spaced += letter) += ' ';
        draw->AddText(bold_, 13.0f * scale_, top + ImVec2(30.0f * scale_, 74.0f * scale_), colour(kRed), spaced.c_str());
    }

    const bool busy = session_.busy(), running = session_.game_running(), playable = session_.playable();
    const unsigned dot = busy ? kAmber : running || playable ? kGreen : kAmber;
    const std::string said = busy      ? first_line(text_of(session_.status()))
                             : running ? std::string("Game running")
                             : playable ? std::string("Ready to play")
                                        : std::string("Add your game disc to play");
    ImFont* pill_font = bold_;
    const float pill_size = ImGui::GetFontSize() * 0.9f;
    const ImVec2 said_size = pill_font->CalcTextSizeA(pill_size, width * 0.55f, 0.0f, said.c_str());
    const float pill_height = 28.0f * scale_;
    const bool sub = playable && !busy;
    const ImVec2 pill(top.x + pad, bottom.y - pad - pill_height - (sub ? 26.0f * scale_ : 0.0f));
    draw->AddRectFilled(pill, pill + ImVec2(said_size.x + 40.0f * scale_, pill_height), colour(0x0a0e0b, 0.82f), pill_height * 0.5f);
    draw->AddRect(pill, pill + ImVec2(said_size.x + 40.0f * scale_, pill_height), colour(kBorder), pill_height * 0.5f);
    if (busy) {
        spinner(draw, pill + ImVec2(17.0f * scale_, pill_height * 0.5f), 5.0f * scale_, colour(dot), 2.0f * scale_);
    } else {
        draw->AddCircleFilled(pill + ImVec2(17.0f * scale_, pill_height * 0.5f), 4.0f * scale_, colour(dot));
    }
    draw->PushClipRect(pill, pill + ImVec2(said_size.x + 40.0f * scale_, pill_height), true);
    draw->AddText(pill_font, pill_size, pill + ImVec2(30.0f * scale_, (pill_height - pill_size) * 0.5f), colour(kText), said.c_str());
    draw->PopClipRect();
    float taken = pill.x + said_size.x + 40.0f * scale_;
    if (sub) {
        const LanguageList languages = session_.languages();
        std::string line = text_of(session_.verified().profile->label);
        if (languages.selected > 0 && languages.selected < static_cast<int>(languages.labels.size()))
            line += "  \xc2\xb7  " + text_of(languages.labels[static_cast<std::size_t>(languages.selected)]);
        else line += "  \xc2\xb7  English";
        draw->AddText(body_, ImGui::GetFontSize() * 0.9f, ImVec2(pill.x, pill.y + pill_height + 8.0f * scale_), colour(kMuted), line.c_str());
        taken = std::max(taken, pill.x + body_->CalcTextSizeA(ImGui::GetFontSize() * 0.9f, FLT_MAX, 0.0f, line.c_str()).x);
    }

    const ImVec2 play_size(210.0f * scale_, 56.0f * scale_);
    const ImVec2 play_at(bottom.x - pad - play_size.x, bottom.y - pad - play_size.y);
    if (busy && session_.cancellable()) {
        ImGui::SetCursorScreenPos(ImVec2(bottom.x - pad - ImGui::CalcTextSize("Cancel").x - ImGui::GetStyle().FramePadding.x * 2.0f,
                                         bottom.y - pad - ImGui::GetFrameHeight()));
        if (ImGui::Button("Cancel")) session_.cancel();
    } else {
        ImGui::SetCursorScreenPos(play_at);
        ImGui::BeginDisabled(!playable || dialog_open);
        const bool pressed = ImGui::InvisibleButton("play", play_size);
        const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        if (playable) hand_when_hovered();
        ImGui::EndDisabled();
        if (!playable)
            tooltip(running ? "The game is running" : busy ? "Wait for the task to finish" : "Add your game disc first");
        const unsigned fill = !playable ? 0x232b24 : held ? kRedPressed : hovered ? kRedHover : kRed;
        const ImU32 ink = colour(playable ? 0xffffff : 0x66736a);
        draw->AddRectFilled(play_at, play_at + play_size, colour(fill), 10.0f * scale_);
        const float letters = 20.0f * scale_;
        const float spacing = 2.5f * scale_;
        float label_width = 0.0f;
        for (const char letter : std::string_view("PLAY")) label_width += heavy_->CalcTextSizeA(letters, FLT_MAX, 0.0f, &letter, &letter + 1).x + spacing;
        const float glyph_width = body_->CalcTextSizeA(letters, FLT_MAX, 0.0f, icon::play).x;
        float x = play_at.x + (play_size.x - label_width - glyph_width - 12.0f * scale_) * 0.5f;
        const float y = play_at.y + (play_size.y - letters) * 0.5f;
        draw->AddText(body_, letters, ImVec2(x, y + 1.0f * scale_), ink, icon::play);
        x += glyph_width + 12.0f * scale_;
        for (const char letter : std::string_view("PLAY")) {
            draw->AddText(heavy_, letters, ImVec2(x, y), ink, &letter, &letter + 1);
            x += heavy_->CalcTextSizeA(letters, FLT_MAX, 0.0f, &letter, &letter + 1).x + spacing;
        }
        if (pressed && playable) session_.play();
        if (!busy) last_activity(taken + pad, play_at.x - 18.0f * scale_, play_at.y + play_size.y * 0.5f);
    }
    if (busy) {
        const float done = std::clamp(static_cast<float>(session_.progress()) / 100.0f, 0.0f, 1.0f);
        const float inset = rounding;
        draw->AddRectFilled(ImVec2(top.x + inset, bottom.y - 3.0f * scale_),
                            ImVec2(top.x + inset + (width - 2.0f * inset) * done, bottom.y), colour(kRed), 1.5f * scale_);
    }
    ImGui::SetCursorScreenPos(top);
    ImGui::Dummy(ImVec2(width, height));
}

void View::first_run(bool dialog_open) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = 196.0f * scale_;
    const ImVec2 bottom = top + ImVec2(width, height);
    const bool free = !session_.busy() && !session_.game_running();
    draw->AddRectFilled(top, bottom, colour(kSurface, 0.9f), 12.0f * scale_);
    dashed_rect(draw, top + ImVec2(6.0f * scale_, 6.0f * scale_), bottom + ImVec2(-6.0f * scale_, -6.0f * scale_),
                colour(dragging_ ? kRed : 0x3a4a3c), 7.0f * scale_, 2.0f * scale_);
    const auto centred = [&](ImFont* font, float size, unsigned hex, const char* text, float y) {
        const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        draw->AddText(font, size, ImVec2(top.x + (width - measured.x) * 0.5f, top.y + y), colour(hex), text);
    };
    centred(body_, 30.0f * scale_, dragging_ ? kRed : 0x9fb39f, icon::disc_3, 22.0f * scale_);
    centred(bold_, 17.0f * scale_, kText, dragging_ ? "Drop it to import" : "Drop your Disruptor (USA) disc image here", 66.0f * scale_);
    centred(body_, kSmallSize * scale_, kMuted, "BIN/CUE, ISO or IMG\nIt is copied and checked, your file stays where it is",
            94.0f * scale_);
    const std::string choose = std::string(icon::upload) + "  Choose file...";
    const char* earlier = "Played an earlier copy? Bring its saves";
    const char* start = "Start the game after importing";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float choose_width = ImGui::CalcTextSize(choose.c_str()).x + style.FramePadding.x * 2.0f;
    const float start_width = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(start).x;
    const float earlier_width = ImGui::CalcTextSize(earlier).x;
    const float gap = 18.0f * scale_;
    const float row = choose_width + gap + start_width + gap + earlier_width;
    ImGui::SetCursorScreenPos(ImVec2(top.x + std::max(gap, (width - row) * 0.5f), top.y + 128.0f * scale_));
    ImGui::BeginDisabled(!free || dialog_open);
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(kRed));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(kRedHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(kRedPressed));
    ImGui::PushStyleColor(ImGuiCol_Border, rgb(kRed));
    ImGui::PushFont(bold_, 0.0f);
    if (ImGui::Button(choose.c_str())) pick(Wanted::image);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    ImGui::SameLine(0.0f, gap);
    ImGui::Checkbox(start, &session_.auto_play);
    ImGui::SameLine(0.0f, gap);
    ImGui::AlignTextToFramePadding();
    if (link(earlier)) pick(Wanted::earlier);
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(top);
    ImGui::Dummy(ImVec2(width, height + 6.0f * scale_));
}

void View::cards(bool dialog_open) {
    const bool free = !session_.busy() && !session_.game_running();
    const float gap = 18.0f * scale_;
    const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    // The two cards stand side by side at the height the taller one wanted in the frame before.
    card_begin("disc", half, cards_height_);
    card_label(icon::disc_3, "GAME DISC");
    const VerifiedDisc& disc = session_.verified();
    ImGui::PushFont(bold_, kFontSize * 1.1f);
    ImGui::TextUnformatted(disc.profile ? text_of(disc.profile->label).c_str() : "Not verified yet");
    ImGui::PopFont();
    ImGui::AlignTextToFramePadding();
    if (disc.profile) {
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(kGreen));
        ImGui::TextUnformatted(icon::circle_check);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 6.0f * scale_);
        ImGui::TextDisabled("Verified");
    } else {
        ImGui::TextDisabled("%s", session_.busy() ? "Checking..." : "Choose your disc image again");
    }
    ImGui::SameLine();
    const char* change = "Change...";
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(change).x -
                                                                     ImGui::GetStyle().FramePadding.x * 2.0f));
    ImGui::BeginDisabled(!free || dialog_open);
    if (ImGui::Button(change)) pick(Wanted::image);
    ImGui::EndDisabled();
    const float disc_wanted = card_end();

    ImGui::SameLine(0.0f, gap);
    card_begin("language", half, cards_height_);
    card_label(icon::languages, "LANGUAGE");
    const LanguageList languages = session_.languages();
    const bool editable = session_.editable();
    ImGui::BeginDisabled(!editable);
    const float buttons = ImGui::CalcTextSize("Add disc...").x + ImGui::GetStyle().FramePadding.x * 2.0f +
                          (languages.selected > 0 ? ImGui::CalcTextSize(icon::x).x + ImGui::GetStyle().FramePadding.x * 2.0f +
                                                        ImGui::GetStyle().ItemSpacing.x
                                                  : 0.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons - ImGui::GetStyle().ItemSpacing.x);
    const std::string chosen = languages.selected >= 0 && languages.selected < static_cast<int>(languages.labels.size())
                                   ? text_of(languages.labels[static_cast<std::size_t>(languages.selected)]) : std::string();
    if (ImGui::BeginCombo("##language", chosen.c_str())) {
        for (std::size_t index = 0; index < languages.labels.size(); ++index) {
            const std::string label = text_of(languages.labels[index]);
            if (ImGui::Selectable(label.c_str(), static_cast<int>(index) == languages.selected) &&
                static_cast<int>(index) != languages.selected)
                session_.choose_language(index);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(dialog_open);
    if (ImGui::Button("Add disc...")) pick(Wanted::language);
    ImGui::EndDisabled();
    if (languages.selected > 0) {
        ImGui::SameLine();
        if (ImGui::Button(icon::x)) session_.remove_language(static_cast<std::size_t>(languages.selected));
        tooltip("Takes this disc off the list\nIts file is left as it is");
    }
    ImGui::EndDisabled();
    ImGui::PushFont(nullptr, kFontSize * 0.85f);
    muted(languages.note.empty() ? "French, German or Japanese from your own disc" : text_of(languages.note), kFaint);
    ImGui::PopFont();
    const float wanted = std::max(disc_wanted, card_end());
    if (wanted != cards_height_) redraw_ = true;
    cards_height_ = wanted;
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
}

// The launcher's last message and its Details link in the banner, ending at `right` on the line through `middle`,
// cut so that it starts no farther left than `left`.
void View::last_activity(float left, float right, float middle) {
    std::string status = first_line(text_of(session_.status()));
    if (status.empty()) return;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float link_width = bold_->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, "Details").x;
    const float icon_width = ImGui::CalcTextSize(icon::history).x;
    const float room = right - left - link_width - icon_width - 3.0f * gap;
    if (room <= 0.0f) return;
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize(), line = ImGui::GetTextLineHeight();
    // Two lines at most, the words that do not fit cut off at the end.
    const auto block = [&](const std::string& text) { return font->CalcTextSizeA(size, FLT_MAX, room, text.c_str()); };
    for (std::size_t cut = status.size(); block(status).y > 2.0f * line + 1.0f && cut > 0;) {
        cut = status.rfind(' ', cut - 1);
        if (cut == std::string::npos) cut = 0;
        status = status.substr(0, cut) + "...";
    }
    const ImVec2 shown = block(status);
    const float y = middle - shown.y * 0.5f;
    float x = right - link_width - gap - shown.x - gap - icon_width;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddText(ImVec2(x, middle - line * 0.5f), colour(kMuted), icon::history);
    x += icon_width + gap;
    draw->AddText(font, size, ImVec2(x, y), colour(kMuted), status.c_str(), nullptr, room);
    ImGui::SetCursorScreenPos(ImVec2(right - link_width, middle - line * 0.5f));
    if (link("Details")) details_ = true;
}

bool View::update_strip(const UpdateView& update) {
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    card_begin("update", 0.0f);
    const float right_edge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    const UpdateLook look = update_look(update);
    const float side = ImGui::GetFontSize() * 1.3f;
    if (update.state == UpdateState::checking) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        spinner(ImGui::GetWindowDrawList(), at + ImVec2(side * 0.5f, side * 0.6f), side * 0.34f, colour(look.tint), 2.0f * scale_);
        ImGui::Dummy(ImVec2(side, side));
    } else {
        ImGui::PushFont(body_, side);
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(look.tint));
        ImGui::TextUnformatted(look.glyph);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::SameLine(0.0f, 12.0f * scale_);
    const std::string action = update_action_label(update);
    const char* more = update.notes.empty() ? "Update options" : "What's new";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float tools = bold_->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, more).x +
                        (action.empty() ? 0.0f : ImGui::CalcTextSize(action.c_str()).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(std::max(ImGui::GetCursorPosX() + 80.0f * scale_, right_edge - tools - style.ItemSpacing.x * 2.0f));
    ImGui::PushFont(bold_, 0.0f);
    ImGui::TextUnformatted(look.title.c_str());
    ImGui::PopFont();
    if (update.state == UpdateState::downloading) {
        ImGui::ProgressBar(static_cast<float>(update.progress) / 100.0f, ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 6.0f * scale_), "");
    } else if (!look.line.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(kMuted));
        ImGui::TextUnformatted(look.line.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_edge - tools));
    ImGui::AlignTextToFramePadding();
    if (link(more)) updates_open_ = true;
    bool leave = false;
    if (!action.empty()) {
        ImGui::SameLine();
        leave = update_action(update);
    }
    card_end();
    return leave;
}

std::string View::update_action_label(const UpdateView& update) const {
    switch (update.state) {
    case UpdateState::unavailable: return std::string(icon::external_link) + "  Releases";
    case UpdateState::available: return std::string(icon::download) + "  Download";
    case UpdateState::ready: return std::string(icon::refresh_cw) + "  Restart and update";
    case UpdateState::checking:
    case UpdateState::downloading: return {};
    case UpdateState::failed: return std::string(icon::refresh_cw) + "  Try again";
    case UpdateState::idle:
    case UpdateState::current: return std::string(icon::refresh_cw) + "  Check for updates";
    }
    return {};
}

bool View::update_action(const UpdateView& update) {
    const std::string label = update_action_label(update);
    if (label.empty()) return false;
    const bool primary = update.state == UpdateState::available || update.state == UpdateState::ready;
    const bool blocked = update.state == UpdateState::ready && (session_.game_running() || session_.busy());
    ImGui::BeginDisabled(updater_.busy() || blocked);
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, rgb(kRed));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(kRedHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(kRedPressed));
        ImGui::PushStyleColor(ImGuiCol_Border, rgb(kRed));
    }
    const bool pressed = ImGui::Button(label.c_str());
    if (primary) ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    if (blocked) tooltip("Close the game and wait for the task to finish first");
    if (!pressed) return false;
    switch (update.state) {
    case UpdateState::unavailable: SDL_OpenURL(kReleasesPage); break;
    case UpdateState::available: updater_.download(); break;
    case UpdateState::ready: return apply_update(updater_, session_.program(), session_.data(), activity_, false, true);
    default: updater_.check(); break;
    }
    return false;
}

void View::details_popup() {
    if (details_) {
        ImGui::OpenPopup("Details");
        details_ = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.72f, viewport->WorkSize.y * 0.72f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale_, 16.0f * scale_));
    const bool open = ImGui::BeginPopupModal("Details", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!open) return;
    heading("Status");
    wrapped(text_of(session_.status()));
    ImGui::Spacing();
    heading("Activity");
    const auto lines = activity_.lines();
    ImGui::BeginChild("lines", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 6.0f * scale_), ImGuiChildFlags_Borders);
    for (const auto& line : lines) {
        const auto split = line.find("  ");
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(kFaint));
        ImGui::TextUnformatted(line.substr(0, split).c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        wrapped(split == std::string::npos ? line : line.substr(split + 2));
    }
    if (lines.size() != seen_lines_) {
        ImGui::SetScrollHereY(1.0f);
        seen_lines_ = lines.size();
    }
    ImGui::EndChild();
    if (ImGui::Button("Copy")) {
        std::string all = text_of(session_.status()) + "\n\n";
        for (const auto& line : lines) all += line + "\n";
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void View::pick(Wanted wanted) {
    if (!window_) return;
    {
        std::scoped_lock lock(g_picked.mutex);
        if (g_picked.open) return;
        g_picked.open = true;
        g_picked.wanted = wanted;
        g_picked.path.reset();
    }
    SDL_PropertiesID properties = SDL_CreateProperties();
    if (wanted != Wanted::earlier) {
        SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(image_filters.data()));
        SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(image_filters.size()));
    }
    SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window_);
    SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                          wanted == Wanted::earlier    ? "Select the folder of your earlier copy of Disruptor Recompiled"
                          : wanted == Wanted::language ? "Select a French, German or Japanese Disruptor disc image"
                                                       : "Select your Disruptor disc image");
    SDL_ShowFileDialogWithProperties(wanted == Wanted::earlier ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE,
                                     picked_image, nullptr, properties);
    SDL_DestroyProperties(properties);
}

void View::take_picked() {
    std::optional<std::string> path;
    Wanted wanted = Wanted::image;
    {
        std::scoped_lock lock(g_picked.mutex);
        path = std::exchange(g_picked.path, std::nullopt);
        wanted = g_picked.wanted;
    }
    if (!path) return;
    if (wanted == Wanted::language) session_.add_language_disc(from_utf8(*path));
    else if (wanted == Wanted::earlier) session_.look_at_earlier(from_utf8(*path));
    else session_.take_image(from_utf8(*path));
}

void View::earlier_popup() {
    if (session_.earlier() && !ImGui::IsPopupOpen("Bring these files here?")) ImGui::OpenPopup("Bring these files here?");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale_, 16.0f * scale_));
    const bool open = ImGui::BeginPopupModal("Bring these files here?", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!open) return;
    if (const auto& copy = session_.earlier()) {
        ImGui::TextUnformatted(("From " + utf8(copy->folder) + ", " + size_text(copy->bytes) + ":").c_str());
        for (const fs::path& entry : copy->entries) ImGui::BulletText("%s", utf8(entry).c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted("They are copied, and the earlier copy stays as it is");
        ImGui::TextUnformatted("A file here that one of them replaces is kept in the backups folder first");
        ImGui::Spacing();
        if (button(icon::check, "Bring them", true)) {
            session_.bring_earlier();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Not now") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            session_.keep_earlier();
            ImGui::CloseCurrentPopup();
        }
    } else {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void View::heading(const char* text) {
    ImGui::PushFont(bold_, 0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

bool View::button(const char* glyph, const char* label, bool primary) {
    const std::string text = glyph ? std::string(glyph) + "  " + label : std::string(label);
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, rgb(kRed));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(kRedHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(kRedPressed));
        ImGui::PushStyleColor(ImGuiCol_Border, rgb(kRed));
    }
    const bool pressed = ImGui::Button(text.c_str());
    if (primary) ImGui::PopStyleColor(4);
    return pressed;
}

bool View::link(const char* label) {
    ImGui::PushFont(bold_, 0.0f);
    const bool pressed = ImGui::TextLink(label);
    ImGui::PopFont();
    return pressed;
}

void View::settings() {
    const auto labels = page_labels();
    if (page_ < 0 || page_ >= static_cast<int>(labels.size())) page_ = 0;
    const bool editable = session_.editable();
    ImGui::PushFont(bold_, kFontSize * 1.45f);
    ImGui::TextUnformatted(text_of(labels[static_cast<std::size_t>(page_)]).c_str());
    ImGui::PopFont();
    if (session_.game_running()) {
        muted("The game is running: change settings in its menu, or close it first", kAmber);
    } else if (!session_.settings().readable()) {
        muted("settings.toml is not valid TOML, so it is left as it is", kAmber);
    } else {
        muted("Changes are saved at once and apply the next time the game starts");
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(kSurface));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale_, 8.0f * scale_));
    if (rows_scroll_ > 0.0f) ImGui::SetNextWindowScroll(ImVec2(0.0f, rows_scroll_));
    ImGui::BeginChild("rows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    rows();
    if (page_ == static_cast<int>(Page::controls)) pad_buttons();
    const char* note = page_notes[std::clamp(page_, 0, static_cast<int>(std::size(page_notes)) - 1)];
    if (*note) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", note);
        ImGui::Spacing();
    }
    const Page page = static_cast<Page>(page_);
    bool reset_asked = false;
    if (page == Page::troubleshooting) {
        if (button(icon::bug, "Report a problem")) session_.make_problem_report();
        hand_when_hovered();
        tooltip("Saves the game's logs, your settings and a note of the system for a bug report");
    } else {
        ImGui::Dummy(ImVec2(0.0f, 10.0f * scale_));
        ImGui::BeginDisabled(!editable);
        ImGui::PushStyleColor(ImGuiCol_TextLink, rgb(kMuted));
        reset_asked = ImGui::TextLink((std::string(icon::rotate_ccw) + "  Reset this page...").c_str());
        if (editable) hand_when_hovered();
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale_));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (reset_asked) ImGui::OpenPopup("Reset settings?");

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale_, 16.0f * scale_));
    const bool open = ImGui::BeginPopupModal("Reset settings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (open) {
        ImGui::TextUnformatted("Put the settings on this page back to what a new installation has?");
        ImGui::TextUnformatted("Other pages, the language, the key bindings and your saves stay as they are");
        ImGui::Spacing();
        ImGui::BeginDisabled(!session_.editable());
        if (button(icon::rotate_ccw, "Reset", true)) {
            session_.reset_page(page);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Keep") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void View::group_heading(const char* name, bool first) {
    if (!first) ImGui::Dummy(ImVec2(0.0f, 10.0f * scale_));
    std::string heading(name);
    for (char& letter : heading) letter = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    ImGui::PushFont(bold_, kFontSize * 0.8f);
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(kRedText));
    ImGui::TextUnformatted(heading.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

namespace {

constexpr std::string_view kPickField = "pick.";

std::string pick_field(std::size_t row, std::size_t which) {
    return std::string(kPickField) + std::to_string(row) + "." + std::to_string(which);
}

bool parse_pick_field(std::string_view field, std::size_t& row, std::size_t& which) {
    if (!field.starts_with(kPickField)) return false;
    const char* end = field.data() + field.size();
    const auto parsed = std::from_chars(field.data() + kPickField.size(), end, row);
    if (parsed.ec != std::errc() || parsed.ptr == end || *parsed.ptr != '.') return false;
    return std::from_chars(parsed.ptr + 1, end, which).ec == std::errc() && row < settings::pick_count && which < 2;
}
// The fields of the Controls page's actions, which name a key's owner before the fields it does not show.
std::vector<std::size_t> shown_fields() {
    std::vector<std::size_t> fields;
    for (const KeyAction& action : key_actions())
        fields.push_back(static_cast<std::size_t>(
            std::find(settings::key_fields.begin(), settings::key_fields.end(), std::string_view(action.field)) -
            settings::key_fields.begin()));
    return fields;
}

std::string field_label(std::size_t field) {
    const std::string_view name = settings::key_fields[field];
    for (const KeyAction& action : key_actions())
        if (name == action.field) return action.label;
    constexpr std::pair<std::string_view, const char*> unshown[] = {
        {"l3", "L3"}, {"r3", "R3"}, {"ls_up", "Left stick up"}, {"ls_down", "Left stick down"}, {"ls_left", "Left stick left"},
        {"ls_right", "Left stick right"}, {"rs_up", "Right stick up"}, {"rs_down", "Right stick down"},
        {"rs_left", "Right stick left"}, {"rs_right", "Right stick right"}};
    for (const auto& [unshown_field, label] : unshown)
        if (name == unshown_field) return label;
    return std::string(name);
}

bool shown_on_page(std::size_t field) {
    const std::vector<std::size_t> fields = shown_fields();
    return std::find(fields.begin(), fields.end(), field) != fields.end();
}

} // namespace

settings::PickKeys View::pick_keys() const {
    return settings::pick_keys_from(session_.settings().pick_lists(), [](std::string_view name) {
        return SDL_GetScancodeFromName(std::string(name).c_str()) != SDL_SCANCODE_UNKNOWN;
    });
}

void View::pick_table(const char* heading, std::size_t first, std::size_t count, const KeyLayout& pad) {
    const settings::PickKeys keys = pick_keys();
    const std::vector<std::size_t> shown = shown_fields();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
    ImGui::PushFont(bold_, kFontSize);
    ImGui::TextUnformatted(heading);
    ImGui::PopFont();
    float label_width = 0.0f;
    for (std::size_t row = first; row < first + count; ++row)
        label_width = std::max(label_width, ImGui::CalcTextSize(settings::pick_names[row]).x);
    // A key's column is as wide as its longest text needs. Both halves side by side only where they fit.
    const float padding = 2.0f * ImGui::GetStyle().FramePadding.x + 4.0f * scale_;
    float key_width = ImGui::CalcTextSize("Press a key").x + padding;
    for (const auto& pair : keys)
        for (const std::string& name : pair) key_width = std::max(key_width, ImGui::CalcTextSize(name.c_str()).x + padding);
    const ImVec2 cells(4.0f * scale_, ImGui::GetStyle().CellPadding.y);
    const float gap = 8.0f * scale_, between = 16.0f * scale_;
    const float side_width = label_width + gap + 2.0f * key_width + 6.0f * cells.x;
    const bool two_sides = 2.0f * side_width + between + 4.0f * cells.x <= ImGui::GetContentRegionAvail().x;
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    ImGui::PushID(heading);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, cells);
    const bool table = ImGui::BeginTable("picks", two_sides ? 7 : 3, flags);
    ImGui::PopStyleVar();
    if (!table) {
        ImGui::PopID();
        return;
    }
    for (int side = 0; side < (two_sides ? 2 : 1); ++side) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, label_width + gap);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, key_width);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, key_width);
        if (side == 0 && two_sides) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, between);
    }
    const std::size_t rows = two_sides ? (count + 1) / 2 : count;
    for (std::size_t line = 0; line < rows; ++line) {
        ImGui::TableNextRow();
        for (const std::size_t at : {line, line + rows}) {
            if (at >= count) continue;
            const std::size_t row = first + at;
            const int base = at == line ? 0 : 4;
            ImGui::TableSetColumnIndex(base);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(settings::pick_names[row]);
            for (std::size_t which = 0; which < 2; ++which) {
                ImGui::TableSetColumnIndex(base + 1 + static_cast<int>(which));
                const std::string field = pick_field(row, which);
                const std::string& name = keys[row][which];
                const bool waiting = listening_ == field;
                const auto owner = waiting ? std::nullopt : settings::field_with(pad, name, shown);
                const std::string label = waiting ? std::string("Press a key") : settings::no_key(name) ? std::string("-") : name;
                ImGui::PushID(field.c_str());
                if (waiting) ImGui::PushStyleColor(ImGuiCol_Border, rgb(kRed));
                if (owner) ImGui::PushStyleColor(ImGuiCol_Text, rgb(kFaint));
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                    listening_ = waiting ? std::string() : field;
                    stop_pad_capture();
                    key_note_.clear();
                    pick_note_.clear();
                    offer_.reset();
                }
                if (owner) ImGui::PopStyleColor();
                if (waiting) ImGui::PopStyleColor();
                hand_when_hovered();
                if (owner) tooltip((name + " is on " + field_label(*owner) + "\nOnly that action takes it").c_str());
                ImGui::PopID();
            }
        }
    }
    ImGui::EndTable();
    ImGui::PopID();
}

void View::pick_section() {
    const bool modern = session_.settings().shown(*find_option("disruptor.modern_controls")) == 1;
    const KeyLayout pad = settings::layout_for_scheme(session_.keys().layout(), modern);
    ImGui::BeginDisabled(!session_.editable());
    ImGui::Indent(14.0f * scale_);
    pick_table("Weapons", 0, settings::weapon_count, pad);
    pick_table("Psionics", settings::weapon_count, settings::psionic_count, pad);
    ImGui::PushFont(nullptr, kSmallSize);
    if (!pick_note_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(kAmber));
        wrapped(pick_note_);
        ImGui::PopStyleColor();
        if (offer_ && offer_->target.starts_with(kPickField) && link("Take it")) take_offer();
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    hint_lines("To change a key: click it, then press the new one\nEscape cancels\nDelete takes the key away",
               ImGui::GetContentRegionAvail().x);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
    if (button(icon::rotate_ccw, "Default weapon and psionic keys")) {
        session_.set_pick_keys(settings::default_pick_keys());
        pick_note_.clear();
    }
    ImGui::Unindent(14.0f * scale_);
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale_));
}

bool View::offer_holds() const {
    const SettingsFile& file = session_.settings();
    if (!offer_ || file.shown(*find_option("disruptor.number_keys")) != 1) return false;
    const bool modern = file.shown(*find_option("disruptor.modern_controls")) == 1;
    const auto field = settings::field_with(settings::layout_for_scheme(session_.keys().layout(), modern), offer_->key,
                                            shown_fields());
    if (!offer_->owner.starts_with(kPickField)) return field && settings::key_fields[*field] == offer_->owner;
    const auto pick = settings::pick_with(pick_keys(), offer_->key);
    return !field && pick && pick_field(pick->row, pick->which) == offer_->owner;
}

void View::take_offer() {
    const bool holds = offer_holds();
    const Offer offer = *std::exchange(offer_, std::nullopt);
    key_note_.clear();
    pick_note_.clear();
    if (!holds) return;
    KeyBindings& keys = session_.keys();
    const bool modern = session_.settings().shown(*find_option("disruptor.modern_controls")) == 1;
    const KeyLayout before = settings::layout_for_scheme(keys.layout(), modern);
    keys.use(before);
    settings::PickKeys picks = pick_keys();
    std::size_t row = 0, which = 0;
    if (parse_pick_field(offer.target, row, which)) give_to_pick(keys, picks, row, which, offer.owner);
    else give_to_action(keys, picks, offer.target, offer.key);
    session_.store_keys(before, picks);
}

void View::profiles() {
    const auto file = [this](int slot) { return session_.data() / ("keys-" + std::to_string(slot) + ".ini"); };
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Profiles");
    for (int slot = 1; slot <= 2; ++slot) {
        ImGui::PushID(slot);
        ImGui::SameLine(0.0f, slot == 1 ? 10.0f * scale_ : 18.0f * scale_);
        const std::string number = std::to_string(slot);
        if (ImGui::Button(("Save to " + number).c_str())) {
            const bool modern = session_.settings().shown(*find_option("disruptor.modern_controls")) == 1;
            const KeyProfile profile{settings::layout_for_scheme(session_.keys().layout(), modern), pick_keys()};
            const bool saved = save_profile(file(slot), profile);
            key_note_ = saved ? "The keys are in profile " + number : "Profile " + number + " could not be written";
            key_note_good_ = saved;
            offer_.reset();
        }
        hand_when_hovered();
        tooltip("Keeps every key of this page, the weapons' and the powers' too\nThe scheme and the mouse stay out");
        ImGui::SameLine(0.0f, 6.0f * scale_);
        std::error_code missing;
        const bool there = std::filesystem::exists(file(slot), missing);
        ImGui::BeginDisabled(!there);
        if (ImGui::Button(("Load " + number).c_str())) {
            const auto profile = load_profile(file(slot), [](std::string_view name) {
                return SDL_GetScancodeFromName(std::string(name).c_str()) != SDL_SCANCODE_UNKNOWN;
            });
            bool used = false;
            if (profile) {
                const KeyLayout undo = session_.keys().layout();
                session_.keys().use(profile->layout);
                used = session_.store_keys(undo, profile->picks);
            }
            key_note_good_ = used;
            key_note_ = "Profile " + number + (used ? " is in use" : profile ? " could not be used" : " could not be read");
            offer_.reset();
        }
        if (there) hand_when_hovered();
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void View::take_pick_key(const std::string& field, const std::string& name) {
    using Kind = settings::PickKeyChange::Kind;
    std::size_t row = 0, which = 0;
    if (!parse_pick_field(field, row, which)) return;
    const bool modern = session_.settings().shown(*find_option("disruptor.modern_controls")) == 1;
    const KeyLayout pad = settings::layout_for_scheme(session_.keys().layout(), modern);
    settings::PickKeys keys = pick_keys();
    const settings::PickKeyChange change = settings::change_pick_key(keys, row, which, name, pad, shown_fields());
    switch (change.kind) {
    case Kind::fixed: pick_note_ = name + " is one of the fixed keys under Keys"; return;
    case Kind::modifier: pick_note_ = "Shift, Ctrl, Alt and the system key pick nothing"; return;
    case Kind::taken:
        pick_note_ = name + " is on " + field_label(change.field);
        offer_ = Offer{name, field, settings::key_fields[change.field]};
        return;
    case Kind::unchanged: return;
    case Kind::cleared:
    case Kind::assigned: break;
    }
    if (!session_.set_pick_keys(keys) || !change.swapped) return;
    pick_note_ = name + " was on " + settings::pick_names[change.swapped->row] + ", which now has " +
                (settings::no_key(change.given) ? std::string("no key") : change.given);
}

void View::key_table() {
    const SettingsFile& file = session_.settings();
    const bool modern = file.shown(*find_option("disruptor.modern_controls")) == 1;
    const bool weapons = file.shown(*find_option("disruptor.number_keys")) == 1;
    const bool editable = session_.editable();
    if (!weapons && listening_.starts_with(kPickField)) listening_.clear();
    if (offer_ && !offer_holds()) {
        offer_.reset();
        key_note_.clear();
        pick_note_.clear();
    }
    // The game may change the keys while it runs: they are read again once the page can be edited.
    if (!editable) {
        listening_.clear();
        keys_read_ = false;
    }
    if (!keys_read_) {
        session_.keys().load();
        keys_read_ = true;
    }
    // What the game will play with: a scheme's own layout follows the scheme saved beside it.
    const KeyLayout shown = settings::layout_for_scheme(session_.keys().layout(), modern);
    group_heading("Keys", false);
    ImGui::BeginDisabled(!editable);
    const auto actions = key_actions();
    float label_width = 0.0f;
    for (const KeyAction& action : actions) label_width = std::max(label_width, ImGui::CalcTextSize(action.label).x);
    const float small = kSmallSize;
    const float mouse_width = modern ? body_->CalcTextSizeA(small, FLT_MAX, 0.0f, "Right click").x + 8.0f * scale_ : 0.0f;
    const int columns = modern ? 3 : 2;
    const float padding = 2.0f * ImGui::GetStyle().FramePadding.x + 10.0f * scale_;
    float key_width = std::max(110.0f * scale_, ImGui::CalcTextSize("Press a key").x + padding);
    for (const std::string& name : shown) key_width = std::max(key_width, ImGui::CalcTextSize(name.c_str()).x + padding);
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("keys", columns * 2 + 1, flags)) {
        for (int pair = 0; pair < 2; ++pair) {
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, label_width + 12.0f * scale_);
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, key_width);
            if (modern) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, mouse_width);
            if (pair == 0) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 28.0f * scale_);
        }
        const std::size_t half = (actions.size() + 1) / 2;
        for (std::size_t row = 0; row < half; ++row) {
            ImGui::TableNextRow();
            for (const std::size_t index : {row, row + half}) {
                if (index >= actions.size()) continue;
                const int base = index == row ? 0 : columns + 1;
                const KeyAction& action = actions[index];
                ImGui::TableSetColumnIndex(base);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(action.label);
                ImGui::TableSetColumnIndex(base + 1);
                const std::size_t field = static_cast<std::size_t>(
                    std::find(settings::key_fields.begin(), settings::key_fields.end(), std::string_view(action.field)) -
                    settings::key_fields.begin());
                const bool waiting = listening_ == action.field;
                std::string label = waiting ? std::string("Press a key") : shown[field];
                if (label == "None") label = "-";
                ImGui::PushID(action.field);
                if (waiting) ImGui::PushStyleColor(ImGuiCol_Border, rgb(kRed));
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                    listening_ = waiting ? std::string() : std::string(action.field);
                    stop_pad_capture();
                    key_note_.clear();
                    pick_note_.clear();
                    offer_.reset();
                }
                if (waiting) ImGui::PopStyleColor();
                hand_when_hovered();
                ImGui::PopID();
                if (!modern) continue;
                ImGui::TableSetColumnIndex(base + 2);
                for (const settings::MouseButton& button : settings::modern_mouse) {
                    if (std::string_view(button.field) != action.field) continue;
                    ImGui::AlignTextToFramePadding();
                    ImGui::PushFont(nullptr, small);
                    ImGui::PushStyleColor(ImGuiCol_Text, rgb(kMuted));
                    ImGui::TextUnformatted(button.name);
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::PushFont(nullptr, kSmallSize);
    if (!key_note_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(key_note_good_ ? kGreen : kAmber));
        wrapped(key_note_);
        ImGui::PopStyleColor();
        if (offer_ && !offer_->target.starts_with(kPickField) && link("Take it")) take_offer();
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    const float width = ImGui::GetContentRegionAvail().x;
    const char* arrows = weapons ? "Arrows: Up and Down also walk while no action, weapon or power has them"
                                 : "Arrows: Up and Down also walk while no action has them";
    hint_lines(modern ? arrows : "Mouse: only turns the view", width);
    hint_lines("To change a key: click it, then press the new one\nEscape cancels\nDelete takes the key away\n"
               "Fixed keys: these do the same in both schemes and cannot be changed",
               width);
    ImGui::Indent(16.0f * scale_);
    std::string fixed;
    for (const FixedKey& key : fixed_keys()) fixed += std::string(fixed.empty() ? "" : "\n") + key.keys + ": " + key.does;
    hint_lines(fixed, width - 16.0f * scale_);
    ImGui::Unindent(16.0f * scale_);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
    if (button(icon::rotate_ccw, "Default keys")) {
        session_.keys().use(settings::layout_of(modern ? settings::modern_keys : settings::classic_keys));
        session_.save_keys();
        key_note_.clear();
    }
    ImGui::SameLine(0.0f, 24.0f * scale_);
    profiles();
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
}

void View::pad_status() {
    if (!pads_tried_) {
        pads_tried_ = true;
        pads_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    }
    std::string names;
    int count = 0;
    if (pads_) {
        SDL_JoystickID* pads = SDL_GetGamepads(&count);
        for (int index = 0; pads && index < count; ++index) {
            const char* name = SDL_GetGamepadNameForID(pads[index]);
            names += std::string(index ? ", " : "") + (name ? name : "Gamepad");
        }
        SDL_free(pads);
    }
    std::string device = session_.settings().player_device();
    for (char& letter : device) letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    const bool beside = device.empty() || device == "keyboard+gamepad";
    std::string status = count ? "Connected: " + names : std::string("No gamepad connected");
    if (beside) status += count > 1 ? "\nThe first plays beside the keyboard and mouse"
                                    : count ? "\nIt plays beside the keyboard and mouse"
                                            : "\nOne plugged in plays beside the keyboard and mouse";
    else status += "\nsettings.toml gives Player 1 the device \"" + session_.settings().player_device() + "\"";
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(count && beside ? kGreen : kMuted));
    wrapped(status);
    ImGui::PopStyleColor();
}

void View::pad_buttons() {
    const bool editable = session_.editable();
    if (!editable) {
        stop_pad_capture();
        pad_map_read_ = false;
    }
    if (!pad_map_read_) {
        session_.pads().load();
        pad_map_read_ = true;
    }
    int count = 0;
    pad_family_ = settings::PadFamily::xbox;
    if (pads_) {
        SDL_JoystickID* pads = SDL_GetGamepads(&count);
        if (pads && count > 0) {
            switch (SDL_GetGamepadTypeForID(pads[0])) {
            case SDL_GAMEPAD_TYPE_PS3: pad_family_ = settings::PadFamily::playstation3; break;
            case SDL_GAMEPAD_TYPE_PS4: pad_family_ = settings::PadFamily::playstation4; break;
            case SDL_GAMEPAD_TYPE_PS5: pad_family_ = settings::PadFamily::playstation5; break;
            default: break;
            }
        }
        SDL_free(pads);
    }
    if (count == 0) stop_pad_capture();
    const auto& actions = settings::pad_actions;
    float label_width = 0.0f;
    for (const settings::PadAction& action : actions) label_width = std::max(label_width, ImGui::CalcTextSize(action.action).x);
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
    ImGui::BeginDisabled(!editable);
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX;
    const float key_width = std::max(110.0f * scale_, ImGui::CalcTextSize("Press a button").x + 2.0f * ImGui::GetStyle().FramePadding.x + 10.0f * scale_);
    if (ImGui::BeginTable("pad buttons", 5, flags)) {
        for (int pair = 0; pair < 2; ++pair) {
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, label_width + 12.0f * scale_);
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, key_width);
            if (pair == 0) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 28.0f * scale_);
        }
        const std::size_t half = (actions.size() + 1) / 2;
        for (std::size_t row = 0; row < half; ++row) {
            ImGui::TableNextRow();
            for (const std::size_t index : {row, row + half}) {
                if (index >= actions.size()) continue;
                const int base = index == row ? 0 : 3;
                const settings::PadAction& action = actions[index];
                ImGui::TableSetColumnIndex(base);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(action.action);
                ImGui::TableSetColumnIndex(base + 1);
                const bool waiting = listening_pad_ == action.field;
                const std::string label = waiting ? std::string("Press a button") : pad_name(session_.pads().source(action.field));
                ImGui::PushID(action.field);
                ImGui::BeginDisabled(count == 0);
                if (waiting) ImGui::PushStyleColor(ImGuiCol_Border, rgb(kRed));
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                    stop_pad_capture();
                    if (!waiting) {
                        listening_.clear();
                        listening_pad_ = action.field;
                        SDL_JoystickID* pads = SDL_GetGamepads(&count);
                        for (int at = 0; pads && at < count; ++at)
                            if (SDL_Gamepad* pad = SDL_OpenGamepad(pads[at])) capture_pads_.push_back(pad);
                        SDL_free(pads);
                        // A trigger held as the wait begins counts only once it has been let go.
                        SDL_UpdateGamepads();
                        trigger_armed_.clear();
                        for (SDL_Gamepad* pad : capture_pads_)
                            trigger_armed_[SDL_GetGamepadID(pad)] = {
                                SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) < kTriggerReleased,
                                SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) < kTriggerReleased};
                    }
                    pad_note_.clear();
                }
                if (waiting) ImGui::PopStyleColor();
                hand_when_hovered();
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PushFont(nullptr, kSmallSize);
    if (!pad_note_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(kAmber));
        wrapped(pad_note_);
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    const float width = ImGui::GetContentRegionAvail().x;
    hint_lines("Move and turn: the left stick or the D-pad, always", width);
    hint_lines(count ? "To change a button: click it, then press the new one on the gamepad\nEscape cancels"
                     : "To change a button: connect a gamepad, click the button, then press the new one",
               width);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
    if (button(icon::rotate_ccw, "Default buttons")) {
        stop_pad_capture();
        session_.pads().use_defaults();
        session_.save_pads();
        pad_note_.clear();
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
}

void View::stop_pad_capture() {
    listening_pad_.clear();
    trigger_armed_.clear();
    for (SDL_Gamepad* pad : capture_pads_) SDL_CloseGamepad(pad);
    capture_pads_.clear();
}

void View::take_pad_button(int button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return finish_pad("a");
    case SDL_GAMEPAD_BUTTON_EAST: return finish_pad("b");
    case SDL_GAMEPAD_BUTTON_WEST: return finish_pad("x");
    case SDL_GAMEPAD_BUTTON_NORTH: return finish_pad("y");
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return finish_pad("leftshoulder");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return finish_pad("rightshoulder");
    case SDL_GAMEPAD_BUTTON_BACK: return finish_pad("back");
    case SDL_GAMEPAD_BUTTON_START: return finish_pad("start");
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return finish_pad("leftstick");
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return finish_pad("rightstick");
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: pad_note_ = "The D-pad stays on moving and turning"; return;
    default: pad_note_ = "The game does not read that button"; return;
    }
}

void View::take_pad_axis(std::uint32_t pad, int axis, int value) {
    const int side = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : -1;
    const auto found = trigger_armed_.find(pad);
    if (side < 0 || found == trigger_armed_.end()) return;
    bool& armed = found->second[static_cast<std::size_t>(side)];
    if (value < kTriggerReleased) armed = true;
    else if (value >= kTriggerPressed && armed) finish_pad(side ? "righttrigger" : "lefttrigger");
}

std::string View::pad_name(const std::string& source) const {
    if (source.empty()) return "-";
    const settings::PadSource* known = settings::pad_source(source);
    return known ? std::string(settings::printed(*known, pad_family_)) : source;
}

void View::finish_pad(const std::string& source) {
    const std::string field = listening_pad_;
    stop_pad_capture();
    if (field.empty() || !session_.editable()) return;
    PadBindings& pads = session_.pads();
    const std::string previous = pads.source(field);
    const auto swapped = pads.assign(field, source);
    pad_note_.clear();
    if (!session_.save_pads() || !swapped) return;
    const auto action_of = [](std::string_view of) {
        for (const settings::PadAction& action : settings::pad_actions)
            if (of == action.field) return std::string(action.action);
        return std::string(of);
    };
    pad_note_ = pad_name(source) + " was on " + action_of(*swapped) + ", which now has " + pad_name(previous);
}

void View::hint_lines(std::string_view text, float width) {
    std::vector<Line> lines;
    for (std::string_view rest = text;;) {
        const std::size_t end = rest.find('\n');
        const std::string_view part = rest.substr(0, end);
        if (const std::size_t name = settings::choice_name(part)) {
            Line line{{Style::bold, std::string(part.substr(0, name)), {}}};
            const Line after = parse_inline(part.substr(name));
            line.insert(line.end(), after.begin(), after.end());
            lines.push_back(std::move(line));
        } else {
            lines.push_back(parse_inline(part));
        }
        if (end == std::string_view::npos) break;
        rest.remove_prefix(end + 1);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 4.0f * scale_));
    for (const Line& line : lines) rich(line, width, false, true, 1.4f);
    ImGui::PopStyleVar();
}

void View::tag(const char* text, unsigned hex) {
    const float size = ImGui::GetFontSize() * 0.75f;
    const ImVec2 measured = bold_->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 pill(measured.x + 12.0f * scale_, measured.y + 4.0f * scale_);
    const ImVec2 at = ImGui::GetCursorScreenPos() + ImVec2(0.0f, std::round((ImGui::GetTextLineHeight() - pill.y) * 0.5f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, at + pill, colour(hex), pill.y * 0.5f);
    draw->AddText(bold_, size, at + ImVec2(6.0f * scale_, 2.0f * scale_), colour(0x111611), text);
    ImGui::Dummy(ImVec2(pill.x, ImGui::GetTextLineHeight()));
}

void View::take_key(const char* name) {
    const std::string field = std::exchange(listening_, std::string());
    if (field.empty() || !name || !*name || settings::folded_key(name) == "escape") return;
    if (field.starts_with(kPickField)) {
        take_pick_key(field, name);
        return;
    }
    key_note_good_ = false;
    if (reserved_key(name)) {
        key_note_ = std::string(name) + " cannot be changed: it is one of the fixed keys below";
        return;
    }
    const SettingsFile& file = session_.settings();
    const bool modern = file.shown(*find_option("disruptor.modern_controls")) == 1;
    KeyBindings& keys = session_.keys();
    if (settings::folded_key(name) == "delete") {
        keys.use(settings::layout_for_scheme(keys.layout(), modern));
        keys.clear(field);
        key_note_.clear();
        session_.save_keys();
        return;
    }
    keys.use(settings::layout_for_scheme(keys.layout(), modern));
    // A pick's key an action also has is idle there: the actions swap as usual.
    if (file.shown(*find_option("disruptor.number_keys")) == 1 && !settings::field_with(keys.layout(), name)) {
        if (const auto pick = settings::pick_with(pick_keys(), name)) {
            key_note_ = std::string(name) + " is on " + settings::pick_names[pick->row];
            offer_ = Offer{name, field, pick_field(pick->row, pick->which)};
            return;
        }
    }
    const std::string previous = keys.key(field);
    const auto swapped = keys.assign(field, name);
    key_note_.clear();
    if (!session_.save_keys() || !swapped) return;
    const auto label_of = [](std::string_view of) {
        for (const KeyAction& action : key_actions())
            if (of == action.field) return std::string(action.label);
        return std::string(of);
    };
    key_note_ = std::string(name) + " was on " + label_of(*swapped) + ", which now has " +
                (previous == "None" ? std::string("no key") : previous);
}

bool View::toggle(const char* id, bool* value) {
    const float height = ImGui::GetFrameHeight() * 0.78f, width = height * 1.8f;
    const ImVec2 at = ImGui::GetCursorScreenPos() + ImVec2(0.0f, (ImGui::GetFrameHeight() - height) * 0.5f);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, ImGui::GetFrameHeight()));
    hand_when_hovered();
    if (pressed) *value = !*value;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float alpha = ImGui::GetStyle().Alpha;
    const unsigned track = *value ? (hovered ? kRedHover : kRed) : (hovered ? 0x2f3d31 : 0x283329);
    draw->AddRectFilled(at, at + ImVec2(width, height), colour(track, alpha), height * 0.5f);
    const float knob = height * 0.5f - 3.0f * scale_;
    const float x = *value ? at.x + width - height * 0.5f : at.x + height * 0.5f;
    draw->AddCircleFilled(ImVec2(x, at.y + height * 0.5f), knob, colour(*value ? 0xffffff : 0xa9b6aa, alpha));
    return pressed;
}

bool View::segmented(const char* id, int* index, std::span<const char* const> choices, float width) {
    ImGui::PushID(id);
    bool changed = false;
    const float part = width / static_cast<float>(choices.size());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    const float alpha = ImGui::GetStyle().Alpha;
    draw->AddRectFilled(at, at + ImVec2(width, height), colour(kRaised, alpha), ImGui::GetStyle().FrameRounding);
    draw->AddRect(at, at + ImVec2(width, height), colour(kBorder, alpha), ImGui::GetStyle().FrameRounding);
    // Every part takes its click before any is drawn, so the frame of a click shows the one choice it made.
    int hovered_part = -1;
    for (std::size_t choice = 0; choice < choices.size(); ++choice) {
        ImGui::PushID(static_cast<int>(choice));
        ImGui::SetCursorScreenPos(at + ImVec2(part * static_cast<float>(choice), 0.0f));
        if (ImGui::InvisibleButton("##part", ImVec2(part, height)) && *index != static_cast<int>(choice)) {
            *index = static_cast<int>(choice);
            changed = true;
        }
        hand_when_hovered();
        if (ImGui::IsItemHovered()) hovered_part = static_cast<int>(choice);
        ImGui::PopID();
    }
    for (std::size_t choice = 0; choice < choices.size(); ++choice) {
        const ImVec2 left = at + ImVec2(part * static_cast<float>(choice), 0.0f);
        const bool chosen = *index == static_cast<int>(choice), hovered = hovered_part == static_cast<int>(choice);
        if (chosen || hovered)
            draw->AddRectFilled(left + ImVec2(3.0f * scale_, 3.0f * scale_), left + ImVec2(part - 3.0f * scale_, height - 3.0f * scale_),
                                colour(chosen ? 0x334235 : 0x222d23, alpha), ImGui::GetStyle().FrameRounding - 2.0f * scale_);
        ImFont* font = chosen ? bold_ : body_;
        const ImVec2 size = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, choices[choice]);
        draw->AddText(font, ImGui::GetFontSize(), left + ImVec2((part - size.x) * 0.5f, (height - size.y) * 0.5f),
                      colour(chosen ? 0xffffff : kMuted, alpha), choices[choice]);
    }
    ImGui::SetCursorScreenPos(at);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopID();
    return changed;
}

void View::rows() {
    const SettingsFile& file = session_.settings();
    if (held_page_ != page_ || !session_.editable()) held_.clear();
    held_page_ = page_;
    const float control = std::min(300.0f * scale_, ImGui::GetContentRegionAvail().x * 0.42f);
    bool first = true;
    for (const auto& placement : layout()) {
        const Option& option = *find_option(placement.key);
        if (static_cast<int>(placement.page) != page_) continue;
        ImGui::PushID(option.key);
        if (placement.group) {
            // The keyboard's keys follow the scheme that sets them, before the group of the weapons' and powers' keys.
            const std::string_view group(placement.group);
            if (group == "Weapons and psionics") key_table();
            group_heading(placement.group, first);
            if (group == "Gamepad") pad_status();
        } else if (!first && !placement.child) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(at, at + ImVec2(ImGui::GetContentRegionAvail().x, 0.0f), colour(kBorder));
        }
        first = false;
        ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
        const bool on = session_.editable() && file.applies(option);
        const float indent = placement.child ? 26.0f * scale_ : 0.0f;
        const float text_width = ImGui::GetContentRegionAvail().x - control - 24.0f * scale_ - indent;
        const float top = ImGui::GetCursorPosY();
        if (placement.child) {
            // A child row hangs off the row above it: a corner line leads from that row to its label.
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float middle = ImGui::GetTextLineHeight() * 0.5f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddLine(at + ImVec2(9.0f * scale_, -8.0f * scale_), at + ImVec2(9.0f * scale_, middle), colour(kBorder), scale_);
            draw->AddLine(at + ImVec2(9.0f * scale_, middle), at + ImVec2(19.0f * scale_, middle), colour(kBorder), scale_);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        }
        ImGui::BeginDisabled(!on);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_width);
        ImGui::PushFont(bold_, kFontSize * 1.0625f);
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(0xffffff));
        ImGui::TextUnformatted(option.label);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (experimental(option) && placement.page != Page::experimental) {
            ImGui::SameLine(0.0f, 10.0f * scale_);
            tag("EXPERIMENTAL", kAmber);
        }
        ImGui::PushFont(nullptr, kSmallSize);
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(kMuted));
        if (option.hint) hint_lines(option.hint, text_width);
        if (option.notice) {
            ImGui::PushStyleColor(ImGuiCol_Text, rgb(kAmber));
            ImGui::TextUnformatted(option.notice);
            ImGui::PopStyleColor();
        }
        if (session_.editable() && !file.applies(option) && !option.needs.empty() && !placement.child) {
            std::string needs = "Needs ";
            for (std::size_t index = 0; index < option.needs.size(); ++index) {
                if (index) needs += index + 1 == option.needs.size() ? " and " : ", ";
                const Option* other = find_option(option.needs[index]);
                needs += other ? other->label : option.needs[index];
            }
            ImGui::PushStyleColor(ImGuiCol_Text, rgb(kAmber));
            ImGui::TextUnformatted(needs.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        const float bottom = ImGui::GetCursorPosY();
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - control, top + 2.0f * scale_));
        const int value = file.shown(option);
        const int highest = file.highest(option);
        switch (option.kind) {
        case Kind::toggle: {
            bool checked = value != 0;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + control - ImGui::GetFrameHeight() * 0.78f * 1.8f);
            if (toggle("##value", &checked)) session_.set(option, checked ? 1 : 0);
            break;
        }
        case Kind::choice: {
            int index = value;
            const int count = std::min(static_cast<int>(option.choices.size()), highest + 1);
            const std::span<const char* const> shown(option.choices.data(), static_cast<std::size_t>(count));
            float wanted = 0.0f;
            for (const char* choice : shown) wanted = std::max(wanted, ImGui::CalcTextSize(choice).x + ImGui::GetFontSize() * 1.4f);
            if (count <= 3 && wanted * static_cast<float>(count) <= control) {
                if (segmented("##value", &index, shown, control)) session_.set(option, index);
            } else {
                ImGui::SetNextItemWidth(control);
                if (ImGui::Combo("##value", &index, option.choices.data(), count)) session_.set(option, index);
            }
            break;
        }
        case Kind::slider: {
            ImGui::SetNextItemWidth(control);
            const auto held = held_.find(&option);
            int position = held != held_.end() ? held->second : to_position(option, value, highest);
            const int low = to_position(option, option.lowest, highest);
            const int high = to_position(option, highest, highest);
            const int shown = from_position(option, position, highest);
            if (ImGui::SliderInt("##value", &position, low, high, slider_format(option, shown).c_str()))
                held_[&option] = position;
            // One write when the slider is let go, not one for every step of a drag.
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                session_.set(option, from_position(option, position, highest));
                held_.erase(&option);
            } else if (!ImGui::IsItemActive()) {
                held_.erase(&option);
            }
            break;
        }
        }
        ImGui::EndDisabled();
        ImGui::SetCursorPosY(std::max(bottom, ImGui::GetCursorPosY()));
        ImGui::Dummy(ImVec2(0.0f, 4.0f * scale_));
        if (std::string_view(option.key) == "disruptor.psionic_keys_use" && file.shown(*find_option("disruptor.number_keys")) == 1)
            pick_section();
        ImGui::PopID();
    }
}

bool View::updates_popup(const UpdateView& update) {
    if (std::exchange(updates_open_, false)) ImGui::OpenPopup("Updates");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float wide = viewport->WorkSize.x * 0.72f;
    ImGui::SetNextWindowSize(ImVec2(wide, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(wide, 0.0f), ImVec2(wide, viewport->WorkSize.y * 0.94f));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * scale_, 18.0f * scale_));
    const bool open = ImGui::BeginPopupModal("Updates", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize);
    ImGui::PopStyleVar();
    if (!open) return false;

    card_begin("state", 0.0f);
    const UpdateLook look = update_look(update);
    ImGui::PushFont(body_, kFontSize * 1.9f);
    if (update.state == UpdateState::checking) {
        const float side = ImGui::GetFontSize();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        spinner(ImGui::GetWindowDrawList(), at + ImVec2(side * 0.5f, side * 0.55f), side * 0.36f, colour(look.tint), 2.5f * scale_);
        ImGui::Dummy(ImVec2(side, side));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, rgb(look.tint));
        ImGui::TextUnformatted(look.glyph);
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 14.0f * scale_);
    ImGui::BeginGroup();
    ImGui::PushFont(bold_, kFontSize * 1.15f);
    ImGui::TextUnformatted(look.title.c_str());
    ImGui::PopFont();
    if (update.state == UpdateState::downloading) {
        ImGui::ProgressBar(static_cast<float>(update.progress) / 100.0f, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
    } else if (!look.line.empty()) {
        muted(look.line);
    }
    ImGui::Spacing();
    const bool leave = update_action(update);
    ImGui::EndGroup();
    card_end();

    const bool checks = update.state != UpdateState::unavailable;
    const auto check_column = [&] {
        group_heading("Update check", false);
        ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
        const float toggle_width = ImGui::GetFrameHeight() * 0.78f * 1.8f;
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        ImGui::PushFont(bold_, kFontSize * 1.0625f);
        ImGui::TextUnformatted("Check at start");
        ImGui::PopFont();
        ImGui::PushFont(nullptr, kSmallSize);
        hint_lines("Looks for a newer release at every start\nOnly what changed is downloaded when it can be",
                   ImGui::GetContentRegionAvail().x - toggle_width - 16.0f * scale_);
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SameLine(right - toggle_width);
        bool automatic = choices_.check_for_updates;
        if (toggle("##automatic", &automatic)) {
            choices_.check_for_updates = automatic;
            if (!write_choices(choices_file_, choices_))
                activity_.add("launcher.ini cannot be written, so the choice holds until the launcher closes");
        }
    };
    const auto copy_column = [&] {
        group_heading("This copy", false);
        ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
        // A row takes the cell padding in force when it begins.
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f * scale_, 3.0f * scale_));
        if (!ImGui::BeginTable("copy", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::PopStyleVar();
            return;
        }
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 128.0f * scale_);
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch);
        const auto row = [this](const char* label) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, rgb(kMuted));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
        };
        row("Version");
        ImGui::TextUnformatted(shown_version(DISRUPTOR_BUILD_VERSION).c_str());
        if (!update.installed.empty()) {
            row("Installed");
            ImGui::Text("v%s%s", update.installed.c_str(), update.portable ? ", portable" : "");
        }
        if (!update.latest.empty()) {
            row("Newest release");
            ImGui::Text("v%s", update.latest.c_str());
        }
        row("Releases from");
        const std::string source = updater_.source();
        const std::string_view site = "https://github.com/";
        ImGui::TextLinkOpenURL(source.starts_with(site) ? source.substr(site.size()).c_str() : source.c_str(), source.c_str());
        row("Pre-releases");
        ImGui::TextUnformatted("Included");
        ImGui::EndTable();
        ImGui::PopStyleVar();
    };
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    const bool beside = checks && ImGui::BeginTable("below", 2, ImGuiTableFlags_SizingStretchSame);
    ImGui::PopStyleVar();
    if (beside) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        check_column();
        ImGui::TableNextColumn();
        ImGui::Indent(24.0f * scale_);
        copy_column();
        ImGui::Unindent(24.0f * scale_);
        ImGui::EndTable();
    } else if (!checks) {
        copy_column();
    }

    group_heading(update.state == UpdateState::available ? "What is new" : "Release notes", false);
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    if (update.notes.empty()) {
        ImGui::PushFont(nullptr, kSmallSize);
        muted("The notes of a newer release appear here once a check finds one");
        ImGui::PopFont();
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(kSurface));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * scale_, 14.0f * scale_));
        ImGui::BeginChild("notes", ImVec2(0.0f, 150.0f * scale_), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (update.notes != notes_source_) {
            notes_source_ = update.notes;
            notes_blocks_ = parse_markdown(notes_source_);
        }
        blocks(notes_blocks_, ImGui::GetContentRegionAvail().x, true);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale_));
    const float close_width = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - close_width);
    if (ImGui::Button("Close") || (!leave && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return leave;
}

void View::help() {
    ImGui::PushFont(bold_, kFontSize * 1.45f);
    ImGui::TextUnformatted("Help");
    ImGui::PopFont();
    const char* online = "Read it on GitHub";
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, ImGui::GetContentRegionAvail().x - bold_->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, online).x));
    ImGui::AlignTextToFramePadding();
    if (link(online)) SDL_OpenURL(kGuidePage);
    muted("The guide that comes with this copy\nIt is also in the program's folder as GETTING_STARTED.md");
    ImGui::Spacing();
    if (guide_.sections.empty()) {
        muted("This copy has no guide built in", kAmber);
        return;
    }
    section_ = std::clamp(section_, 0, static_cast<int>(guide_.sections.size()) - 1);

    const float size = ImGui::GetFontSize();
    ImGui::BeginChild("contents", ImVec2(196.0f * scale_, 0.0f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (std::size_t index = 0; index < guide_.sections.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        const float row = 34.0f * scale_;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float wide = ImGui::GetContentRegionAvail().x;
        const bool selected = static_cast<int>(index) == section_;
        if (ImGui::InvisibleButton("##section", ImVec2(wide, row)) && !selected) {
            section_ = static_cast<int>(index);
            section_changed_ = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        hand_when_hovered();
        if (selected || hovered) draw->AddRectFilled(at, at + ImVec2(wide, row), colour(selected ? kRaised : 0x172018), 8.0f * scale_);
        if (selected)
            draw->AddRectFilled(at + ImVec2(0.0f, 8.0f * scale_), at + ImVec2(3.0f * scale_, row - 8.0f * scale_), colour(kRed), 2.0f * scale_);
        ImFont* font = selected ? bold_ : body_;
        const std::string title = fitted(guide_.sections[index].title, wide - 24.0f * scale_,
                                         [&](const std::string& text) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x; });
        draw->AddText(font, size, at + ImVec2(14.0f * scale_, (row - size) * 0.5f), colour(selected ? 0xffffff : 0xc5cfc6), title.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 16.0f * scale_);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(kSurface));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26.0f * scale_, 20.0f * scale_));
    if (std::exchange(section_changed_, false)) ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("section", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    const Section& section = guide_.sections[static_cast<std::size_t>(section_)];
    const float width = std::min(ImGui::GetContentRegionAvail().x, 720.0f * scale_);
    ImGui::PushFont(bold_, kFontSize * 1.3f);
    ImGui::TextUnformatted(section.title.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale_));
    if (section_ == 0) blocks(guide_.intro, width);
    blocks(section.blocks, width);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void View::blocks(const std::vector<Block>& list, float width, bool compact) {
    const float size = ImGui::GetFontSize();
    const auto listed = [&](std::size_t index) {
        return index < list.size() && (list[index].kind == BlockKind::bullet || list[index].kind == BlockKind::numbered);
    };
    for (std::size_t index = 0; index < list.size(); ++index) {
        const Block& block = list[index];
        switch (block.kind) {
        case BlockKind::heading:
            if (!compact) ImGui::Dummy(ImVec2(0.0f, 6.0f * scale_));
            else if (index > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * scale_);
            ImGui::PushFont(bold_, kFontSize * (block.level <= 2 ? 1.2f : 1.08f));
            rich(block.text, width, true, false, 1.5f, kRedText);
            ImGui::PopFont();
            break;
        // A release's notes open with its date.
        case BlockKind::paragraph: rich(block.text, width, false, compact && index == 0); break;
        case BlockKind::bullet:
        case BlockKind::numbered: {
            const float indent = (24.0f + 18.0f * static_cast<float>(block.depth)) * scale_;
            const float row = std::round(size * 1.5f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (block.kind == BlockKind::bullet) {
                ImGui::GetWindowDrawList()->AddCircleFilled(at + ImVec2(indent - 13.0f * scale_, row * 0.5f), 2.5f * scale_, colour(kMuted));
            } else {
                const std::string number = std::to_string(block.level) + ".";
                const float wide = body_->CalcTextSizeA(size, FLT_MAX, 0.0f, number.c_str()).x;
                ImGui::GetWindowDrawList()->AddText(body_, size, at + ImVec2(indent - 8.0f * scale_ - wide, std::floor((row - size) * 0.5f)),
                                                    colour(kMuted), number.c_str());
            }
            ImGui::SetCursorScreenPos(at + ImVec2(indent, 0.0f));
            rich(block.text, width - indent);
            break;
        }
        case BlockKind::table: {
            const std::size_t columns = block.rows.front().size();
            if (columns == 0) break;
            std::vector<float> natural(columns, 0.0f);
            for (const auto& cells : block.rows)
                for (std::size_t column = 0; column < columns; ++column)
                    natural[column] = std::max(natural[column], bold_->CalcTextSizeA(size, FLT_MAX, 0.0f, plain_text(cells[column]).c_str()).x);
            ImGui::PushID(&block);
            constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
            if (ImGui::BeginTable("table", static_cast<int>(columns), flags, ImVec2(width, 0.0f))) {
                for (std::size_t column = 0; column < columns; ++column)
                    ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, std::clamp(natural[column], 60.0f * scale_, width * 0.62f));
                for (std::size_t row = 0; row < block.rows.size(); ++row) {
                    ImGui::TableNextRow(row == 0 ? ImGuiTableRowFlags_Headers : ImGuiTableRowFlags_None);
                    if (row == 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, colour(kRaised));
                    for (const Line& cell : block.rows[row]) {
                        ImGui::TableNextColumn();
                        rich(cell, ImGui::GetContentRegionAvail().x, row == 0, row == 0);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
            break;
        }
        case BlockKind::code:
            ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(kRaised));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale_, 10.0f * scale_));
            ImGui::BeginChild(ImGui::GetID(&block), ImVec2(width, 0.0f),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(block.code.c_str());
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            break;
        case BlockKind::rule: ImGui::Separator(); break;
        }
        // A heading stands close to what it heads, blocks apart, the items of a list closer.
        if (block.kind == BlockKind::heading) ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f * scale_);
        else if (!compact && !(listed(index) && listed(index + 1))) ImGui::Dummy(ImVec2(0.0f, 5.0f * scale_));
    }
}

void View::rich(const Line& line, float width, bool strong, bool faint, float leading, unsigned tint) {
    const float size = ImGui::GetFontSize();
    const float pad = std::round(4.0f * scale_);
    const auto font_of = [&](Style style) { return strong || style == Style::bold || style == Style::link || style == Style::lead ? bold_ : body_; };
    const auto measure = [&](const std::string& text, Style style) {
        return font_of(style)->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x + (style == Style::code ? pad * 2.0f : 0.0f);
    };
    const std::vector<Placed> placed = lay_out(line, width, measure);
    const float row = std::round(size * leading);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    int rows = 1;
    std::optional<std::size_t> lit;
    // Links and code spans take their clicks first, so that a hovered link is underlined in every piece.
    ImGui::PushID(&line);
    for (std::size_t index = 0; index < placed.size(); ++index) {
        const Placed& piece = placed[index];
        rows = std::max(rows, piece.row + 1);
        const Span& span = line[piece.span];
        if (span.style != Style::link && span.style != Style::code) continue;
        ImGui::SetCursorScreenPos(origin + ImVec2(piece.x, row * static_cast<float>(piece.row)));
        ImGui::PushID(static_cast<int>(index));
        const bool clicked = ImGui::InvisibleButton("##piece", ImVec2(std::max(piece.width, 1.0f), row));
        ImGui::PopID();
        const bool hovered = ImGui::IsItemHovered();
        if (!hovered && !clicked) continue;
        hand_when_hovered();
        if (!span.target.empty()) {
            lit = piece.span;
            const int section = section_of(guide_, span.target);
            const std::string address = section >= 0 || span.target.starts_with("http") ? span.target
                                        : std::string(kProjectPage) + "/blob/main/docs/" + span.target;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", section >= 0 ? guide_.sections[static_cast<std::size_t>(section)].title.c_str() : address.c_str());
            if (clicked && section >= 0) {
                section_ = section;
                section_changed_ = true;
            } else if (clicked) {
                SDL_OpenURL(address.c_str());
            }
        } else {
            if (clicked) {
                ImGui::SetClipboardText(span.text.c_str());
                copied_ = span.text;
                copied_at_ = ImGui::GetTime();
            }
            const bool fresh = copied_ == span.text && ImGui::GetTime() - copied_at_ < 1.5;
            if (fresh || ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", fresh ? "Copied" : "Click to copy");
        }
    }
    ImGui::PopID();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (const Placed& piece : placed) {
        const Span& span = line[piece.span];
        const ImVec2 at = origin + ImVec2(piece.x, row * static_cast<float>(piece.row) + std::floor((row - size) * 0.5f));
        const bool on = lit == piece.span;
        if (span.style == Style::code) {
            draw->AddRectFilled(at + ImVec2(0.0f, -2.0f * scale_), at + ImVec2(piece.width, size + 2.0f * scale_), colour(kRaised), 4.0f * scale_);
            const unsigned ink = span.target.empty() ? 0xd8e6d8 : on ? 0xc4f0c9 : kLink;
            draw->AddText(body_, size, at + ImVec2(pad, 0.0f), colour(ink), piece.text.c_str());
        } else if (span.style == Style::link) {
            draw->AddText(bold_, size, at, colour(on ? 0xc4f0c9 : kLink), piece.text.c_str());
        } else {
            const unsigned ink = span.style == Style::lead ? kGreen
                               : tint                       ? tint
                               : faint ? (span.style == Style::bold ? kGreen : kMuted)
                               : strong || span.style == Style::bold ? 0xffffff : kText;
            draw->AddText(font_of(span.style), size, at, colour(ink), piece.text.c_str());
        }
        if (on) draw->AddLine(at + ImVec2(0.0f, size + 1.0f), at + ImVec2(piece.width, size + 1.0f), colour(0xc4f0c9), std::max(1.0f, scale_));
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, row * static_cast<float>(rows)));
}

void View::about() {
    ImGui::PushFont(bold_, kFontSize * 1.45f);
    ImGui::TextUnformatted("Disruptor Recompiled");
    ImGui::PopFont();
    ImGui::TextDisabled("Version %s", DISRUPTOR_BUILD_VERSION);
    ImGui::Spacing();
    wrapped("A native PC static recompilation of Disruptor (USA, SLUS-00224), built on PSXRecomp\n"
            "No game data is included: the game runs from your own disc");
    ImGui::Spacing();
    ImGui::TextLinkOpenURL("Project page", kProjectPage);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("Releases", kReleasesPage);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("RecompTeam on GitHub", kTeamPage);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("Community Discord", kDiscord);
    std::error_code missing;
    const fs::path licences = session_.program() / "licenses";
    if (fs::is_directory(licences, missing)) {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::TextLink("Licences")) platform::open_folder(licences);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const auto folder = [](const char* label, const fs::path& path, const char* id) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(utf8(path).c_str());
        ImGui::TableNextColumn();
        ImGui::PushID(id);
        if (ImGui::Button("Open")) platform::open_folder(path);
        ImGui::PopID();
    };
    if (ImGui::BeginTable("folders", 3, ImGuiTableFlags_SizingFixedFit)) {
        folder("Your saves and settings", session_.data(), "data");
        folder("Program", session_.program(), "program");
        ImGui::EndTable();
    }
}

} // namespace disruptor::launcher
