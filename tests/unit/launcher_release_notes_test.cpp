#include "release_notes.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using disruptor::launcher::release_notes;
using disruptor::launcher::release_tag;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr const char* changelog = "# Changelog\r\n\r\nWhat changed.\r\n\r\n## v0.1.0-alpha.5\r\n\r\n- Five **new**.\r\n- More.\r\n\r\n"
                                  "## v0.1.0-alpha.4\n\n- Four.\n\n## v0.1.0-alpha.1\n\nFirst.\n";

void tags() {
    require(release_tag("v0.1.0-alpha.4") == "v0.1.0-alpha.4", "a tag is its own tag");
    require(release_tag("v0.1.0-alpha.4-12-g1a2b3c4") == "v0.1.0-alpha.4", "a build past a tag is that tag's");
    require(release_tag("v0.1.0-alpha.4-12-g1a2b3c4-dirty") == "v0.1.0-alpha.4", "a build with changes too");
    require(release_tag(" v0.1.0-alpha.4-dirty ") == "v0.1.0-alpha.4", "a tag with changes is the tag");
    require(release_tag("v1.0-gold") == "v1.0-gold" && release_tag("v1.0-rc-gamma") == "v1.0-rc-gamma" &&
                release_tag("v1.0-2-gold") == "v1.0-2-gold" && release_tag("v1.0-2-g") == "v1.0-2-g",
            "a tag that only holds -g, or a count before a word, is not cut");
}

void sections() {
    const auto five = release_notes(changelog, "v0.1.0-alpha.5");
    require(five && five->version == "v0.1.0-alpha.5" && five->markdown == "- Five **new**.\n- More.",
            "a version's section is its lines up to the next, line ends of either kind");
    const auto four = release_notes(changelog, "v0.1.0-alpha.4-37-gabcdef0");
    require(four && four->version == "v0.1.0-alpha.4" && four->markdown == "- Four.", "a build past a tag shows that tag's notes");
    const auto first = release_notes(changelog, "v0.1.0-alpha.1");
    require(first && first->markdown == "First.", "the last section runs to the end");
    const auto newer = release_notes(changelog, "v0.1.0-alpha.6");
    require(newer && newer->version == "v0.1.0-alpha.5", "a version the changelog lacks shows the newest, named as it is");
    require(!release_notes("# Changelog\n\nNothing yet.\n", "v0.1.0-alpha.5") && !release_notes("", "x"),
            "a changelog without sections has no notes");
}

void shipped() {
    std::ifstream in(std::filesystem::path(DISRUPTOR_SOURCE_DIR) / "CHANGELOG.md", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const auto newest = release_notes(text, "unknown");
    require(newest && text.find("## " + newest->version) == text.find("## "), "a version it lacks gets the shipped changelog's first section");
    for (const char* tag : {"v0.1.0-alpha.1", "v0.1.0-alpha.2", "v0.1.0-alpha.3", "v0.1.0-alpha.4", "v0.1.0-alpha.5"}) {
        const auto section = release_notes(text, std::string(tag) + "-3-g0123abc");
        require(section && section->version == tag && section->markdown.size() > 80 && section->markdown.find("## ") == std::string::npos,
                "every release so far has its own section in the shipped changelog");
    }
}

} // namespace

int main() {
    try {
        tags();
        sections();
        shipped();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "launcher release notes: PASS\n";
    return 0;
}
