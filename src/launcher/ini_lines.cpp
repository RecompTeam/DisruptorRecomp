#include "ini_lines.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace disruptor::launcher {

std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    return std::string(text.substr(first, text.find_last_not_of(" \t\r") - first + 1));
}

std::string lowered(std::string text) {
    for (char& letter : text) letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    return text;
}

std::optional<std::vector<std::string>> read_lines(const fs::path& file) {
    std::error_code failed;
    const fs::file_type type = fs::status(file, failed).type();
    if (type == fs::file_type::not_found) return std::vector<std::string>{};
    if (type != fs::file_type::regular) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    if (in.bad()) return std::nullopt;
    return lines;
}

bool write_lines(const fs::path& file, const std::vector<std::string>& lines) {
    std::ostringstream text;
    for (const std::string& line : lines) text << line << '\n';
    const fs::path temporary = fs::path(file).concat(".new");
    std::error_code ignored;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text.str();
        out.close();
        if (!out) {
            fs::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code failed;
    fs::rename(temporary, file, failed);
    if (failed) fs::remove(temporary, ignored);
    return !failed;
}

void put_section(std::vector<std::string>& lines, std::string_view section, std::span<const IniValue> values, std::size_t pad,
                 Headers headers) {
    const auto line_of = [pad](const std::string& key, const std::string& value) {
        std::string padded = key;
        padded.resize(std::max(padded.size(), pad), ' ');
        return padded + " = " + value;
    };
    std::vector<bool> written(values.size());
    std::string current;
    std::size_t end = lines.size();
    bool found = false;
    for (std::size_t at = 0; at < lines.size(); ++at) {
        const std::string line = trimmed(lines[at]);
        const std::string bare = headers == Headers::closed ? trimmed(line.substr(0, line.find_first_of(";#"))) : line;
        if (!bare.empty() && bare.front() == '[' && (headers == Headers::opened || bare.back() == ']')) {
            if (current == section) end = at;
            const std::size_t close = headers == Headers::closed ? bare.size() - 1 : bare.find(']');
            current = lowered(trimmed(bare.substr(1, close - 1)));
            found = found || current == section;
            continue;
        }
        const auto equals = line.find('=');
        if (current != section || equals == std::string::npos || line[0] == '#' || line[0] == ';') continue;
        const std::string key = lowered(trimmed(line.substr(0, equals)));
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (values[index].key != key) continue;
            lines[at] = line_of(key, values[index].value);
            written[index] = true;
        }
    }
    if (current == section) end = lines.size();
    std::vector<std::string> missing;
    if (!found) {
        missing.push_back("[" + std::string(section) + "]");
        end = lines.size();
    }
    for (std::size_t index = 0; index < values.size(); ++index)
        if (!written[index]) missing.push_back(line_of(values[index].key, values[index].value));
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(end), missing.begin(), missing.end());
}

} // namespace disruptor::launcher
