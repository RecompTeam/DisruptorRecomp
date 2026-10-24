#pragma once

#include <string>
#include <string_view>

namespace disruptor::achievements {

// RetroAchievements tells clients apart by this name alone, so it never changes.
inline constexpr std::string_view kClientName = "DisruptorRecompiled";

// "DisruptorRecompiled/0.7.0 (Windows 10.0.26200) rcheevos/12.5": numbers only in the version, as the server asks.
[[nodiscard]] std::string user_agent(std::string_view version, std::string_view system);
// This build's: its release version, or the last release tag for a build between releases.
[[nodiscard]] std::string user_agent();
// "1.2.3" from "v1.2.3-alpha.1" or "1.2.3", "0.0.0" when the text starts with no number.
[[nodiscard]] std::string numeric_version(std::string_view text);

} // namespace disruptor::achievements
