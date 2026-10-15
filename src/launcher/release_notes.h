#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace disruptor::launcher {

struct ReleaseNotes {
    std::string version; // the section's heading, "v0.1.0-alpha.4"
    std::string markdown;
};

// The tag a build was made from: "v0.1.0-alpha.4-12-g1a2b3c4-dirty" is v0.1.0-alpha.4.
std::string release_tag(std::string_view version);
// CHANGELOG.md's section for `version`'s tag, or its newest section when it has none for it.
std::optional<ReleaseNotes> release_notes(std::string_view changelog, std::string_view version);

} // namespace disruptor::launcher
