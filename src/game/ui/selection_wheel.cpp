#include "game/input/selection_wheel.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace disruptor::wheel {
namespace {

constexpr float kPi = 3.14159265358979323846f;

ImVec2 point(ImVec2 center, float radius, float angle) {
    return ImVec2(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
}

void centered(ImDrawList *draw, const char *text, ImVec2 at, float size, ImU32 color) {
    const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, 10000.0f, 0.0f, text);
    draw->AddText(ImGui::GetFont(), size, ImVec2(at.x - extent.x * 0.5f, at.y - extent.y * 0.5f), color, text);
}

} // namespace

void draw(int width, int height) {
    const View &wheel = view();
    if (wheel.kind == Kind::none || wheel.count == 0 || width <= 0 || height <= 0) return;
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const ImVec2 center(width * 0.5f, height * 0.5f);
    const float radius = std::min(width, height) * 0.34f;
    const float inner = radius * 0.43f;
    const float size = std::max(10.0f, radius / 240.0f * 19.0f);
    const float step = 2.0f * kPi / static_cast<float>(wheel.count);
    const ImU32 accent = wheel.kind == Kind::weapons ? IM_COL32(110, 231, 148, 255) : IM_COL32(123, 192, 255, 255);
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(static_cast<float>(width), static_cast<float>(height)), IM_COL32(0, 0, 0, 110));

    for (int i = 0; i < wheel.count; ++i) {
        const Entry &entry = wheel.entries[static_cast<size_t>(i)];
        const bool highlighted = i == wheel.highlighted;
        const float angle = -kPi * 0.5f + i * step;
        const float first = angle - step * 0.5f + 0.012f;
        const float last = angle + step * 0.5f - 0.012f;
        const ImU32 fill = !entry.owned ? IM_COL32(23, 27, 31, 230) : highlighted
            ? (wheel.kind == Kind::weapons ? IM_COL32(26, 86, 51, 246) : IM_COL32(27, 66, 108, 246))
            : IM_COL32(29, 36, 43, 242);
        const ImDrawListFlags saved_flags = draw->Flags;
        draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (int part = 0; part < 16; ++part) {
            const float a = first + (last - first) * part / 16.0f;
            const float b = first + (last - first) * (part + 1) / 16.0f;
            draw->AddQuadFilled(point(center, radius, a), point(center, radius, b),
                                point(center, inner, b), point(center, inner, a), fill);
        }
        draw->Flags = saved_flags;
        draw->PathArcTo(center, radius, first, last, 16);
        draw->PathStroke(highlighted ? accent : IM_COL32(72, 86, 95, 220), 0, highlighted ? 3.0f : 1.0f);
        const ImVec2 label = point(center, radius * 0.72f, angle);
        std::string name = entry.name;
        if (const auto space = name.find(' '); space != std::string::npos) name[space] = '\n';
        centered(draw, name.c_str(), ImVec2(label.x, label.y - size * 0.35f), size,
                 !entry.owned ? IM_COL32(103, 112, 121, 255) : IM_COL32(234, 241, 244, 255));
        char detail[40] = "";
        if (!entry.owned) std::snprintf(detail, sizeof detail, "Locked");
        else if (wheel.kind == Kind::weapons && entry.resource >= 0) std::snprintf(detail, sizeof detail, "%d ammo", entry.resource);
        else if (entry.equipped) std::snprintf(detail, sizeof detail, "Equipped");
        centered(draw, detail, ImVec2(label.x, label.y + size * 1.3f), size * 0.72f,
                 entry.equipped ? accent : IM_COL32(159, 172, 184, 255));
    }
    draw->AddCircleFilled(center, inner - 3.0f, IM_COL32(12, 18, 23, 247), 80);
    centered(draw, wheel.kind == Kind::weapons ? "WEAPONS" : "PSIONICS",
             ImVec2(center.x, center.y - size * 2.5f), size * 0.8f, accent);
    if (wheel.highlighted >= 0 && wheel.highlighted < wheel.count) {
        const Entry &entry = wheel.entries[static_cast<size_t>(wheel.highlighted)];
        centered(draw, entry.name, ImVec2(center.x, center.y), size * 1.05f, IM_COL32(241, 245, 249, 255));
        char detail[48] = "";
        if (!entry.owned) std::snprintf(detail, sizeof detail, "Not acquired");
        else if (wheel.kind == Kind::psionics) std::snprintf(detail, sizeof detail, "%d psionic energy", entry.resource);
        else std::snprintf(detail, sizeof detail, "%s", entry.equipped ? "Equipped" : "Release to equip");
        centered(draw, detail, ImVec2(center.x, center.y + size * 2), size * 0.75f,
                 entry.owned ? IM_COL32(171, 191, 201, 255) : IM_COL32(239, 153, 125, 255));
    }
    const ImVec2 cursor = point(center, radius * 0.94f, std::atan2(wheel.pointer_y, wheel.pointer_x));
    if (std::hypot(wheel.pointer_x, wheel.pointer_y) >= 0.25f) {
        draw->AddCircleFilled(cursor, size * 0.3f, accent, 16);
        draw->AddCircle(cursor, size * 0.5f, IM_COL32(240, 248, 252, 220), 16);
    }
    centered(draw, "Hold selector, point, release to equip", ImVec2(center.x, center.y + radius + size * 1.4f), size * 0.85f,
             IM_COL32(228, 236, 241, 255));
    centered(draw, "Mouse / left stick / arrows / D-pad   |   Escape / right-click to cancel",
             ImVec2(center.x, center.y + radius + size * 2.9f), size * 0.7f, IM_COL32(164, 178, 188, 255));
}

} // namespace disruptor::wheel
