#include "host_ui_flags.h"

#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr std::uint32_t kCapture =
    PSX_HOST_UI_CAPTURE_KEYBOARD | PSX_HOST_UI_CAPTURE_MOUSE | PSX_HOST_UI_CAPTURE_GAMEPAD;

}  // namespace

int main() {
    constexpr std::uint32_t kCard = PSX_HOST_UI_VISIBLE | PSX_HOST_UI_LAYER;
    expect(!psx_host_ui_stops_interpolation(kCard) && psx_host_ui_in_layer(kCard),
           "a card that only shows goes into the layer and leaves the interpolation running");
    expect(psx_host_ui_stops_interpolation(kCapture | PSX_HOST_UI_VISIBLE) &&
               !psx_host_ui_in_layer(kCapture | PSX_HOST_UI_VISIBLE),
           "the open menu stops the interpolation and is drawn before the swap");
    expect(psx_host_ui_stops_interpolation(PSX_HOST_UI_VISIBLE) && !psx_host_ui_in_layer(PSX_HOST_UI_VISIBLE),
           "visible UI that names no layer stops it, as before the layer");
    expect(psx_host_ui_stops_interpolation(kCard | PSX_HOST_UI_SUSPEND_INTERPOLATION) &&
               !psx_host_ui_in_layer(kCard | PSX_HOST_UI_SUSPEND_INTERPOLATION),
           "a provider's suspension wins over its layer");
    expect(!psx_host_ui_stops_interpolation(0u) && !psx_host_ui_in_layer(0u), "no UI, nothing to do");
    expect(!psx_host_ui_stops_interpolation(PSX_HOST_UI_LAYER) && !psx_host_ui_in_layer(PSX_HOST_UI_LAYER),
           "a layer with nothing visible draws nothing");

    for (std::uint32_t flags = 0; flags < 64u; ++flags) {
        const bool visible = (flags & PSX_HOST_UI_VISIBLE) != 0;
        const bool stops = psx_host_ui_stops_interpolation(flags) != 0;
        const bool layer = psx_host_ui_in_layer(flags) != 0;
        expect(!(stops && layer), "the layer is only drawn while the interpolation runs");
        expect(!visible || stops || layer, "visible UI is drawn one way or the other");
        expect(stops == (psx_host_ui_stops_interpolation(flags & ~kCapture) != 0) &&
                   layer == (psx_host_ui_in_layer(flags & ~kCapture) != 0),
               "capturing input changes neither");
        if ((flags & PSX_HOST_UI_SUSPEND_INTERPOLATION) != 0) expect(stops, "asked to stop, it stops");
    }

    if (g_failures != 0) return 1;
    std::cout << "host UI flags tests passed\n";
    return 0;
}
