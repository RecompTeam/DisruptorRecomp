#pragma once

#include "game/ui/crosshair_style.h"

#include "imgui.h"

#include <algorithm>
#include <utility>

namespace disruptor::crosshair {

/* The crosshair at `middle` over a picture `height` pixels tall, as the game draws it; `fade` dims it for a greyed setting. */
inline void draw(ImDrawList* list, ImVec2 middle, int height, const Style& asked, float fade = 1.0f) {
    const Style style = clamped(asked);
    const float dot = radius(height, style.size);
    const auto pen = [fade](Rgba colour) { return IM_COL32(colour.r, colour.g, colour.b, static_cast<int>(colour.a * fade)); };
    const ImU32 inner = pen(fill(style)), outer = pen(rim(style));
    switch (static_cast<Shape>(style.shape)) {
    case Shape::plus: {
        const float gap = dot, arm = dot * 3.0f, thick = std::max(1.5f, dot * 0.6f);
        for (const auto& [colour, width] : {std::pair{outer, thick + 2.0f}, std::pair{inner, thick}}) {
            list->AddLine(ImVec2(middle.x - gap - arm, middle.y), ImVec2(middle.x - gap, middle.y), colour, width);
            list->AddLine(ImVec2(middle.x + gap, middle.y), ImVec2(middle.x + gap + arm, middle.y), colour, width);
            list->AddLine(ImVec2(middle.x, middle.y - gap - arm), ImVec2(middle.x, middle.y - gap), colour, width);
            list->AddLine(ImVec2(middle.x, middle.y + gap), ImVec2(middle.x, middle.y + gap + arm), colour, width);
        }
        break;
    }
    case Shape::ring: {
        const float ring = dot * 2.5f, thick = std::max(1.5f, dot * 0.5f);
        list->AddCircle(middle, ring, outer, 0, thick + 2.0f);
        list->AddCircle(middle, ring, inner, 0, thick);
        break;
    }
    case Shape::dot:
        list->AddCircleFilled(middle, dot + 1.0f, outer);
        list->AddCircleFilled(middle, dot, inner);
        break;
    }
}

}  // namespace disruptor::crosshair
