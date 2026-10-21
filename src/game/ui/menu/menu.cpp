#include "game/ui/menu/menu.h"

namespace disruptor::menu {

extern "C" const unsigned char disruptor_window_icon_bmp[];
extern "C" const std::size_t disruptor_window_icon_bmp_size;

DevMenuState g_menu;

namespace {

// The launcher's icon on the game's window and in the taskbar.
void set_window_icon(SDL_Window *window) {
    if (!window) return;
#if defined(PSX_SDL3)
    SDL_Surface *icon = SDL_LoadBMP_IO(SDL_IOFromConstMem(disruptor_window_icon_bmp, disruptor_window_icon_bmp_size), true);
#else
    SDL_Surface *icon = SDL_LoadBMP_RW(
        SDL_RWFromConstMem(disruptor_window_icon_bmp, static_cast<int>(disruptor_window_icon_bmp_size)), 1);
#endif
    if (!icon) return;
    SDL_SetWindowIcon(window, icon);
#if defined(PSX_SDL3)
    SDL_DestroySurface(icon);
#else
    SDL_FreeSurface(icon);
#endif
}

bool imgui_sdl_init(SDL_Window *window) {
#if defined(PSX_SDL3)
    return ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext());
#else
    return ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext());
#endif
}

void imgui_sdl_shutdown() {
#if defined(PSX_SDL3)
    ImGui_ImplSDL3_Shutdown();
#else
    ImGui_ImplSDL2_Shutdown();
#endif
}

void imgui_sdl_new_frame() {
#if defined(PSX_SDL3)
    ImGui_ImplSDL3_NewFrame();
#else
    ImGui_ImplSDL2_NewFrame();
#endif
}

void imgui_sdl_process_event(const SDL_Event *event) {
#if defined(PSX_SDL3)
    (void)ImGui_ImplSDL3_ProcessEvent(event);
#else
    (void)ImGui_ImplSDL2_ProcessEvent(event);
#endif
}

void apply_disruptor_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;

    const ImVec4 green(0.20f, 0.78f, 0.34f, 1.00f);
    const ImVec4 green_hover(0.28f, 0.90f, 0.43f, 1.00f);
    const ImVec4 green_active(0.14f, 0.62f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = green_hover;
    style.Colors[ImGuiCol_SliderGrab] = green;
    style.Colors[ImGuiCol_SliderGrabActive] = green_hover;
    style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.38f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = green_active;
    style.Colors[ImGuiCol_HeaderActive] = green;
    style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.34f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = green_active;
    style.Colors[ImGuiCol_ButtonActive] = green;
}

bool initialize_imgui() {
    if (g_menu.imgui_ready) return true;
    if (!g_menu.window || g_menu.backend != PSX_HOST_UI_BACKEND_OPENGL)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Persist only the reviewed gameplay/presentation fields below.  ImGui's
     * ambient layout file is deliberately disabled: it is relative-path,
     * independently written, and outside our atomic settings contract. */
    io.IniFilename = nullptr;
    apply_disruptor_style();

    if (!imgui_sdl_init(g_menu.window)) {
        ImGui::DestroyContext();
        std::fprintf(stderr,
                     "disruptor: developer menu SDL backend initialization failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        imgui_sdl_shutdown();
        ImGui::DestroyContext();
        std::fprintf(stderr,
                     "disruptor: developer menu OpenGL initialization failed\n");
        return false;
    }

    g_menu.imgui_ready = true;
    std::fprintf(stdout,
                 "disruptor: in-game settings menu ready (press ` to open)\n");
    return true;
}

void start_shown_recording() {
    g_menu.record_shown = false;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const fs::path folder = g_preferences.path.parent_path() / "shown" / std::to_string(now);
    std::error_code failed;
    fs::create_directories(folder, failed);
    if (failed) {
        g_menu.shown_note = "The folder for the pictures could not be made";
        return;
    }
    g_menu.shown_note = gl_renderer_record_shown(utf8(folder).c_str(), 120, 240, 960) ? "Last recording: " + utf8(folder)
                                                                                      : "The recording could not start";
}

}  // namespace

void set_menu_open(bool open) {
    if (open == g_menu.open) return;
    if (open) {
        disruptor_selection_wheel_cancel();
        g_menu.restore_mouse_capture = disruptor_mouse_captured() != 0;
        if (g_menu.restore_mouse_capture)
            (void)disruptor_mouse_set_captured(0);
        g_menu.open = true;
    } else {
        g_menu.open = false;
        (void)flush_preferences();
        if (g_menu.record_shown) start_shown_recording();
        if (g_menu.restore_mouse_capture)
            (void)disruptor_mouse_set_captured(1);
        g_menu.restore_mouse_capture = false;
    }
}

namespace {

bool key_down(const SDL_Event *event, SDL_Scancode scancode) {
    if (!event || event->type != SDL_KEYDOWN) return false;
#if defined(PSX_SDL3)
    return event->key.scancode == scancode;
#else
    return event->key.keysym.scancode == scancode;
#endif
}

/* The modifiers the event carries: the keyboard's state now may be a release later. */
int key_held(const SDL_Event *event) {
#if defined(PSX_SDL3)
    return event->key.mod;
#else
    return event->key.keysym.mod;
#endif
}

bool scancode_event(const SDL_Event *event, SDL_Scancode scancode) {
    return key_down(event, scancode) && !event->key.repeat;
}

void draw_menu() {
    char picked[4096];
    if (psx_host_pick_disc_image_poll(picked, static_cast<int>(sizeof picked)) == 1) mark_language_disc(picked);
    bool keep_open = g_menu.open;
    const ImVec2 room = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(880.0f, room.x - 32.0f), std::min(640.0f, room.y - 32.0f)),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Disruptor Settings", &keep_open,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushTextWrapPos(0.0f);  // a long note wraps at the window's edge, where it used to be cut off
        ImGui::TextDisabled("Press ` to toggle");
        ImGui::TextDisabled("Escape closes the menu");
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
                           "The game is not paused while this menu is open");

        if (ImGui::BeginTabBar("DisruptorDevTabs")) {
            if (ImGui::BeginTabItem("Controls")) {
                draw_controls_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Display")) {
                draw_display_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Enhancements")) {
                draw_enhancements_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Audio")) {
                draw_audio_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Experimental")) {
                draw_experimental_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Language")) {
                draw_language_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("System")) {
                draw_system_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                draw_debug_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Cheats")) {
                draw_cheats_tab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (!keep_open && g_menu.open) set_menu_open(false);
}

void on_runtime_ready(void *, SDL_Window *window, int backend) {
    disruptor_cheats_reset_session();
    disruptor_far_rendering_reset_session();
    disruptor_billboard_aspect_set_site_mask(
        disruptor_billboard_aspect_all_site_mask());
    disruptor_mouse_recenter_vertical();
    disruptor_high_precision_camera_set_enabled(0);
    gte_ws_set_far_threshold(900);
    gte_ws_set_backdrop_repair_enabled(0);
    g_menu.window = window;
    g_menu.backend = backend;
    set_window_icon(window);
    load_preferences_for_session();
    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();
    disruptor_selection_wheels_set_available(g_menu.imgui_ready && backend == PSX_HOST_UI_BACKEND_OPENGL);
}

void on_runtime_shutdown(void *) {
    disruptor_selection_wheels_set_available(0);
    disruptor_cheats_reset_session();
    disruptor_far_rendering_reset_session();
    disruptor_billboard_aspect_set_site_mask(
        disruptor_billboard_aspect_all_site_mask());
    disruptor_mouse_recenter_vertical();
    disruptor_high_precision_camera_set_enabled(0);
    gte_ws_set_far_threshold(900);
    gte_ws_set_backdrop_repair_enabled(0);
    (void)flush_preferences();
    if (disruptor_mouse_captured())
        (void)disruptor_mouse_set_captured(0);
    g_menu.open = false;
    g_menu.restore_mouse_capture = false;
    if (g_menu.imgui_ready) {
        ImGui_ImplOpenGL3_Shutdown();
        imgui_sdl_shutdown();
        ImGui::DestroyContext();
    }
    g_menu = DevMenuState{};
}

int on_sdl_event(void *, const SDL_Event *event) {
    if (event && event->type == SDL_CONTROLLERAXISMOTION) {
#if defined(PSX_SDL3)
        disruptor_selection_wheel_controller_axis(event->gaxis.axis, event->gaxis.value);
#else
        disruptor_selection_wheel_controller_axis(event->caxis.axis, event->caxis.value);
#endif
    }
    if (event && event->type == SDL_CONTROLLERDEVICEREMOVED) {
        disruptor_selection_wheel_cancel();
        disruptor_selection_wheel_controller_axis(0, 0);
        disruptor_selection_wheel_controller_axis(1, 0);
    }
    const bool toggle = scancode_event(event, SDL_SCANCODE_GRAVE);
    const bool close =
        g_menu.open && scancode_event(event, SDL_SCANCODE_ESCAPE);
    if (toggle || close) {
        set_menu_open(toggle ? !g_menu.open : false);
        return 1;
    }
    if (disruptor_selection_wheel_open()) {
        if (scancode_event(event, SDL_SCANCODE_ESCAPE)) disruptor_selection_wheel_cancel();
        return 1;
    }
    if (!g_menu.open && scancode_event(event, SDL_SCANCODE_ESCAPE))
        disruptor_intro_skip_request();
    if (!g_menu.open && key_down(event, SDL_SCANCODE_F12)) {
        const int held = key_held(event);
        disruptor_capsule_key(event->key.repeat ? 1 : 0, (held & KMOD_CTRL) != 0, (held & KMOD_SHIFT) != 0);
        return 1;
    }
    /* While a capsule's state is saved or loaded the save state keys would act in its folder. */
    for (int key = SDL_SCANCODE_F1; !g_menu.open && key <= SDL_SCANCODE_F11; ++key)
        if (key_down(event, static_cast<SDL_Scancode>(key)) && disruptor_capsule_holds_states()) return 1;
    if (!g_menu.open || !g_menu.imgui_ready) return 0;
    imgui_sdl_process_event(event);
    return 1;
}

uint32_t current_flags(void *) {
    if (disruptor_selection_wheel_open())
        return PSX_HOST_UI_VISIBLE;
    if (!g_menu.open || !g_menu.imgui_ready) return disruptor_capsule_notice() ? PSX_HOST_UI_VISIBLE : 0u;
    return PSX_HOST_UI_CAPTURE_KEYBOARD |
           PSX_HOST_UI_CAPTURE_MOUSE |
           PSX_HOST_UI_CAPTURE_GAMEPAD |
           PSX_HOST_UI_VISIBLE;
}

/* Drawn without the SDL backend's frame: that one reads and captures the mouse, which is the game's while the menu is closed. */
void render_notice(const char *word, int width, int height) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f));
    ImGui::Begin("##capsule", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(std::max(1.0f, static_cast<float>(height) / 360.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.2f, 1.0f), "%s", word);
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void render_gl(void *, int width, int height) {
    if (disruptor_selection_wheel_open()) {
        if (!initialize_imgui()) return;
        gl_renderer_set_host_ui_suspended(1);
        ImGui_ImplOpenGL3_NewFrame();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        disruptor::wheel::draw(width, height);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }
    if (const char *word = g_menu.open ? nullptr : disruptor_capsule_notice()) {
        if (initialize_imgui()) render_notice(word, width, height);
        return;
    }
    if (!g_menu.open || !initialize_imgui()) return;
    ImGui_ImplOpenGL3_NewFrame();
    imgui_sdl_new_frame();
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

struct MenuRegistration {
    MenuRegistration() {
        PsxHostUiHooks hooks{};
        hooks.abi_version = PSX_HOST_UI_ABI_VERSION;
        hooks.struct_size = static_cast<uint32_t>(sizeof(hooks));
        hooks.on_runtime_ready = on_runtime_ready;
        hooks.on_runtime_shutdown = on_runtime_shutdown;
        hooks.on_sdl_event = on_sdl_event;
        hooks.flags = current_flags;
        hooks.render_gl = render_gl;
        if (!psx_host_ui_register(&hooks)) {
            std::fprintf(stderr,
                         "disruptor: settings menu registration failed\n");
        }
    }
};

MenuRegistration g_registration;

}  // namespace

}  // namespace disruptor::menu
