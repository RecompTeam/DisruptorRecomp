#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace disruptor::crosshair {

enum class Shape : int { dot = 0, plus = 1, ring = 2 };

inline constexpr int kShapeCount = 3;
inline constexpr int kColourCount = 5;
inline constexpr int kSmallest = 50, kLargest = 300;  /* size in percent of the plain dot */
inline constexpr int kFaintest = 20, kOpaque = 100;    /* opacity in percent */

struct Rgba {
    std::uint8_t r, g, b, a;
};

struct Style {
    int shape = 0;
    int size = 100;
    int colour = 0;
    int opacity = 90;
};

/* White, green, red, yellow, cyan: the launcher's list, in its order. */
inline constexpr std::array<Rgba, kColourCount> kColours{{
    {255, 255, 255, 255}, {80, 255, 80, 255}, {255, 70, 60, 255}, {255, 230, 60, 255}, {70, 230, 255, 255}}};

/* A value outside its range takes the default, as a hand-edited file may hold one. */
[[nodiscard]] inline Style clamped(Style style) {
    if (style.shape < 0 || style.shape >= kShapeCount) style.shape = 0;
    if (style.colour < 0 || style.colour >= kColourCount) style.colour = 0;
    style.size = std::clamp(style.size, kSmallest, kLargest);
    style.opacity = std::clamp(style.opacity, kFaintest, kOpaque);
    return style;
}

/* The dot's radius in pixels: 3 at 1080 rows, never under 1.5. */
[[nodiscard]] inline float radius(int height, int size_percent) {
    const float plain = std::max(2.0f, std::round(static_cast<float>(height) / 360.0f));
    return std::max(1.5f, plain * static_cast<float>(std::clamp(size_percent, kSmallest, kLargest)) / 100.0f);
}

[[nodiscard]] inline Rgba fill(const Style& style) {
    const Style kept = clamped(style);
    Rgba colour = kColours[static_cast<std::size_t>(kept.colour)];
    colour.a = static_cast<std::uint8_t>(std::lround(255.0 * kept.opacity / 100.0));
    return colour;
}

/* The dark rim under the fill, a little fainter than it. */
[[nodiscard]] inline Rgba rim(const Style& style) {
    return Rgba{0, 0, 0, static_cast<std::uint8_t>(fill(style).a * 2 / 3)};
}

}  // namespace disruptor::crosshair
