#include "game/ui/menu/menu.h"

namespace disruptor::menu {
namespace {
constexpr const char *driver_words[] = {"off", "on", "from the next launch", "not offered by this driver"};

#if DISRUPTOR_DEV_TOOLS
void draw_mission_select() {
    static int mission = DISRUPTOR_FIRST_MISSION;
    static int hard = 0;
    ImGui::TextUnformatted("Start a mission");
    draw_status_badge("DEV BUILD", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("##mission", &mission, DISRUPTOR_FIRST_MISSION, DISRUPTOR_LAST_MISSION, "Mission %d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::Combo("##difficulty", &hard, "Regular\0Hard\0");
    ImGui::SameLine();
    const bool ready = disruptor_mission_select_ready() != 0;
    if (!ready) ImGui::BeginDisabled();
    if (ImGui::Button("Start") &&
        disruptor_mission_select_start(mission, hard ? DISRUPTOR_DIFFICULTY_HARD : DISRUPTOR_DIFFICULTY_REGULAR))
        set_menu_open(false);
    if (!ready) ImGui::EndDisabled();
    ImGui::TextDisabled("Works from the game's main menu\nStarts as the mission's password would\nNo carried weapons or flags");
}
#endif

std::string local_time(int64_t second) {
    const std::time_t when = static_cast<std::time_t>(second);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &when) != 0) return "?";
#else
    if (!localtime_r(&when, &local)) return "?";
#endif
    char text[32] = {};
    return std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local) ? text : "?";
}

unsigned load_picture(const fs::path &file, float *tall) {
    const std::string bytes = disruptor::capsule::preview_read(file);
    uint32_t width = 0, height = 0;
    std::string_view rgb;
    if (!disruptor::capsule::preview_decode(bytes, width, height, rgb)) return 0;
    if (tall) *tall = static_cast<float>(height) / static_cast<float>(width);
    return gl_renderer_picture(reinterpret_cast<const unsigned char *>(rgb.data()), static_cast<int>(width), static_cast<int>(height));
}

/* Under the mouse a capsule's pictures play in order, four a second: they were taken two a second. */
void drop_film() {
    for (const unsigned picture : g_menu.capsule_film) gl_renderer_picture_free(picture);
    g_menu.capsule_film.clear();
    g_menu.capsule_film_of.clear();
}

void draw_film(const fs::path &folder, const disruptor::capsule::Stored &kept, float tall) {
    if (g_menu.capsule_film_of != kept.name || g_menu.capsule_film_pictures != kept.pictures || g_menu.capsule_film_revised != kept.revised) {
        drop_film();
        g_menu.capsule_film_of = kept.name;
        g_menu.capsule_film_pictures = kept.pictures;
        g_menu.capsule_film_revised = kept.revised;
    }
    for (int loaded = 0; loaded < 4 && g_menu.capsule_film.size() < kept.pictures; ++loaded)
        g_menu.capsule_film.push_back(load_picture(folder / disruptor::capsule::preview_name(static_cast<uint32_t>(g_menu.capsule_film.size())), nullptr));
    if (g_menu.capsule_film.empty()) return;
    const unsigned picture = g_menu.capsule_film[static_cast<size_t>(ImGui::GetTime() * 4.0) % g_menu.capsule_film.size()];
    ImGui::BeginTooltip();
    if (picture) ImGui::Image(static_cast<ImTextureID>(picture), ImVec2(480.0f, 480.0f * tall));
    ImGui::EndTooltip();
}

void draw_capsules() {
    namespace capsule = disruptor::capsule;
    const fs::path all = capsule::home();
    if (g_menu.capsules_read < 0.0 || ImGui::GetTime() - g_menu.capsules_read > 1.0) {
        g_menu.capsules = capsule::stored(all);
        g_menu.capsules_read = ImGui::GetTime();
        for (auto thumb = g_menu.capsule_thumbs.begin(); thumb != g_menu.capsule_thumbs.end();) {
            const bool listed = std::any_of(g_menu.capsules.begin(), g_menu.capsules.end(), [&](const capsule::Stored &kept) { return kept.name == thumb->first; });
            if (!listed) gl_renderer_picture_free(thumb->second.picture);
            thumb = listed ? std::next(thumb) : g_menu.capsule_thumbs.erase(thumb);
        }
        if (std::none_of(g_menu.capsules.begin(), g_menu.capsules.end(), [](const capsule::Stored &kept) { return kept.name == g_menu.capsule_film_of; })) drop_film();
    }
    ImGui::TextUnformatted("Replay capsules");
    ImGui::TextDisabled("Ctrl+F12 starts a capsule and ends it, F12 loads the newest\nEach is kept in a folder of its own: the newest is on top");
    ImGui::TextDisabled("The picture is a capsule's last\nHold the mouse over it to see the capsule play");
    if (g_menu.capsules.empty()) {
        ImGui::TextDisabled("None recorded yet");
        return;
    }
    if (ImGui::Button("Open the folder")) {
#if defined(PSX_SDL3)
        const bool opened = SDL_OpenURL(capsule::folder_url(all).c_str());
#else
        const bool opened = SDL_OpenURL(capsule::folder_url(all).c_str()) == 0;
#endif
        g_menu.capsule_note = opened ? std::string() : "The folder could not be opened";
    }
    if (!g_menu.capsule_note.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", g_menu.capsule_note.c_str());
    }
    const std::string recording = capsule::recording();
    const float rows = static_cast<float>(std::min<size_t>(g_menu.capsules.size(), 5)) + 0.5f, wide = 72.0f;
    if (!ImGui::BeginTable("##capsules", 5, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit,
                           ImVec2(0.0f, (wide * 0.5625f + ImGui::GetStyle().CellPadding.y * 2.0f) * rows)))
        return;
    for (const capsule::Stored &kept : g_menu.capsules) {
        const bool asked = g_menu.capsule_asked == kept.name;
        DevMenuState::CapsuleThumb &thumb = g_menu.capsule_thumbs[kept.name];
        if (thumb.pictures != kept.pictures || thumb.revised != kept.revised) {
            gl_renderer_picture_free(thumb.picture);
            thumb.picture = kept.pictures ? load_picture(all / kept.name / capsule::preview_name(kept.pictures - 1), &thumb.tall) : 0;
            thumb.pictures = kept.pictures;
            thumb.revised = kept.revised;
        }
        ImGui::TableNextRow();
        ImGui::PushID(kept.name.c_str());
        ImGui::TableNextColumn();
        if (thumb.picture) ImGui::Image(static_cast<ImTextureID>(thumb.picture), ImVec2(wide, wide * thumb.tall));
        else ImGui::Dummy(ImVec2(wide, wide * 0.5625f));
        if (thumb.picture && ImGui::IsItemHovered()) draw_film(all / kept.name, kept, thumb.tall);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(local_time(kept.began).c_str());
        ImGui::TableNextColumn();
        if (kept.name == recording) ImGui::TextUnformatted("recording");
        else if (kept.frames == 0) ImGui::TextDisabled("unfinished");
        else ImGui::Text("%u s", capsule::stored_seconds(kept.frames));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(kept.name.c_str());
        ImGui::TableNextColumn();
        if (kept.name != recording && kept.frames != 0) {
            if (ImGui::SmallButton("Load")) {
                if (capsule::load(kept.name)) set_menu_open(false);
                else g_menu.capsule_note = "Could not load " + kept.name;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Puts the game where this recording began");
            ImGui::SameLine();
        }
        if (kept.name != recording && ImGui::SmallButton(asked ? "Delete for good?" : "Delete")) {
            if (asked) {
                g_menu.capsule_note = (capsule::discard(all, kept.name) ? "Deleted " : "Could not delete ") + kept.name;
                g_menu.capsules_read = -1.0;
            }
            g_menu.capsule_asked = asked ? std::string() : kept.name;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}
}  // namespace

void draw_debug_tab() {
    ImGui::TextDisabled("Tools for a report of a fault");
    ImGui::Spacing();

    bool detailed = disruptor_diagnostics_detailed() != 0;
    if (option_toggle("disruptor.detailed_logging", &detailed)) {
        disruptor_diagnostics_set_detailed(detailed ? 1 : 0);
        mark_detailed_logging(detailed);
    }
    if (detailed) ImGui::TextDisabled("Driver messages: %s", driver_words[disruptor_diagnostics_driver_state()]);

#if DISRUPTOR_DEV_TOOLS
    ImGui::Separator();
    if (ImGui::Button("Record the screen") && !gl_renderer_recording_shown()) g_menu.record_shown = true;
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "For a report: two seconds of the pictures that reach the screen, with "
        "their times\nIt starts a second after this menu closes");
    if (g_menu.record_shown) ImGui::TextDisabled("Armed\nClose the menu and do what you want to show");
    else if (!g_menu.shown_note.empty()) ImGui::TextDisabled("%s", g_menu.shown_note.c_str());

    ImGui::Separator();
    int view = gl_renderer_uv_view() ? 6 : gl_renderer_mesh_view() ? 5 : gl_renderer_ssao_view();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Diagnostic view", &view, "Off\0Shading alone\0Depth\0Surface normals\0Fog fade\0Mesh\0Texture coordinates\0")) {
        apply_diagnostic_view(view);
        mark_diagnostic_view(view);
    }
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Shows a layer of the renderer in the picture's place, for reports\nA "
        "replay capsule recorded with one on replays in it\nOff again at the "
        "next start");

    ImGui::Separator();
    bool probes = gpu_ddgi_view() != 0;
    if (ImGui::Checkbox("Show the probes", &probes)) gpu_ddgi_set_view(probes ? 1 : 0);
    ImGui::TextDisabled("Dots at fixed places in the world, every 400 units\nThey must stay put while you walk and turn");
    GpuDdgiStats probe_stats{};
    gpu_ddgi_stats(&probe_stats);
    if (probes || gl_renderer_ddgi() || gl_renderer_ddgi_shadows() || gl_renderer_ddgi_lamps())
        ImGui::TextDisabled("World triangles in the last frame: %d", probe_stats.triangles);

    ImGui::Separator();
    ImGui::TextDisabled("Vertical pitch asked for: %+.2f degrees, at most 30.94 either way",
                        disruptor_mouse_vertical_pitch() * (360.0 / 256.0));
    ImGui::TextDisabled("Mouse captured before this menu opened: %s\nThe menu releases it while open and gives it "
                        "back when closed", g_menu.restore_mouse_capture ? "yes" : "no");

    ImGui::Separator();
    draw_mission_select();
#endif

    ImGui::Separator();
    draw_capsules();
}

}  // namespace disruptor::menu
