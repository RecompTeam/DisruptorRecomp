#ifndef PSX_HOST_UI_FLAGS_H
#define PSX_HOST_UI_FLAGS_H

/* The host UI provider's flags (host_ui.h), and what they make of the frame interpolation. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum PsxHostUiFlags {
    /* Suppress the polled host input before it reaches SIO or a game mod. */
    PSX_HOST_UI_CAPTURE_KEYBOARD = 1u << 0,
    PSX_HOST_UI_CAPTURE_MOUSE    = 1u << 1,
    PSX_HOST_UI_CAPTURE_GAMEPAD  = 1u << 2,

    /* Stop high-refresh interpolation while the UI is open, so render_gl runs on the emulation thread's GL context. */
    PSX_HOST_UI_SUSPEND_INTERPOLATION = 1u << 3,

    /* The provider has visible GL output: defeats unchanged-frame and display-disabled present elision. */
    PSX_HOST_UI_VISIBLE = 1u << 4,

    /* The visible output only shows: render_gl draws it into a layer laid over each interpolated picture, so the
     * interpolation keeps running. Without interpolation it is drawn before SwapWindow as any other. */
    PSX_HOST_UI_LAYER = 1u << 5,
};

/* Whether the UI stops the interpolation: asked to, or visible outside the layer, since render_gl needs the main context. */
static inline int psx_host_ui_stops_interpolation(uint32_t flags) {
    return (flags & PSX_HOST_UI_SUSPEND_INTERPOLATION) != 0 ||
           (flags & (PSX_HOST_UI_VISIBLE | PSX_HOST_UI_LAYER)) == PSX_HOST_UI_VISIBLE;
}

/* Whether the UI is drawn into the layer over interpolated pictures. */
static inline int psx_host_ui_in_layer(uint32_t flags) {
    return !psx_host_ui_stops_interpolation(flags) && (flags & PSX_HOST_UI_VISIBLE) != 0;
}

#ifdef __cplusplus
}
#endif

#endif
