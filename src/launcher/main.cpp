#include "activity.h"
#include "disc_import.h"
#include "game/settings/user_folder.h"
#include "game_process.h"
#include "launcher_settings.h"
#include "platform.h"
#include "session.h"
#include "update_flow.h"
#include "updater.h"
#include "view.h"

#if DISRUPTOR_UPDATER
#include "Velopack.hpp"
#endif

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace disruptor::launcher;

namespace {

constexpr const char* kRepository = "https://github.com/RecompTeam/DisruptorRecomp";

#if DISRUPTOR_UPDATER
void velopack_log(void* user, const char* level, const char* message) noexcept {
    const char kind = level && *level ? static_cast<char>(level[0] | 0x20) : 'i';
    if (kind != 'i' && kind != 'w' && kind != 'e') return;
    try {
        static_cast<Activity*>(user)->add(std::string("Velopack: ") + (message ? message : ""));
    } catch (...) {
        // Velopack calls this from Rust: nothing may unwind through it.
    }
}
#endif

// Diagnostic modes run the window's checks with redirected output and no console. --root is the program and data folder.
int diagnostic(int count, char** arguments, const fs::path& data) {
    if (count < 2) return -1;
    const std::string_view mode = arguments[1];
    if (mode != "--verify" && mode != "--import" && mode != "--check") return -1;
    const bool needs_image = mode != "--check";
    if (needs_image && count < 3) throw std::runtime_error("A disc image path is required");
    fs::path program = executable_directory();
    fs::path files = data;
    const int option = needs_image ? 3 : 2;
    if (count == option + 2 && std::string_view(arguments[option]) == "--root") program = files = fs::absolute(from_utf8(arguments[option + 1]));
    else if (count != option) throw std::runtime_error("Unexpected launcher arguments");
    std::atomic_bool cancelled{false};
    VerifiedDisc verified;
    if (mode == "--verify") verified = verify_disc(from_utf8(arguments[2]), cancelled);
    else if (mode == "--import") verified = import_disc(from_utf8(arguments[2]), files, cancelled);
    else {
        const auto installed = find_installed_disc(files);
        if (installed.empty()) throw std::runtime_error("No supported disc image is installed");
        verified = verify_disc(installed, cancelled);
        check_package(program, *verified.profile);
    }
    platform::write_standard(std::string("Verified ") + verified.profile->id + "\n", false);
    return 0;
}

struct Arguments {
    std::string source = kRepository;
    std::optional<fs::path> status, update_test, screenshot;
    bool apply = true;
    bool updates_window = false; // the home page with the Updates window open
    Screen screen = Screen::home;
    int page = 0;
    int tab = 0; // of the Controls page
    int scroll = 0;
    int width = 0, height = 0; // a picture's size in pixels, 0 for the usual one
    float scale = 1.0f;
    // A picture's steps before it is taken: keys, gamepad buttons, the pointer, pictures on the way. See drive().
    std::vector<std::string> input;
    std::optional<fs::path> nav_log;
    bool virtual_pad = false;
};

Arguments parse(int count, char** values) {
    Arguments arguments;
    const std::vector<std::string_view> words(values + 1, values + count);
    for (std::size_t i = 0; i < words.size(); ++i) {
        const bool more = i + 1 < words.size();
        if (words[i] == "--updates-from" && more) arguments.source = std::string(words[++i]);
        else if (words[i] == "--status" && more) arguments.status = from_utf8(std::string(words[++i]));
        else if (words[i] == "--update-test" && more) arguments.update_test = from_utf8(std::string(words[++i]));
        else if (words[i] == "--no-apply") arguments.apply = false;
        else if (words[i] == "--screenshot" && more) arguments.screenshot = from_utf8(std::string(words[++i]));
        else if (words[i] == "--screen" && more) {
            const auto screen = words[++i];
            arguments.updates_window = screen == "updates";
            arguments.screen = screen == "settings"   ? Screen::settings
                               : screen == "help"     ? Screen::help
                               : screen == "about"    ? Screen::about
                                                      : Screen::home;
        } else if (words[i] == "--page" && more) {
            const auto page = words[++i];
            std::from_chars(page.data(), page.data() + page.size(), arguments.page);
        } else if (words[i] == "--scroll" && more) {
            const auto pixels = words[++i];
            std::from_chars(pixels.data(), pixels.data() + pixels.size(), arguments.scroll);
        } else if (words[i] == "--tab" && more) {
            const auto tab = words[++i];
            std::from_chars(tab.data(), tab.data() + tab.size(), arguments.tab);
        } else if (words[i] == "--size" && more) {
            const auto size = words[++i];
            const auto cross = std::min(size.find('x'), size.size());
            std::from_chars(size.data(), size.data() + cross, arguments.width);
            if (cross < size.size()) std::from_chars(size.data() + cross + 1, size.data() + size.size(), arguments.height);
        } else if (words[i] == "--scale" && more) {
            const auto factor = words[++i];
            std::from_chars(factor.data(), factor.data() + factor.size(), arguments.scale);
            if (!(arguments.scale >= 0.5f && arguments.scale <= 4.0f)) arguments.scale = 1.0f;
        } else if (words[i] == "--input" && more) {
            std::istringstream steps{std::string(words[++i])};
            for (std::string step; steps >> step;) arguments.input.push_back(step);
        } else if (words[i] == "--nav-log" && more) {
            arguments.nav_log = from_utf8(std::string(words[++i]));
        } else if (words[i] == "--virtual-pad") {
            arguments.virtual_pad = true;
        } else {
            throw std::runtime_error("Usage: DisruptorLauncher.exe --verify IMAGE | --import IMAGE [--root FOLDER] | "
                                     "--check [--root FOLDER]");
        }
    }
    return arguments;
}

void wait_for(const Updater& updater) {
    while (updater.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

// The keys an --input step names, each with the name SDL gives it, which a key button that waits for a key takes.
struct NamedKey {
    std::string_view name;
    ImGuiKey key;
    const char* scancode;
};
constexpr NamedKey kNamedKeys[] = {
    {"Tab", ImGuiKey_Tab, "Tab"},           {"Enter", ImGuiKey_Enter, "Return"},       {"Space", ImGuiKey_Space, "Space"},
    {"Escape", ImGuiKey_Escape, "Escape"},  {"Up", ImGuiKey_UpArrow, "Up"},            {"Down", ImGuiKey_DownArrow, "Down"},
    {"Left", ImGuiKey_LeftArrow, "Left"},   {"Right", ImGuiKey_RightArrow, "Right"},   {"PageUp", ImGuiKey_PageUp, "PageUp"},
    {"PageDown", ImGuiKey_PageDown, "PageDown"}, {"Home", ImGuiKey_Home, "Home"},      {"End", ImGuiKey_End, "End"},
    {"Delete", ImGuiKey_Delete, "Delete"},  {"F1", ImGuiKey_F1, "F1"},
};
// The gamepad buttons an --input step names: what ImGui's SDL backend makes of each, and SDL's own button for a capture.
struct NamedButton {
    std::string_view name;
    ImGuiKey key;
    SDL_GamepadButton button;
};
constexpr NamedButton kNamedButtons[] = {
    {"A", ImGuiKey_GamepadFaceDown, SDL_GAMEPAD_BUTTON_SOUTH},       {"B", ImGuiKey_GamepadFaceRight, SDL_GAMEPAD_BUTTON_EAST},
    {"X", ImGuiKey_GamepadFaceLeft, SDL_GAMEPAD_BUTTON_WEST},        {"Y", ImGuiKey_GamepadFaceUp, SDL_GAMEPAD_BUTTON_NORTH},
    {"Up", ImGuiKey_GamepadDpadUp, SDL_GAMEPAD_BUTTON_DPAD_UP},      {"Down", ImGuiKey_GamepadDpadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"Left", ImGuiKey_GamepadDpadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"Right", ImGuiKey_GamepadDpadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"LB", ImGuiKey_GamepadL1, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},    {"RB", ImGuiKey_GamepadR1, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"LT", ImGuiKey_GamepadL2, SDL_GAMEPAD_BUTTON_INVALID},          {"RT", ImGuiKey_GamepadR2, SDL_GAMEPAD_BUTTON_INVALID},
    {"Start", ImGuiKey_GamepadStart, SDL_GAMEPAD_BUTTON_START},      {"Back", ImGuiKey_GamepadBack, SDL_GAMEPAD_BUTTON_BACK},
};

std::optional<NamedKey> named_key(std::string_view name) {
    for (const NamedKey& key : kNamedKeys)
        if (key.name == name) return key;
    static constexpr const char* letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                              "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z')
        return NamedKey{name, static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'A')), letters[name[0] - 'A']};
    return std::nullopt;
}

int closed(std::ofstream& file, int result) {
    file.close();
    return file.fail() ? 6 : result;
}

// Check, download and hand over to Velopack's updater without a window: what a test needs to see an update applied.
int checked_update(Updater& updater, Activity& activity, std::ofstream& report, bool apply, const fs::path& data) {
    UpdateView view = updater.view();
    report << "installed=" << view.installed << "\nportable=" << view.portable << "\n";
    if (view.state == UpdateState::unavailable) {
        report << "error=" << view.error << "\n";
        return 2;
    }
    report << "pending=" << (view.state == UpdateState::ready) << "\n";
    updater.check();
    wait_for(updater);
    view = updater.view();
    report << "latest=" << view.latest << "\nbytes=" << view.download_bytes << "\ndelta=" << view.delta << "\n";
    if (view.state == UpdateState::available) {
        updater.download();
        wait_for(updater);
        view = updater.view();
        report << "downloaded=" << (view.state == UpdateState::ready) << "\n";
    }
    if (view.state != UpdateState::ready) {
        report << "state=" << static_cast<int>(view.state) << "\nerror=" << view.error << "\n";
        return view.state == UpdateState::current ? 0 : 3;
    }
    if (!apply) return 0;
    const bool applying = apply_update(updater, executable_directory(), data, activity, true, false);
    report << "applying=" << applying << "\n";
    return applying ? 0 : 5;
}

int update_test(Updater& updater, Activity& activity, const fs::path& report_path, bool apply, const fs::path& data) {
    std::ofstream report(report_path, std::ios::binary);
    if (!report) return 6;
    const int result = checked_update(updater, activity, report, apply, data);
    return closed(report, result);
}

int write_status(const Updater& updater, const fs::path& path) {
    const UpdateView view = updater.view();
    std::ofstream status(path, std::ios::binary);
    if (!status) return 6;
    status << "installed=" << view.installed << "\nportable=" << view.portable << "\nstate=" << static_cast<int>(view.state)
           << "\nlatest=" << view.latest << "\nerror=" << view.error << "\n";
    return closed(status, 0);
}

void write_error(const std::string& what) { platform::write_standard(what + "\n", true); }

bool g_unattended = false; // a picture or a test: no dialog may wait for a person

void fail_window(const char* what) {
    if (g_unattended) write_error(what);
    else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Disruptor Recompiled", what, nullptr);
}

// The window draws its own title bar. Windows 11 still rounds its corners and draws its edge in the launcher's colour.
void shape_frame([[maybe_unused]] SDL_Window* window) {
#ifdef _WIN32
    constexpr DWORD use_dark_mode = 20, corner_preference = 33, border_colour = 34, round_corners = 2;
    const auto handle = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!handle) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(handle, use_dark_mode, &dark, sizeof(dark));
    DwmSetWindowAttribute(handle, corner_preference, &round_corners, sizeof(round_corners));
    const COLORREF edge = RGB(38, 50, 40);
    DwmSetWindowAttribute(handle, border_colour, &edge, sizeof(edge));
#endif
}

SDL_HitTestResult SDLCALL hit_test(SDL_Window* window, const SDL_Point* point, void* data) {
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const bool maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    switch (hit_at(static_cast<float>(point->x), static_cast<float>(point->y), static_cast<float>(width),
                   static_cast<float>(height), static_cast<const View*>(data)->title_bar(), maximized)) {
    case Hit::client: return SDL_HITTEST_NORMAL;
    case Hit::drag: return SDL_HITTEST_DRAGGABLE;
    case Hit::top_left: return SDL_HITTEST_RESIZE_TOPLEFT;
    case Hit::top: return SDL_HITTEST_RESIZE_TOP;
    case Hit::top_right: return SDL_HITTEST_RESIZE_TOPRIGHT;
    case Hit::right: return SDL_HITTEST_RESIZE_RIGHT;
    case Hit::bottom_right: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    case Hit::bottom: return SDL_HITTEST_RESIZE_BOTTOM;
    case Hit::bottom_left: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    case Hit::left: return SDL_HITTEST_RESIZE_LEFT;
    }
    return SDL_HITTEST_NORMAL;
}

// A place is used only when its title bar would be on a monitor that is there now.
bool on_a_display(const WindowPlace& place) {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    bool seen = false;
    for (int index = 0; displays && index < count && !seen; ++index) {
        SDL_Rect usable{};
        if (!SDL_GetDisplayUsableBounds(displays[index], &usable)) continue;
        const long long left = std::max<long long>(place.x, usable.x);
        const long long right = std::min<long long>(0LL + place.x + place.width, 0LL + usable.x + usable.w);
        const long long top = std::max<long long>(place.y, usable.y);
        const long long bottom = std::min<long long>(0LL + place.y + 40, 0LL + usable.y + usable.h);
        seen = right - left >= 120 && bottom - top >= 20;
    }
    SDL_free(displays);
    return seen;
}

// Direct3D 11 where there is one, else whatever SDL can draw with, SDL's own software renderer last: setup, settings
// and a report must open on a machine whose driver cannot run the game.
SDL_Renderer* make_renderer(SDL_Window* window) {
    if (SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr)) return renderer;
    return SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
}

int run_window(const Arguments& arguments, Updater& updater, Activity& activity, const fs::path& program,
               const fs::path& data) {
    g_unattended = arguments.screenshot.has_value();
    // A picture where SDL finds no display to draw on ends with 5, which a test takes for a skip.
    const int no_display = arguments.screenshot ? 5 : 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fail_window(SDL_GetError());
        return no_display;
    }
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (arguments.screenshot) scale = arguments.scale > 0.0f ? arguments.scale : 1.0f;
    else if (scale <= 0.0f) scale = 1.0f;
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    int width = static_cast<int>(1060 * scale), height = static_cast<int>(720 * scale);
    if (arguments.screenshot && arguments.width > 0 && arguments.height > 0) {
        width = arguments.width;
        height = arguments.height;
    }
    // A first start takes most of a tall screen, so the home page has room for the release notes.
    SDL_Rect usable{};
    if (!arguments.screenshot && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.h > 0) {
        height = std::clamp(static_cast<int>(static_cast<float>(usable.h) * 0.78f), height, static_cast<int>(900 * scale));
        width = std::max(std::min(height * 1060 / 720, static_cast<int>(static_cast<float>(usable.w) * 0.9f)),
                         static_cast<int>(900 * scale));
        width = std::min(width, usable.w);
        height = std::min(height, usable.h);
    }
    const fs::path choices_file = data / "launcher.ini";
    const std::optional<WindowPlace> saved = arguments.screenshot ? std::nullopt : read_choices(choices_file).window;
    const bool placed = saved && on_a_display(*saved);
    SDL_Window* window = SDL_CreateWindow("Disruptor Recompiled", placed ? saved->width : width,
                                          placed ? saved->height : height, flags);
    SDL_Renderer* renderer = window ? make_renderer(window) : nullptr;
    if (!renderer) {
        fail_window(SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // 900 by 620 at the display's scale, at most its usable room: 125% on 1366 by 768 wants 775 rows of 728.
    const auto fit_minimum = [&](float at_scale) {
        int least_width = static_cast<int>(900 * at_scale), least_height = static_cast<int>(620 * at_scale);
        if (SDL_Rect room{}; SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &room) && room.w > 0 && room.h > 0) {
            least_width = std::min(least_width, room.w);
            least_height = std::min(least_height, room.h);
        }
        // A picture of a smaller screen's room is drawn at that size, as the window there would be.
        if (arguments.screenshot && arguments.width > 0 && arguments.height > 0) {
            least_width = std::min(least_width, arguments.width);
            least_height = std::min(least_height, arguments.height);
        }
        SDL_SetWindowMinimumSize(window, least_width, least_height);
    };
    fit_minimum(scale);
    SDL_SetRenderVSync(renderer, 1);
    shape_frame(window);
    if (placed) SDL_SetWindowPosition(window, saved->x, saved->y);
    // The place the window is restored to: kept while it is maximized or minimized, so it is what closing saves.
    std::optional<WindowPlace> normal = placed ? saved : std::nullopt;
    const auto note_normal = [&] {
        if (SDL_GetWindowFlags(window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) return;
        WindowPlace now;
        SDL_GetWindowPosition(window, &now.x, &now.y);
        SDL_GetWindowSize(window, &now.width, &now.height);
        normal = now;
    };
    if (!arguments.screenshot) {
        SDL_ShowWindow(window);
        note_normal();
        if (placed && saved->maximized) SDL_MaximizeWindow(window);
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    // Ctrl+Tab steps through the launcher's pages instead of ImGui's window list: there is only one window.
    GImGui->ConfigNavWindowingKeyNext = ImGuiKey_None;
    GImGui->ConfigNavWindowingKeyPrev = ImGuiKey_None;
    Session session(program, data, activity);
    View view(session, updater, activity, choices_file);
    view.style(scale);
    view.show(arguments.screen, arguments.page, arguments.tab);
    if (arguments.screenshot) view.for_picture(static_cast<float>(arguments.scroll));
    if (arguments.screenshot && arguments.virtual_pad) view.pretend_pad();
    if (arguments.updates_window) view.open_updates();
    view.attach(window, renderer);
    SDL_SetWindowHitTest(window, hit_test, &view);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    // A picture's steps may press a link: it opens nothing.
    if (arguments.screenshot) ImGui::GetPlatformIO().Platform_OpenInShellFn = [](ImGuiContext*, const char*) { return false; };
    // The first gamepad moves through the window while the window has the keyboard. In the background the game has it.
    SDL_Gamepad* nav_pad = nullptr;
    bool gamepads = false, gamepads_tried = false;
    const auto choose_nav_pad = [&] {
        const bool focused = arguments.screenshot || (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
        if (nav_pad && (!focused || !SDL_GamepadConnected(nav_pad))) {
            SDL_CloseGamepad(nav_pad);
            nav_pad = nullptr;
        }
        if (!nav_pad && focused && gamepads) {
            int count = 0;
            SDL_JoystickID* found = SDL_GetGamepads(&count);
            if (found && count > 0) nav_pad = SDL_OpenGamepad(found[0]);
            SDL_free(found);
        }
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nav_pad ? &nav_pad : nullptr, nav_pad ? 1 : 0);
    };
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);

    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    session.open(mode ? mode->refresh_rate : 0.0);
    if (view.choices().check_for_updates && updater.view().state == UpdateState::idle) updater.check();
    SDL_Texture* picture = nullptr;
    if (arguments.screenshot) {
        wait_for(updater);
        for (int waited = 0; waited < 6000 && session.busy(); ++waited) {
            session.poll();
            SDL_Delay(20);
        }
        picture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!picture || !SDL_SetRenderTarget(renderer, picture)) {
            if (picture) SDL_DestroyTexture(picture);
            picture = nullptr;
        }
    }

    bool drawing = false;
    const auto frame = [&] {
        drawing = true;
        session.poll();
        // A button pressed for the Gamepad page's capture must not also move or press in the window, held or let go.
        if (view.pad_blocked()) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        // --virtual-pad: steps press a gamepad's buttons at ImGui's input, which the backend clears with no pad open.
        if (arguments.virtual_pad) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        ImGui::NewFrame();
        const bool leave = view.draw();
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 11, 15, 12, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        drawing = false;
        return leave;
    };

    const auto save_picture = [&](const fs::path& path) {
        SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
        const std::u8string name = path.u8string();
        const bool saved = pixels && SDL_SaveBMP(pixels, reinterpret_cast<const char*>(name.c_str()));
        if (!saved) write_error(std::string("The picture was not saved: ") + SDL_GetError());
        SDL_DestroySurface(pixels);
        return saved;
    };
    // Where the keyboard and gamepad cursor is after a step: its window, place, whether it shows, the open popup, the view.
    const auto note = [&](std::ofstream& log, std::string_view step) {
        const ImGuiContext& g = *GImGui;
        ImRect at;
        if (g.NavWindow && g.NavId) at = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[g.NavLayer]);
        const char* popup = g.OpenPopupStack.empty() || !g.OpenPopupStack.back().Window ? "-" : g.OpenPopupStack.back().Window->Name;
        log << step << '\t' << (g.NavWindow ? g.NavWindow->Name : "-") << '\t' << static_cast<int>(at.Min.x) << ' '
            << static_cast<int>(at.Min.y) << ' ' << static_cast<int>(at.GetWidth()) << ' ' << static_cast<int>(at.GetHeight()) << '\t'
            << (g.NavCursorVisible ? "cursor" : "no-cursor") << '\t' << popup << '\t' << view.describe() << '\n';
    };
    // --input steps: a key ("Shift+Tab"), "Pad:A", "Hover:x,y", "Click:x,y", "Wheel:x,y,lines", "Sleep:ms" or "@name" for a picture on the way.
    const auto drive = [&] {
        std::ofstream log;
        if (arguments.nav_log) log.open(*arguments.nav_log, std::ios::binary);
        bool driven = true;
        const auto number_after = [](std::string_view word, std::size_t from) {
            float value = 0.0f;
            std::from_chars(word.data() + from, word.data() + word.size(), value);
            return value;
        };
        for (const std::string& step : arguments.input) {
            const std::string_view word = step;
            if (word.starts_with('@')) {
                fs::path named = *arguments.screenshot;
                named.replace_filename(from_utf8(utf8(named.stem()) + "-" + std::string(word.substr(1)) + ".bmp"));
                driven = save_picture(named) && driven;
                continue;
            }
            if (word.starts_with("Sleep:")) {
                SDL_Delay(static_cast<Uint32>(number_after(word, 6)));
                frame();
            } else if (word.starts_with("Hover:") || word.starts_with("Click:") || word.starts_with("Wheel:")) {
                const std::size_t comma = word.find(',');
                io.AddMousePosEvent(number_after(word.substr(0, comma), 6), comma == std::string_view::npos ? 0.0f : number_after(word, comma + 1));
                frame();
                if (word.starts_with("Click:")) {
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                    frame();
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                    frame();
                }
                if (word.starts_with("Wheel:")) {
                    io.AddMouseWheelEvent(0.0f, number_after(word, word.rfind(',') + 1));
                    frame();
                }
                frame();
            } else if (word.starts_with("Pad:")) {
                const auto button = std::ranges::find(kNamedButtons, word.substr(4), &NamedButton::name);
                if (!arguments.virtual_pad || button == std::end(kNamedButtons)) {
                    write_error("No gamepad button named in the step " + step + ", or no --virtual-pad");
                    driven = false;
                    continue;
                }
                // As the main loop hands a capture a button's press before ImGui reads the pad.
                if (view.listening_pad() && button->button != SDL_GAMEPAD_BUTTON_INVALID) view.take_pad_button(button->button);
                io.AddKeyAnalogEvent(button->key, true, 1.0f);
                frame();
                io.AddKeyAnalogEvent(button->key, false, 0.0f);
                frame();
                frame();
            } else {
                std::string_view name = word;
                ImGuiKeyChord held = ImGuiKey_None;
                for (const auto& [prefix, modifier] : {std::pair{std::string_view("Ctrl+"), ImGuiMod_Ctrl}, std::pair{std::string_view("Shift+"), ImGuiMod_Shift}})
                    if (name.starts_with(prefix)) {
                        held |= modifier;
                        name.remove_prefix(prefix.size());
                    }
                const auto key = named_key(name);
                if (!key) {
                    write_error("No key named in the step " + step);
                    driven = false;
                    continue;
                }
                if (view.listening()) {
                    view.take_key(key->scancode);
                    frame();
                } else {
                    for (const ImGuiKey modifier : {ImGuiMod_Ctrl, ImGuiMod_Shift})
                        if (held & modifier) io.AddKeyEvent(modifier, true);
                    io.AddKeyEvent(key->key, true);
                    frame();
                    io.AddKeyEvent(key->key, false);
                    for (const ImGuiKey modifier : {ImGuiMod_Ctrl, ImGuiMod_Shift})
                        if (held & modifier) io.AddKeyEvent(modifier, false);
                    frame();
                    frame();
                }
            }
            if (log) note(log, step);
        }
        return driven;
    };

    int result = 0;
    if (arguments.screenshot) {
        result = 1;
        if (picture) {
            for (int i = 0; i < 4; ++i) frame();
            if (drive() && save_picture(*arguments.screenshot)) result = 0;
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_DestroyTexture(picture);
        } else {
            write_error(std::string("There is no picture to draw into: ") + SDL_GetError());
        }
    } else {
        // A drag of the window's edge keeps the loop inside Windows' own until the button is let go: draw from the event.
        struct Live {
            const decltype(frame)* draw;
            const bool* drawing;
            SDL_Window* window;
            SDL_Renderer* renderer;
        } live{&frame, &drawing, window, renderer};
        const SDL_EventFilter redraw = [](void* data, SDL_Event* event) -> bool {
            const Live& live = *static_cast<const Live*>(data);
            const bool sized = event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_EXPOSED;
            if (sized && event->window.windowID == SDL_GetWindowID(live.window) && !*live.drawing) {
                (*live.draw)();
                SDL_RenderPresent(live.renderer);
            }
            return true;
        };
        SDL_AddEventWatch(redraw, &live);
        bool running = true;
        // Frames go on a while after the last event, so a tooltip shows at its own delay and not at the next 250 ms wake.
        constexpr Uint64 kLively = 700;
        Uint64 lively_until = SDL_GetTicks() + kLively;
        while (running) {
            SDL_Event event;
            const bool working = session.busy() || session.achievements().busy() || updater.busy() || view.redraw();
            if (SDL_WaitEventTimeout(&event, working || SDL_GetTicks() < lively_until ? 16 : 250)) {
                do {
                    // Releases still reach ImGui, which saw the presses made before the key button listened.
                    if (view.listening_pad()) {
                        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                            view.take_pad_button(event.gbutton.button);
                            continue;
                        }
                        if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                            view.take_pad_axis(event.gaxis.which, event.gaxis.axis, event.gaxis.value);
                            continue;
                        }
                        if (event.type == SDL_EVENT_KEY_DOWN) {
                            if (event.key.scancode == SDL_SCANCODE_ESCAPE) view.stop_pad_capture();
                            continue;
                        }
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && view.listening()) {
                        if (!event.key.repeat) view.take_key(SDL_GetScancodeName(event.key.scancode));
                        continue;
                    }
                    ImGui_ImplSDL3_ProcessEvent(&event);
                    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                        if (session.close()) running = false;
                    } else if (event.type == SDL_EVENT_DROP_BEGIN) {
                        view.dragging(true);
                    } else if (event.type == SDL_EVENT_DROP_COMPLETE) {
                        view.dragging(false);
                    } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
                        view.dropped(from_utf8(event.drop.data));
                    } else if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED ||
                               event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                        choose_nav_pad();
                    } else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED || event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) {
                        view.style(SDL_GetWindowDisplayScale(window));
                        fit_minimum(SDL_GetWindowDisplayScale(window));
                    } else if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                        note_normal();
                    }
                } while (SDL_PollEvent(&event));
                lively_until = SDL_GetTicks() + kLively;
            }
            if (session.closed()) running = false;
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                session.poll();
                continue;
            }
            if (frame()) running = false;
            SDL_RenderPresent(renderer);
            // After the first frame is on screen: opening the gamepads made the start about 120 ms slower.
            if (!std::exchange(gamepads_tried, true)) {
                gamepads = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
                choose_nav_pad();
            }
        }
        SDL_RemoveEventWatch(redraw, &live);
    }

    if (!arguments.screenshot) {
        LauncherChoices kept = view.choices();
        note_normal();
        if (normal) {
            normal->maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
            kept.window = normal;
        }
        write_choices(choices_file, kept);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (nav_pad) SDL_CloseGamepad(nav_pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

} // namespace

int main(int count, char** values) {
#if DISRUPTOR_UPDATER
    Velopack::VelopackApp::Build().SetAutoApplyOnStartup(false).Run();
#endif
    static Activity activity;
    try {
        const fs::path program = executable_directory();
        const fs::path data = disruptor::user_folder::prepare(program);
        // One launcher for a folder of the player's files: two would write them at once.
        const platform::Instance single(data);
        if (!single.first()) {
            if (count > 1) write_error("The launcher of this copy is already open");
            else fail_window("The launcher is already open");
            return 1;
        }
        const int result = diagnostic(count, values, data);
        if (result >= 0) return result;
#ifdef _WIN32
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
        // The Language row names the disc and checks it: the game would let this variable replace it unchecked.
        platform::unset("PSX_DISRUPTOR_LANGUAGE_DISC");
        const Arguments arguments = parse(count, values);
        Updater updater(arguments.source, activity);
#if DISRUPTOR_UPDATER
        if (updater.view().state != UpdateState::unavailable) vpkc_set_logger(velopack_log, &activity);
#endif
        if (arguments.status) return write_status(updater, *arguments.status);
        if (arguments.update_test) return update_test(updater, activity, *arguments.update_test, arguments.apply, data);
        return run_window(arguments, updater, activity, program, data);
    } catch (const std::exception& error) {
        if (count > 1) {
            write_error(error.what());
        } else {
            fail_window(utf8(fs::path(error_text(error))).c_str());
        }
        return 1;
    }
}
