#include "game/ui/menu/menu.h"

namespace disruptor::menu {
namespace {
const ImVec4 kExperimental(1.0f, 0.73f, 0.25f, 1.0f);

// Applied at once, saved as the renderer took it when the menu closes.
void set_switch(void (*apply)(int), bool value) {
    apply(value ? 1 : 0);
    mark_lighting();
}

void set_amount(void (*apply)(float), int percent) {
    apply(percent / 100.0f);
    mark_lighting();
}

int percent(float amount) { return static_cast<int>(std::lround(amount * 100.0f)); }

void draw_lighting() {
    bool lighting = gl_renderer_ddgi() != 0;
    const bool lighting_applies = applies("disruptor.probe_lighting");
    if (!lighting_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.probe_lighting", &lighting)) set_switch(gl_renderer_set_ddgi, lighting);
    if (!lighting_applies) ImGui::EndDisabled();
    draw_status_badge("EXPERIMENTAL", kExperimental);
    ImGui::Indent();
    const bool probes_apply =
        applies("disruptor.probe_strength") && applies("disruptor.probe_bounce") && applies("disruptor.probe_glow");
    if (!probes_apply) ImGui::BeginDisabled();
    int strength = percent(gl_renderer_ddgi_strength());
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.probe_strength", &strength)) set_amount(gl_renderer_set_ddgi_strength, strength);
    int bounce = percent(gl_renderer_ddgi_bounce());
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.probe_bounce", &bounce)) set_amount(gl_renderer_set_ddgi_bounce, bounce);
    int glow = percent(gl_renderer_ddgi_glow());
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.probe_glow", &glow)) set_amount(gl_renderer_set_ddgi_glow, glow);
    if (!probes_apply) ImGui::EndDisabled();
    if (lighting) {
        bool alone = gl_renderer_ddgi_view() != 0;
        if (ImGui::Checkbox("Probe light alone", &alone)) gl_renderer_set_ddgi_view(alone ? 1 : 0);
        ImGui::TextDisabled("Grey is the average light, brighter and darker around it");
    }
    ImGui::Unindent();

    bool shadows = gl_renderer_ddgi_shadows() != 0;
    const bool shadows_apply = applies("disruptor.dynamic_shadows");
    if (!shadows_apply) ImGui::BeginDisabled();
    if (option_toggle("disruptor.dynamic_shadows", &shadows)) set_switch(gl_renderer_set_ddgi_shadows, shadows);
    if (!shadows_apply) ImGui::EndDisabled();
    draw_status_badge("EXPERIMENTAL", kExperimental);
    ImGui::Indent();
    const bool shadow_amounts_apply = applies("disruptor.shadow_strength") && applies("disruptor.shadow_underneath");
    if (!shadow_amounts_apply) ImGui::BeginDisabled();
    int darkness = percent(gl_renderer_ddgi_shadow_strength());
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.shadow_strength", &darkness)) set_amount(gl_renderer_set_ddgi_shadow_strength, darkness);
    int underneath = percent(gl_renderer_ddgi_contact());
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.shadow_underneath", &underneath)) set_amount(gl_renderer_set_ddgi_contact, underneath);
    if (!shadow_amounts_apply) ImGui::EndDisabled();
    ImGui::Unindent();

    bool lamps = gl_renderer_ddgi_lamps() != 0;
    const bool lamps_apply = applies("disruptor.shot_lights");
    if (!lamps_apply) ImGui::BeginDisabled();
    if (option_toggle("disruptor.shot_lights", &lamps)) set_switch(gl_renderer_set_ddgi_lamps, lamps);
    if (!lamps_apply) ImGui::EndDisabled();
    draw_status_badge("EXPERIMENTAL", kExperimental);
    ImGui::Indent();
    const bool brightness_applies = applies("disruptor.shot_light_strength");
    if (!brightness_applies) ImGui::BeginDisabled();
    int brightness = percent(gl_renderer_ddgi_lamp_strength());
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.shot_light_strength", &brightness)) set_amount(gl_renderer_set_ddgi_lamp_strength, brightness);
    if (!brightness_applies) ImGui::EndDisabled();
    ImGui::Unindent();

    bool sun = gl_renderer_ddgi_sun() != 0;
    const bool sun_applies = applies("disruptor.sun_lighting");
    if (!sun_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.sun_lighting", &sun)) set_switch(gl_renderer_set_ddgi_sun, sun);
    if (!sun_applies) ImGui::EndDisabled();
    draw_status_badge("EXPERIMENTAL", kExperimental);
    ImGui::Indent();
    // Another mission's placement would be saved as the second mission's.
    if (!gl_renderer_ddgi_sun_here()) {
        ImGui::TextDisabled("No sun on this mission");
    } else {
        float place[4];
        gl_renderer_ddgi_sun_get(place);
        int amounts[4] = {static_cast<int>(std::lround(place[0])), static_cast<int>(std::lround(place[1])), percent(place[2]),
                          percent(place[3])};
        const bool placement_applies = applies("disruptor.sun_direction") && applies("disruptor.sun_height") &&
                                       applies("disruptor.sun_strength") && applies("disruptor.sun_shade");
        if (!placement_applies) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(260.0f);
        const bool direction_moved = option_slider("disruptor.sun_direction", &amounts[0]);
        ImGui::SetNextItemWidth(260.0f);
        const bool height_moved = option_slider("disruptor.sun_height", &amounts[1]);
        ImGui::SetNextItemWidth(260.0f);
        const bool strength_moved = option_slider("disruptor.sun_strength", &amounts[2]);
        ImGui::SetNextItemWidth(260.0f);
        const bool shade_moved = option_slider("disruptor.sun_shade", &amounts[3]);
        if (!placement_applies) ImGui::EndDisabled();
        if (direction_moved || height_moved || strength_moved || shade_moved) {
            const float sun_now[4] = {static_cast<float>(amounts[0]), static_cast<float>(amounts[1]), amounts[2] / 100.0f,
                                      amounts[3] / 100.0f};
            gl_renderer_ddgi_sun_set(sun_now);
            mark_sun(amounts);
        }
    }
    ImGui::Unindent();
    if (shadows || lamps || sun) {
        bool alone = gl_renderer_ddgi_lamp_view() != 0;
        if (ImGui::Checkbox("Shadows and light alone", &alone)) gl_renderer_set_ddgi_lamp_view(alone ? 1 : 0);
        ImGui::TextDisabled("Grey is no shadow, darker is shadow, colour is added light");
    }
}
}  // namespace

void draw_experimental_tab() {
    draw_lighting();
}

}  // namespace disruptor::menu
