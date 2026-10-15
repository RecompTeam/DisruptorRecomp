#include "guide.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace disruptor::launcher {

namespace {

constexpr auto npos = std::string_view::npos;

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

std::size_t indent_of(std::string_view line) {
    std::size_t spaces = 0;
    for (const char letter : line) {
        if (letter == ' ') ++spaces;
        else if (letter == '\t') spaces += 4;
        else break;
    }
    return spaces;
}

bool punctuation(char letter) { return std::ispunct(static_cast<unsigned char>(letter)) != 0; }

void add(Line& line, Style style, std::string text, const std::string& target = {}) {
    if (text.empty()) return;
    if (!line.empty() && line.back().style == style && line.back().target == target) line.back().text += text;
    else line.push_back({style, std::move(text), target});
}

struct CodeSpan {
    std::size_t inside = 0, length = 0, after = 0;
};

// A code span opened by the backticks at `at`: closed by a run of as many, else none.
std::optional<CodeSpan> code_span(std::string_view text, std::size_t at) {
    std::size_t run = text.find_first_not_of('`', at);
    if (run == npos) return std::nullopt;
    const std::size_t ticks = run - at;
    for (std::size_t search = run; (search = text.find('`', search)) != npos;) {
        std::size_t end = text.find_first_not_of('`', search);
        if (end == npos) end = text.size();
        if (end - search == ticks) return CodeSpan{run, search - run, end};
        search = end;
    }
    return std::nullopt;
}

// Where `mark` next stands at or after `from`, outside code spans and escapes.
std::size_t closing(std::string_view text, std::size_t from, std::string_view mark) {
    for (std::size_t at = from; at < text.size();) {
        if (text[at] == '\\') {
            at += 2;
        } else if (text[at] == '`') {
            const auto span = code_span(text, at);
            at = span ? span->after : text.find_first_not_of('`', at);
            if (at == npos) return npos;
        } else if (text.substr(at, mark.size()) == mark) {
            return at;
        } else {
            ++at;
        }
    }
    return npos;
}

// The bracket that closes the one at `open`, nested pairs and code spans skipped.
std::size_t bracket_end(std::string_view text, std::size_t open) {
    int depth = 0;
    for (std::size_t at = open; at < text.size();) {
        if (text[at] == '\\') {
            at += 2;
            continue;
        }
        if (text[at] == '`') {
            const auto span = code_span(text, at);
            at = span ? span->after : text.find_first_not_of('`', at);
            if (at == npos) return npos;
            continue;
        }
        if (text[at] == '[') ++depth;
        if (text[at] == ']' && --depth == 0) return at;
        ++at;
    }
    return npos;
}

// The parenthesis that closes the one at `open`: nested pairs and escaped ones are part of a link's address.
std::size_t paren_end(std::string_view text, std::size_t open) {
    int depth = 0;
    for (std::size_t at = open; at < text.size(); ++at) {
        if (text[at] == '\\') ++at;
        else if (text[at] == '(') ++depth;
        else if (text[at] == ')' && --depth == 0) return at;
    }
    return npos;
}

bool web_address(std::string_view text) {
    return (text.starts_with("https://") || text.starts_with("http://")) && text.find(' ') == npos;
}

void inline_into(Line& out, std::string_view text, Style outer, const std::string& target) {
    std::string pending;
    const auto flush = [&] { add(out, outer, std::exchange(pending, {}), target); };
    for (std::size_t at = 0; at < text.size();) {
        const char letter = text[at];
        const std::string_view pair = text.substr(at, 2);
        if (letter == '\\' && at + 1 < text.size() && punctuation(text[at + 1])) {
            pending += text[at + 1];
            at += 2;
        } else if (letter == '`') {
            const auto span = code_span(text, at);
            if (!span) {
                const std::size_t run = text.find_first_not_of('`', at);
                pending.append(text.substr(at, run == npos ? npos : run - at));
                at = run == npos ? text.size() : run;
                continue;
            }
            std::string_view inside = text.substr(span->inside, span->length);
            if (inside.size() >= 2 && inside.front() == ' ' && inside.back() == ' ' && inside.find_first_not_of(' ') != npos)
                inside = inside.substr(1, inside.size() - 2);
            flush();
            add(out, Style::code, std::string(inside), target);
            at = span->after;
        } else if ((pair == "**" || pair == "__") && at + 2 < text.size() && text[at + 2] != ' ') {
            const std::size_t close = closing(text, at + 2, pair);
            if (close == npos || text[close - 1] == ' ') {
                pending += pair;
                at += 2;
                continue;
            }
            flush();
            inline_into(out, text.substr(at + 2, close - at - 2), outer == Style::link ? Style::link : Style::bold, target);
            at = close + 2;
        } else if (letter == '*' && at + 1 < text.size() && text[at + 1] != ' ' && text[at + 1] != '*') {
            const std::size_t close = closing(text, at + 1, "*");
            if (close == npos || text[close - 1] == ' ') {
                pending += letter;
                ++at;
                continue;
            }
            flush();
            inline_into(out, text.substr(at + 1, close - at - 1), outer, target);
            at = close + 1;
        } else if ((letter == '[' || pair == "![") && outer != Style::link) {
            const std::size_t open = letter == '!' ? at + 1 : at;
            const std::size_t close = bracket_end(text, open);
            const std::size_t end = close != npos && close + 1 < text.size() && text[close + 1] == '(' ? paren_end(text, close + 1) : npos;
            if (end == npos) {
                pending += letter;
                ++at;
                continue;
            }
            std::string_view written = trimmed(text.substr(close + 2, end - close - 2));
            written = written.substr(0, written.find(' '));
            std::string address;
            for (std::size_t index = 0; index < written.size(); ++index) {
                if (written[index] == '\\' && index + 1 < written.size() && punctuation(written[index + 1])) ++index;
                address += written[index];
            }
            std::string_view label = text.substr(open + 1, close - open - 1);
            if (trimmed(label).empty()) label = address;
            flush();
            inline_into(out, label, Style::link, address);
            at = end + 1;
        } else if (letter == '<' && outer != Style::link) {
            const std::size_t end = text.find('>', at);
            const std::string_view inside = end == npos ? std::string_view() : text.substr(at + 1, end - at - 1);
            if (!web_address(inside)) {
                pending += letter;
                ++at;
                continue;
            }
            flush();
            add(out, Style::link, std::string(inside), std::string(inside));
            at = end + 1;
        } else {
            pending += letter;
            ++at;
        }
    }
    flush();
}

bool rule(std::string_view body) {
    if (body.size() < 3 || (body[0] != '-' && body[0] != '*' && body[0] != '_')) return false;
    std::size_t marks = 0;
    for (const char letter : body) {
        if (letter == body[0]) ++marks;
        else if (letter != ' ') return false;
    }
    return marks >= 3;
}

bool table_separator(std::string_view body) {
    return body.find('|') != npos && body.find('-') != npos && body.find_first_not_of("|:- \t") == npos;
}

std::vector<Line> cells(std::string_view row) {
    if (row.starts_with('|')) row.remove_prefix(1);
    if (row.ends_with('|') && !row.ends_with("\\|")) row.remove_suffix(1);
    std::vector<Line> out;
    std::size_t start = 0;
    for (std::size_t at = 0; at <= row.size();) {
        if (at < row.size() && row[at] == '\\') {
            at += 2;
        } else if (at < row.size() && row[at] == '`') {
            const auto span = code_span(row, at);
            at = span ? span->after : at + 1;
        } else if (at == row.size() || row[at] == '|') {
            out.push_back(parse_inline(trimmed(row.substr(start, at - start))));
            start = ++at;
        } else {
            ++at;
        }
    }
    return out;
}

// "1. " or "1) ": the item's number and where its text begins.
std::optional<std::pair<int, std::size_t>> numbered(std::string_view body) {
    std::size_t digits = 0;
    while (digits < body.size() && digits < 9 && std::isdigit(static_cast<unsigned char>(body[digits]))) ++digits;
    if (digits == 0 || digits + 1 >= body.size() || (body[digits] != '.' && body[digits] != ')') || body[digits + 1] != ' ')
        return std::nullopt;
    return std::pair{std::stoi(std::string(body.substr(0, digits))), digits + 2};
}

// A line that is one HTML comment or one tag and nothing else.
bool markup_only(std::string_view body) {
    if (body.starts_with("<!--")) return body.find("-->") == body.size() - 3;
    if (!body.starts_with('<') || body.find('>') != body.size() - 1) return false;
    return body.starts_with("</") ||
           (body.size() > 2 && std::isalpha(static_cast<unsigned char>(body[1])) && !web_address(body.substr(1, body.size() - 2)));
}

} // namespace

Line parse_inline(std::string_view text) {
    Line out;
    inline_into(out, text, Style::plain, {});
    return out;
}

std::vector<Block> parse_markdown(std::string_view markdown) {
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start <= markdown.size();) {
        std::size_t end = markdown.find('\n', start);
        if (end == npos) end = markdown.size();
        std::string_view line = markdown.substr(start, end - start);
        if (line.ends_with('\r')) line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    std::vector<Block> blocks;
    std::optional<Block> open;
    std::string pending;
    const auto close = [&] {
        if (!open) return;
        open->text = parse_inline(pending);
        blocks.push_back(std::move(*open));
        open.reset();
        pending.clear();
    };
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string_view line = lines[index];
        std::string_view body = trimmed(line);
        if (body.empty()) {
            close();
            continue;
        }
        if (body.starts_with("```") || body.starts_with("~~~")) {
            close();
            const std::string_view fence = body.substr(0, 3);
            Block code{BlockKind::code};
            while (++index < lines.size() && !trimmed(lines[index]).starts_with(fence)) (code.code += lines[index]) += '\n';
            if (!code.code.empty()) code.code.pop_back();
            blocks.push_back(std::move(code));
            continue;
        }
        if (body.front() == '#') {
            const std::size_t level = body.find_first_not_of('#');
            if (level != npos && level <= 6 && body[level] == ' ') {
                close();
                std::string_view title = trimmed(body.substr(level));
                const std::size_t marks = title.find_last_not_of('#');
                if (marks != npos && marks + 1 < title.size() && title[marks] == ' ') title = trimmed(title.substr(0, marks));
                Block heading{BlockKind::heading, static_cast<int>(level)};
                heading.text = parse_inline(title);
                blocks.push_back(std::move(heading));
                continue;
            }
        }
        if (body.front() == '|' && index + 1 < lines.size() && table_separator(trimmed(lines[index + 1]))) {
            close();
            Block table{BlockKind::table};
            table.rows.push_back(cells(body));
            ++index;
            while (index + 1 < lines.size() && trimmed(lines[index + 1]).starts_with('|')) table.rows.push_back(cells(trimmed(lines[++index])));
            for (auto& row : table.rows) row.resize(table.rows.front().size());
            blocks.push_back(std::move(table));
            continue;
        }
        if (rule(body)) {
            close();
            blocks.push_back(Block{BlockKind::rule});
            continue;
        }
        if (markup_only(body)) continue;
        if (body.starts_with("<!--") && body.find("-->") == npos) {
            close();
            while (++index < lines.size() && lines[index].find("-->") == npos) {}
            continue;
        }
        const int depth = static_cast<int>(std::min<std::size_t>(indent_of(line) / 2, 3));
        if ((body[0] == '-' || body[0] == '*' || body[0] == '+') && body.size() > 1 && body[1] == ' ') {
            close();
            open = Block{BlockKind::bullet, 0, depth};
            pending = trimmed(body.substr(2));
            continue;
        }
        if (const auto item = numbered(body)) {
            close();
            open = Block{BlockKind::numbered, item->first, depth};
            pending = trimmed(body.substr(item->second));
            continue;
        }
        if (body.front() == '>') body = trimmed(body.substr(1));
        if (open) {
            (pending += ' ') += body;
        } else {
            open = Block{BlockKind::paragraph};
            pending = body;
        }
    }
    close();
    return blocks;
}

std::string plain_text(const Line& line) {
    std::string text;
    for (const Span& span : line) text += span.text;
    return text;
}

std::string anchor_of(std::string_view heading) {
    std::string anchor;
    for (const char letter : heading) {
        const auto byte = static_cast<unsigned char>(letter);
        if (std::isalnum(byte) || letter == '-' || letter == '_' || byte >= 0x80) anchor += static_cast<char>(std::tolower(byte));
        else if (letter == ' ') anchor += '-';
    }
    return anchor;
}

Guide guide_of(std::string_view markdown) {
    Guide guide;
    for (Block& block : parse_markdown(markdown)) {
        if (block.kind == BlockKind::heading && block.level == 1 && guide.title.empty() && guide.sections.empty() &&
            guide.intro.empty()) {
            guide.title = plain_text(block.text);
        } else if (block.kind == BlockKind::heading && block.level == 2) {
            std::string title = plain_text(block.text);
            std::string anchor = anchor_of(title);
            guide.sections.push_back({std::move(title), std::move(anchor), {}});
        } else {
            (guide.sections.empty() ? guide.intro : guide.sections.back().blocks).push_back(std::move(block));
        }
    }
    return guide;
}

int section_of(const Guide& guide, std::string_view target) {
    if (!target.starts_with('#')) return -1;
    target.remove_prefix(1);
    for (std::size_t index = 0; index < guide.sections.size(); ++index)
        if (guide.sections[index].anchor == target) return static_cast<int>(index);
    return -1;
}

std::vector<Placed> lay_out(const Line& line, float width, const std::function<float(const std::string&, Style)>& measure) {
    struct Piece {
        std::size_t span;
        std::string text;
    };
    std::vector<std::vector<Piece>> words;
    std::vector<Piece> word;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const Span& span = line[index];
        if (span.style == Style::code) {
            word.push_back({index, span.text});
            continue;
        }
        std::string part;
        for (const char letter : span.text) {
            if (letter != ' ' && letter != '\t' && letter != '\n') {
                part += letter;
                continue;
            }
            if (!part.empty()) word.push_back({index, std::exchange(part, {})});
            if (!word.empty()) words.push_back(std::exchange(word, {}));
        }
        if (!part.empty()) word.push_back({index, std::move(part)});
    }
    if (!word.empty()) words.push_back(std::move(word));

    width = std::max(width, 1.0f);
    const float space = measure(" ", Style::plain);
    std::vector<Placed> placed;
    float x = 0.0f;
    int row = 0;
    for (auto& pieces : words) {
        float whole = 0.0f;
        for (const Piece& piece : pieces) whole += measure(piece.text, line[piece.span].style);
        if (x > 0.0f && x + space + whole > width) {
            ++row;
            x = 0.0f;
        } else if (x > 0.0f) {
            x += space;
        }
        for (Piece& piece : pieces) {
            const Style style = line[piece.span].style;
            float size = measure(piece.text, style);
            if (x > 0.0f && x + size > width && whole > width) {
                ++row;
                x = 0.0f;
            }
            // A piece wider than what is left of the row is cut at the last code point that fits.
            while (x + size > width) {
                std::size_t cut = 0;
                for (std::size_t at = 1; at <= piece.text.size(); ++at) {
                    if (at < piece.text.size() && (static_cast<unsigned char>(piece.text[at]) & 0xC0) == 0x80) continue;
                    if (x + measure(piece.text.substr(0, at), style) > width) break;
                    cut = at;
                }
                if (cut == 0 && x > 0.0f) {
                    ++row;
                    x = 0.0f;
                    continue;
                }
                if (cut == 0) {
                    cut = 1;
                    while (cut < piece.text.size() && (static_cast<unsigned char>(piece.text[cut]) & 0xC0) == 0x80) ++cut;
                }
                if (cut >= piece.text.size()) break;
                const std::string head = piece.text.substr(0, cut);
                placed.push_back({piece.span, head, x, measure(head, style), row});
                piece.text.erase(0, cut);
                size = measure(piece.text, style);
                ++row;
                x = 0.0f;
            }
            placed.push_back({piece.span, piece.text, x, size, row});
            x += size;
        }
    }
    return placed;
}

} // namespace disruptor::launcher
