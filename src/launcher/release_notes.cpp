#include "release_notes.h"

#include "ini_lines.h"

#include <cctype>
#include <vector>

namespace disruptor::launcher {

std::string release_tag(std::string_view version) {
    std::string tag = trimmed(version);
    if (tag.ends_with("-dirty")) tag.resize(tag.size() - 6);
    // git describe appends "-<commits>-g<hash>" past the tag.
    const std::size_t hash = tag.rfind("-g");
    if (hash == std::string::npos || hash == 0 || hash + 2 == tag.size()) return tag;
    for (std::size_t at = hash + 2; at < tag.size(); ++at)
        if (!std::isxdigit(static_cast<unsigned char>(tag[at]))) return tag;
    const std::size_t count = tag.rfind('-', hash - 1);
    if (count == std::string::npos || count + 1 == hash) return tag;
    for (std::size_t at = count + 1; at < hash; ++at)
        if (!std::isdigit(static_cast<unsigned char>(tag[at]))) return tag;
    tag.resize(count);
    return tag;
}

std::optional<ReleaseNotes> release_notes(std::string_view changelog, std::string_view version) {
    std::vector<ReleaseNotes> sections;
    std::size_t start = 0;
    while (start <= changelog.size()) {
        std::size_t end = changelog.find('\n', start);
        if (end == std::string_view::npos) end = changelog.size();
        std::string_view line = changelog.substr(start, end - start);
        if (line.ends_with('\r')) line.remove_suffix(1);
        if (line.starts_with("## ")) {
            sections.push_back({trimmed(line.substr(3)), {}});
        } else if (!sections.empty()) {
            sections.back().markdown += std::string(line) + "\n";
        }
        start = end + 1;
    }
    for (ReleaseNotes& section : sections) {
        const std::size_t first = section.markdown.find_first_not_of(" \t\r\n");
        section.markdown = first == std::string::npos
            ? std::string()
            : section.markdown.substr(first, section.markdown.find_last_not_of(" \t\r\n") - first + 1);
    }
    if (sections.empty()) return std::nullopt;
    const std::string tag = release_tag(version);
    for (const ReleaseNotes& section : sections)
        if (section.version == tag) return section;
    return sections.front();
}

} // namespace disruptor::launcher
