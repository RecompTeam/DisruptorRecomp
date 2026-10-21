#include "guide.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace disruptor::launcher;
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool spans(const Line& line, std::initializer_list<Span> expected) {
    if (line.size() != expected.size()) return false;
    auto wanted = expected.begin();
    for (const Span& span : line) {
        if (span.style != wanted->style || span.text != wanted->text || span.target != wanted->target) return false;
        ++wanted;
    }
    return true;
}

void test_lead_in() {
    const auto list = parse_markdown("- Draw distance: 1.25x is offered again\n- Plain item, no name: here\n1. Fog: one\n\nWord: no list");
    require(list.size() == 4 && spans(list[0].text, {{Style::lead, "Draw distance:"}, {Style::plain, " 1.25x is offered again"}}),
            "a list item's name before its colon is its lead");
    require(spans(list[1].text, {{Style::plain, "Plain item, no name: here"}}), "a comma before the colon makes no lead");
    require(spans(list[2].text, {{Style::lead, "Fog:"}, {Style::plain, " one"}}), "a numbered item has one too");
    require(spans(list[3].text, {{Style::plain, "Word: no list"}}), "a paragraph has none");
    const auto marked = parse_markdown("- **Bold**: stays bold");
    require(spans(marked[0].text, {{Style::bold, "Bold"}, {Style::plain, ": stays bold"}}), "an item that marks its start keeps its marks");
}

void test_inline_marks() {
    require(spans(parse_inline("Press **Play** now"), {{Style::plain, "Press "}, {Style::bold, "Play"}, {Style::plain, " now"}}),
            "bold is read and its marks are left out");
    require(spans(parse_inline("Backquote (`` ` ``); Escape"), {{Style::plain, "Backquote ("}, {Style::code, "`"}, {Style::plain, "); Escape"}}),
            "a code span of two backticks holds a backtick, its padding spaces taken off");
    require(spans(parse_inline("A `**not bold**` span"), {{Style::plain, "A "}, {Style::code, "**not bold**"}, {Style::plain, " span"}}),
            "marks inside a code span stay as written");
    require(spans(parse_inline("see [Where your files are](#where-your-files-are)."),
                  {{Style::plain, "see "}, {Style::link, "Where your files are", "#where-your-files-are"}, {Style::plain, "."}}),
            "a link keeps its text and its target");
    require(spans(parse_inline("[**Bold** link](https://example.com \"title\")"), {{Style::link, "Bold link", "https://example.com"}}),
            "a link's text loses its marks and its target its title");
    require(spans(parse_inline("<https://example.com/a>"), {{Style::link, "https://example.com/a", "https://example.com/a"}}),
            "an address in angle brackets is a link");
    require(spans(parse_inline("![A picture](images/a.png)"), {{Style::link, "A picture", "images/a.png"}}),
            "a picture is a link to it");
    require(spans(parse_inline("an unclosed **bold and `tick"), {{Style::plain, "an unclosed **bold and `tick"}}),
            "a mark nothing closes is plain text");
    require(spans(parse_inline("\\*not\\* emphasis, a \\| pipe"), {{Style::plain, "*not* emphasis, a | pipe"}}),
            "an escaped mark is the mark itself");
    require(spans(parse_inline("C:\\Users\\Player"), {{Style::plain, "C:\\Users\\Player"}}),
            "a backslash before a letter stays");
    require(spans(parse_inline("[a] b (c)"), {{Style::plain, "[a] b (c)"}}), "brackets that make no link stay");
    require(spans(parse_inline("see [`startup.log`](#logs)"), {{Style::plain, "see "}, {Style::code, "startup.log", "#logs"}}),
            "code inside a link keeps the link's target");
    require(spans(parse_inline("[manual](https://example.com/a_(b)) and [x](https://e.com/a\\)b)"),
                  {{Style::link, "manual", "https://example.com/a_(b)"}, {Style::plain, " and "}, {Style::link, "x", "https://e.com/a)b"}}),
            "an address keeps its paired and its escaped parentheses");
}

void test_markup_lines() {
    const auto both = parse_markdown("<https://example.com/a> <https://example.com/b>");
    require(both.size() == 1 && spans(both[0].text, {{Style::link, "https://example.com/a", "https://example.com/a"},
                                                     {Style::plain, " "},
                                                     {Style::link, "https://example.com/b", "https://example.com/b"}}),
            "a line of two addresses in angle brackets is a paragraph of two links");
    const auto tagged = parse_markdown("<b>Keep this warning</b>");
    require(tagged.size() == 1 && plain_text(tagged[0].text).find("Keep this warning") != std::string::npos,
            "a line with text between tags keeps its text");
    const auto comment = parse_markdown("Before\n<!-- one\ntwo -->\nAfter\n<details>\n<br>");
    require(comment.size() == 2 && plain_text(comment[0].text) == "Before" && plain_text(comment[1].text) == "After",
            "a comment over several lines and a line of one tag are left out");
}

void test_blocks() {
    const auto blocks = parse_markdown("# Title\n\nOne line\nwrapped on.\r\n\n1. First\n   item.\n2. Second\n\n- **Lead:** text\n  more\n"
                                       "  - nested\n\n| Action | Key |\n| --- | :---: |\n| Pipe | `a|b` |\n| Short |\n\n"
                                       "```\n**kept** as is\n```\n\n---\n<!-- a note -->\n### Deeper ##\n");
    require(blocks.size() == 10, "every block is read");
    require(blocks[0].kind == BlockKind::heading && blocks[0].level == 1 && plain_text(blocks[0].text) == "Title", "a heading");
    require(blocks[1].kind == BlockKind::paragraph && plain_text(blocks[1].text) == "One line wrapped on.",
            "the lines of a paragraph are joined with a space, a carriage return dropped");
    require(blocks[2].kind == BlockKind::numbered && blocks[2].level == 1 && plain_text(blocks[2].text) == "First item." &&
                blocks[3].kind == BlockKind::numbered && blocks[3].level == 2,
            "a numbered item takes its indented lines and keeps its number");
    require(blocks[4].kind == BlockKind::bullet && blocks[4].depth == 0 &&
                spans(blocks[4].text, {{Style::bold, "Lead:"}, {Style::plain, " text more"}}) && blocks[5].depth == 1,
            "a bullet keeps its bold lead, a nested one its depth");
    require(blocks[6].kind == BlockKind::table && blocks[6].rows.size() == 3 && blocks[6].rows[1].size() == 2 &&
                spans(blocks[6].rows[1][1], {{Style::code, "a|b"}}) && blocks[6].rows[2].size() == 2 && blocks[6].rows[2][1].empty(),
            "a table splits at pipes outside code, a short row is filled to the head's columns");
    require(blocks[7].kind == BlockKind::code && blocks[7].code == "**kept** as is", "a fenced block is kept as written");
    require(blocks[8].kind == BlockKind::rule, "a rule");
    require(blocks[9].kind == BlockKind::heading && blocks[9].level == 3 && plain_text(blocks[9].text) == "Deeper",
            "an HTML comment is left out and a heading's closing marks dropped");
}

void test_anchors() {
    require(anchor_of("Where your files are") == "where-your-files-are" && anchor_of("What's new?") == "whats-new" &&
                anchor_of("Reports and replay capsules") == "reports-and-replay-capsules" && anchor_of("A_b-C 2") == "a_b-c-2",
            "an anchor is the heading in lower case, punctuation dropped and spaces as hyphens");
}

float points(const std::string& text, Style) {
    float count = 0.0f;
    for (const char byte : text) count += (static_cast<unsigned char>(byte) & 0xC0) != 0x80 ? 1.0f : 0.0f;
    return count;
}

bool whole_letters(const std::string& text) {
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<unsigned char>(text[at]);
        const std::size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (length == 0 || at + length > text.size()) return false;
        for (std::size_t next = 1; next < length; ++next)
            if ((static_cast<unsigned char>(text[at + next]) & 0xC0) != 0x80) return false;
        at += length;
    }
    return true;
}

std::string rows_of(const std::vector<Placed>& placed) {
    std::string out;
    int row = 0;
    for (const Placed& piece : placed) {
        if (piece.row != row) {
            out += '/';
            row = piece.row;
        } else if (!out.empty() && out.back() != '/') {
            out += ' ';
        }
        out += piece.text;
    }
    return out;
}

void test_wrapping() {
    const Line words = parse_inline("aaa bbb ccc");
    require(rows_of(lay_out(words, 7.0f, points)) == "aaa bbb/ccc", "words fill a row and the next goes below");
    const auto placed = lay_out(words, 7.0f, points);
    require(placed[1].x == 4.0f && placed[2].x == 0.0f && placed[2].row == 1, "a word stands after a space, a new row at its start");
    const Line glued = parse_inline("xx **file...**, end");
    const auto glued_out = lay_out(glued, 10.0f, points);
    require(glued_out.size() == 4 && glued_out[1].text == "file..." && glued_out[2].text == "," && glued_out[1].row == 1 &&
                glued_out[2].row == 1 && glued_out[2].x == 7.0f,
            "pieces with no space between them go to the next row together");
    require(rows_of(lay_out(parse_inline("`0123456789`"), 4.0f, points)) == "0123/4567/89", "a piece wider than a row is cut");
    const auto letters = lay_out(parse_inline("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"), 2.0f, points);
    require(letters.size() == 3 && letters[0].text == "\xc3\xa9\xc3\xa9" && letters[2].text == "\xc3\xa9",
            "a cut falls between code points");
    // Measured in bytes, the widest prefix that fits can end inside a letter: the cut still may not.
    const auto bytes = [](const std::string& text, Style) { return static_cast<float>(text.size()); };
    for (float width = 1.0f; width < 9.0f; width += 1.0f)
        for (const Placed& piece : lay_out(parse_inline("\xc3\xa9\xe2\x82\xac\xf0\x9f\x8e\xae\xc3\xa9"), width, bytes))
            require(whole_letters(piece.text), "a cut never splits a code point, whatever the widths");
    require(rows_of(lay_out(parse_inline("ab `cdefgh`"), 5.0f, points)) == "ab/cdefg/h",
            "a piece wider than a row starts a row of its own and is cut there");
    for (float width = 1.0f; width < 12.0f; width += 1.0f)
        for (const Placed& piece : lay_out(parse_inline("one **two** `three four` [five](#5)"), width, points))
            require(piece.x + piece.width <= width, "nothing reaches past the row");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::string> words_of(const std::string& text) {
    static const std::regex word("[A-Za-z0-9]+");
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), word); it != std::sregex_iterator(); ++it) out.push_back(it->str());
    return out;
}

// What a walk through a whole guide finds: its words in order, marks left in text, links and where they lead.
struct Reading {
    std::string text;
    int plain_marks = 0, links = 0, unresolved = 0;
    bool fullscreen = false, backtick = false;
};

Reading read(const Guide& guide) {
    Reading found;
    found.text = guide.title + ' ';
    const auto collect = [&](const Line& line) {
        for (const Span& span : line) {
            found.text += span.text + ' ';
            if (span.style != Style::code && (span.text.find("**") != std::string::npos || span.text.find('`') != std::string::npos ||
                                              span.text.find("](") != std::string::npos))
                ++found.plain_marks;
            found.links += span.target.empty() ? 0 : 1;
            if (span.target.starts_with('#') && section_of(guide, span.target) < 0) ++found.unresolved;
        }
    };
    const auto walk = [&](const std::vector<Block>& blocks) {
        for (const Block& block : blocks) {
            if (block.kind == BlockKind::numbered) found.text += std::to_string(block.level) + ' ';
            collect(block.text);
            for (const auto& row : block.rows) {
                for (const Line& cell : row) collect(cell);
                found.fullscreen = found.fullscreen || (row.size() == 2 && plain_text(row[0]) == "Fullscreen" && plain_text(row[1]) == "Alt+Enter");
                for (const Line& cell : row)
                    for (const Span& span : cell) found.backtick = found.backtick || (span.style == Style::code && span.text == "`");
            }
            found.text += block.code + ' ';
        }
    };
    walk(guide.intro);
    for (const Section& section : guide.sections) {
        found.text += section.title + ' ';
        walk(section.blocks);
    }
    return found;
}

void test_links_to_sections() {
    const Reading broken = read(guide_of("# T\n\n## A\n\nSee [`startup.log`](#missing), [b](#gone) and [a](#a).\n"));
    require(broken.links == 3 && broken.unresolved == 2, "a link to no section is found, one written as code too");
}

void test_the_players_guide() {
    const std::string markdown = read_file(std::filesystem::path(DISRUPTOR_SOURCE_DIR) / "docs" / "PLAYING.md");
    require(!markdown.empty(), "docs/PLAYING.md is read");
    const Guide guide = guide_of(markdown);
    std::vector<std::string> headings;
    std::istringstream lines(markdown);
    std::string title;
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("## ")) headings.push_back(line.substr(3));
        if (line.starts_with("# ") && title.empty()) title = line.substr(2);
    }
    require(guide.title == title && !guide.intro.empty(), "the guide's title and the text before its first section");
    require(guide.sections.size() == headings.size() && !headings.empty(), "every second-level heading is a section");
    for (std::size_t index = 0; index < headings.size(); ++index)
        require(guide.sections[index].title == headings[index] && !guide.sections[index].blocks.empty(),
                "the sections keep their order and none is empty");

    const Reading found = read(guide);
    const std::string& text = found.text;
    require(found.plain_marks == 0, "no Markdown mark is left in the text the window shows");
    require(found.unresolved == 0 && found.links > 0, "every link to a section of the guide finds it");
    require(found.fullscreen && found.backtick, "the controls table is read cell by cell, the backtick key included");

    // The parse keeps the file's words in order; what the window draws of it has no test, and it leaves out the title.
    const std::string source = std::regex_replace(std::regex_replace(markdown, std::regex("\\]\\([^)]*\\)"), "]"),
                                                  std::regex("\\|[ :|-]+\\|"), "");
    require(words_of(source) == words_of(text), "the parse keeps every word of the guide and adds none");
}

} // namespace

int main() {
    try {
        test_inline_marks();
        test_lead_in();
        test_markup_lines();
        test_blocks();
        test_anchors();
        test_wrapping();
        test_links_to_sections();
        test_the_players_guide();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "launcher guide: PASS\n";
    return 0;
}
