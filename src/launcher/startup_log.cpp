#include "startup_log.h"

#include "launcher_settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {
constexpr std::streamoff most_bytes = 16384;
} // namespace

std::wstring log_tail(const fs::path& log, size_t lines) {
    std::ifstream file(log, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff size = file.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - most_bytes);
    std::string text(static_cast<size_t>(size - start), '\0');
    file.seekg(start);
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(std::max<std::streamsize>(file.gcount(), 0)));
    if (start > 0) text.erase(0, std::min(text.find('\n'), text.size() - 1) + 1);

    std::vector<std::string> kept;
    size_t end = text.size();
    while (end > 0 && kept.size() < lines) {
        const size_t newline = text.rfind('\n', end - 1);
        const size_t begin = newline == std::string::npos ? 0 : newline + 1;
        std::string line = text.substr(begin, end - begin);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) kept.push_back(std::move(line));
        end = newline == std::string::npos ? 0 : newline;
    }
    std::string joined;
    for (auto line = kept.rbegin(); line != kept.rend(); ++line) joined += *line + "\r\n";
    return wide(joined);
}

bool FirstFrameWatch::seen(const fs::path& log) {
    if (seen_) return true;
    std::ifstream file(log, std::ios::binary | std::ios::ate);
    const std::streamoff size = file ? static_cast<std::streamoff>(file.tellg()) : -1;
    if (size < 0 || static_cast<std::uintmax_t>(size) < read_) {
        read_ = 0;
        line_.clear();
    }
    if (size <= 0 || static_cast<std::uintmax_t>(size) == read_) return false;
    std::string text(static_cast<size_t>(static_cast<std::uintmax_t>(size) - read_), '\0');
    file.seekg(static_cast<std::streamoff>(read_));
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(std::max<std::streamsize>(file.gcount(), 0)));
    read_ += text.size();
    for (const char letter : text) {
        if (letter == '\n') {
            line_.clear();
            continue;
        }
        if (line_.size() < first_frame_line.size()) line_ += letter;
        if (line_ == first_frame_line) return seen_ = true;
    }
    return false;
}

void note_exit(const fs::path& log, unsigned long code, unsigned long long seconds) {
    bool mid_line = false;
    if (std::ifstream file(log, std::ios::binary | std::ios::ate); file && file.tellg() > 0) {
        file.seekg(-1, std::ios::end);
        mid_line = file.get() != '\n';
    }
    std::array<char, 96> line{};
    std::snprintf(line.data(), line.size(), "%slauncher: the game ended with code 0x%08lX after %llu s\r\n",
                  mid_line ? "\r\n" : "", code, seconds);
    std::ofstream(log, std::ios::binary | std::ios::app) << line.data();
}

} // namespace disruptor::launcher
