#ifndef PSX_HOST_UI_H
#define PSX_HOST_UI_H

/* Optional in-game host UI seam. A statically linked game module registers one callback table before main(). The runtime
 * forwards SDL events, brackets the SDL/renderer lifetime and calls the OpenGL draw callback on the main GL context before
 * SwapWindow. Without a provider every function is a no-op. Callbacks and live setting functions are main-thread only.
 * The table is copied at registration, `userdata` stays caller-owned. */

#include <stddef.h>
#include <stdint.h>

#include "psx_sdl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_HOST_UI_ABI_VERSION 1u

enum PsxHostUiBackend {
    PSX_HOST_UI_BACKEND_NONE     = 0,
    PSX_HOST_UI_BACKEND_SOFTWARE = 1,
    PSX_HOST_UI_BACKEND_OPENGL   = 2,
    PSX_HOST_UI_BACKEND_VULKAN   = 3,
};

enum PsxHostUiFlags {
    /* Suppress the polled host input before it reaches SIO or a game mod. */
    PSX_HOST_UI_CAPTURE_KEYBOARD = 1u << 0,
    PSX_HOST_UI_CAPTURE_MOUSE    = 1u << 1,
    PSX_HOST_UI_CAPTURE_GAMEPAD  = 1u << 2,

    /* Stop high-refresh interpolation while the UI is open, so render_gl runs on the emulation thread's GL context. */
    PSX_HOST_UI_SUSPEND_INTERPOLATION = 1u << 3,

    /* The provider has visible GL output: defeats unchanged-frame and display-disabled present elision. */
    PSX_HOST_UI_VISIBLE = 1u << 4,
};

typedef struct PsxHostUiHooks {
    uint32_t abi_version; /* PSX_HOST_UI_ABI_VERSION */
    uint32_t struct_size; /* sizeof(PsxHostUiHooks)   */
    void *userdata;

    /* Once the window and renderer are ready, with the GL context current for PSX_HOST_UI_BACKEND_OPENGL. */
    void (*on_runtime_ready)(void *userdata, SDL_Window *window, int backend);

    /* At most once per ready session, before its renderer context and window are destroyed. */
    void (*on_runtime_shutdown)(void *userdata);

    /* Before hotkey handling. Non-zero consumes the event, except SDL_QUIT and controller hotplug bookkeeping. */
    int (*on_sdl_event)(void *userdata, const SDL_Event *event);

    uint32_t (*flags)(void *userdata);

    /* On the main GL context after the game image, before SwapWindow. The provider restores any GL state it changes. */
    void (*render_gl)(void *userdata, int drawable_width, int drawable_height);
} PsxHostUiHooks;

/* Register the sole provider. 1 on success, 0 for an invalid table, ABI mismatch or a second provider. */
int psx_host_ui_register(const PsxHostUiHooks *hooks);

uint32_t psx_host_ui_capture_flags(void);
int psx_host_ui_game_input_captured(void);

/* A game module's fast-forward. While its query answers non-zero the frontend
 * runs guest frames unpaced, presents nothing and drops the sound it renders.
 * Asked once per VBlank on the emulation thread, never under netplay. */
void psx_host_set_fast_forward_query(int (*query)(void));

/* Asks the player for a disc image with the platform's file dialog, on a thread of its own so that the game keeps
 * running behind it. 1 when the dialog opened, 0 when one is open already, none could be started or the platform has none. */
int psx_host_pick_disc_image_begin(void);
/* The dialog's answer, given once: 1 with the path in `out`, -1 when it closed without one or the path does not
 * fit, 0 while it is open or none was begun. */
int psx_host_pick_disc_image_poll(char *out, int size);

/* UTF-8 path to the runtime's user settings file, valid for the session: the same settings.toml as the launcher. */
const char *psx_host_user_settings_path(void);

enum PsxHostFrameInterpolationMode {
    PSX_HOST_FRAME_INTERPOLATION_LINEAR = 0,
    PSX_HOST_FRAME_INTERPOLATION_MOTION_ADAPTIVE = 1,
    PSX_HOST_FRAME_INTERPOLATION_GEOMETRY = 2,
};

/* Live, session-only video controls, persistence belongs to the game UI. Aspect changes apply at the next presentation
 * boundary: fixed 4:3, 16:9, 21:9, 32:9, or adaptive between 4:3 and 32:9 (a fixed choice disables it). Internal scale
 * 1..8, live on OpenGL only. Fullscreen 0 windowed, 1 borderless, 2 exclusive. Vsync -1 adaptive, 0 immediate, 1
 * synchronised. Interpolation target -1 uncapped, 0 display refresh, or 60..1000 FPS. Geometry mode redraws matched
 * world triangles between source frames, needs exact-provenance geometry and never changes that setting. */
void psx_host_video_get_display_aspect(int *numerator, int *denominator);
int psx_host_video_set_display_aspect(int numerator, int denominator);
int psx_host_video_get_adaptive_view(void);
int psx_host_video_set_adaptive_view(int enabled);
int psx_host_video_get_internal_scale(void);
int psx_host_video_set_internal_scale(int scale);
int psx_host_video_get_fullscreen_mode(void);
int psx_host_video_set_fullscreen_mode(int mode);
int psx_host_video_get_vsync(void);
int psx_host_video_set_vsync(int mode);
void psx_host_video_get_interpolation(int *enabled, int *target_fps,
                                      int *blend_mode);
int psx_host_video_set_interpolation(int enabled, int target_fps,
                                     int blend_mode);
/* Geometry interpolation diagnostics: scenes published, redraws, fallback captures, last scene's matched and world
 * triangles, last scene replayable (0/1). Six zeros without OpenGL. */
void psx_host_video_get_geometry_interpolation_stats(uint64_t out[6]);

/* Host-output audio, applied after the SPU mix. Volume 0..100 percent, muting a separate latch that keeps the volume. */
int psx_host_audio_get_master_volume(void);
int psx_host_audio_set_master_volume(int percent);
int psx_host_audio_get_muted(void);
int psx_host_audio_set_muted(int muted);
/* Silences the output while the window is out of focus, apart from the player's own mute. */
int psx_host_audio_get_mute_in_background(void);
int psx_host_audio_set_mute_in_background(int enabled);

int psx_host_ui_handle_event(const SDL_Event *event);
int psx_host_ui_gl_visible(void);
void psx_host_ui_render_gl(int drawable_width, int drawable_height);

#ifdef __cplusplus
}
#endif

#endif /* PSX_HOST_UI_H */
