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
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
    if (needs_image && count < 3) throw std::runtime_error("A disc image path is required.");
    fs::path program = executable_directory();
    fs::path files = data;
    const int option = needs_image ? 3 : 2;
    if (count == option + 2 && std::string_view(arguments[option]) == "--root") program = files = fs::absolute(from_utf8(arguments[option + 1]));
    else if (count != option) throw std::runtime_error("Unexpected launcher arguments.");
    std::atomic_bool cancelled{false};
    VerifiedDisc verified;
    if (mode == "--verify") verified = verify_disc(from_utf8(arguments[2]), cancelled);
    else if (mode == "--import") verified = import_disc(from_utf8(arguments[2]), files, cancelled);
    else {
        const auto installed = find_installed_disc(files);
        if (installed.empty()) throw std::runtime_error("No supported disc image is installed.");
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
    int scroll = 0;
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
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fail_window(SDL_GetError());
        return 1;
    }
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0.0f || arguments.screenshot) scale = 1.0f;
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    int width = static_cast<int>(1060 * scale), height = static_cast<int>(720 * scale);
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
    SDL_SetWindowMinimumSize(window, static_cast<int>(900 * scale), static_cast<int>(620 * scale));
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Session session(program, data, activity);
    View view(session, updater, activity, choices_file);
    view.style(scale);
    view.show(arguments.screen, arguments.page);
    if (arguments.screenshot) view.for_picture(static_cast<float>(arguments.scroll));
    if (arguments.updates_window) view.open_updates();
    view.attach(window, renderer);
    SDL_SetWindowHitTest(window, hit_test, &view);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    // An open gamepad makes SDL poll every millisecond: the launcher only lists them.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    ImGui_ImplSDLRenderer3_Init(renderer);

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

    const auto frame = [&] {
        session.poll();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const bool leave = view.draw();
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 11, 15, 12, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        return leave;
    };

    int result = 0;
    if (arguments.screenshot) {
        result = 1;
        if (picture) {
            for (int i = 0; i < 4; ++i) frame();
            SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
            const std::u8string name = arguments.screenshot->u8string();
            if (pixels && SDL_SaveBMP(pixels, reinterpret_cast<const char*>(name.c_str()))) result = 0;
            else write_error(std::string("The picture was not saved: ") + SDL_GetError());
            SDL_DestroySurface(pixels);
            SDL_SetRenderTarget(renderer, nullptr);
            SDL_DestroyTexture(picture);
        } else {
            write_error(std::string("There is no picture to draw into: ") + SDL_GetError());
        }
    } else {
        bool running = true;
        int settle = 3;
        while (running) {
            SDL_Event event;
            const bool working = session.busy() || updater.busy() || view.redraw();
            if (SDL_WaitEventTimeout(&event, working || settle > 0 ? 16 : 250)) {
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
                    } else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                        view.style(SDL_GetWindowDisplayScale(window));
                    } else if (event.type == SDL_EVENT_WINDOW_MOVED || event.type == SDL_EVENT_WINDOW_RESIZED) {
                        note_normal();
                    }
                } while (SDL_PollEvent(&event));
                settle = 3;
            } else if (settle > 0) {
                --settle;
            }
            if (session.closed()) running = false;
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                session.poll();
                continue;
            }
            if (frame()) running = false;
            SDL_RenderPresent(renderer);
        }
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
            if (count > 1) write_error("The launcher of this copy is already open.");
            else fail_window("The launcher is already open.");
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
