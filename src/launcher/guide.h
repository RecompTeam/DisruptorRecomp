#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::launcher {

// Markdown as the player's guide and release notes write it, read into what the launcher draws.
enum class Style { plain, bold, code, link };

struct Span {
    Style style = Style::plain;
    std::string text;
    std::string target; // a link's address: a web page, or "#anchor" for a section of the guide
};
using Line = std::vector<Span>;

enum class BlockKind { heading, paragraph, bullet, numbered, table, code, rule };

struct Block {
    BlockKind kind = BlockKind::paragraph;
    int level = 0; // a heading's level, a numbered item's number
    int depth = 0; // how far a list item is nested
    Line text;
    std::vector<std::vector<Line>> rows; // a table's cells, its head first
    std::string code;                    // a fenced block as written
};

Line parse_inline(std::string_view text);
std::vector<Block> parse_markdown(std::string_view markdown);
std::string plain_text(const Line& line);
// The anchor GitHub gives a heading: lower case, punctuation dropped, spaces as hyphens.
std::string anchor_of(std::string_view heading);

struct Section {
    std::string title, anchor;
    std::vector<Block> blocks;
};
struct Guide {
    std::string title;
    std::vector<Block> intro; // what stands between the title and the first section
    std::vector<Section> sections;
};
// Sections are the second-level headings. Deeper headings stay inside them.
Guide guide_of(std::string_view markdown);
// The section a "#anchor" link names, or -1.
int section_of(const Guide& guide, std::string_view target);

// Where each piece of a line goes when it is wrapped to `width`. Pieces with no space between them move to the next
// row together, and a piece wider than a row is cut at code points.
struct Placed {
    std::size_t span = 0;
    std::string text;
    float x = 0.0f;
    float width = 0.0f;
    int row = 0;
};
std::vector<Placed> lay_out(const Line& line, float width, const std::function<float(const std::string&, Style)>& measure);

} // namespace disruptor::launcher
