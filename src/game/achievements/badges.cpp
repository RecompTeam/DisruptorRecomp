#include "game/achievements/badges.h"

#include <array>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace disruptor::achievements {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kMedia = "https://media.retroachievements.org/";
constexpr std::string_view kSignature = "\x89PNG\r\n\x1a\n";
constexpr std::array<std::string_view, 2> kFolders = {"Badge/", "Images/"};

bool picture(std::string_view bytes) { return bytes.size() <= Badges::kLargest && bytes.starts_with(kSignature); }

std::shared_ptr<const std::string> read_kept(const fs::path& file) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || size > Badges::kLargest) return nullptr;
    std::ifstream in(file, std::ios::binary);
    auto bytes = std::make_shared<std::string>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return bytes->size() == size && picture(*bytes) ? bytes : nullptr;
}

void keep(const fs::path& file, const std::string& bytes) {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    fs::path written = file;
    written += ".new";
    {
        std::ofstream out(written, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out.flush()) {
            out.close();
            fs::remove(written, error);
            return;
        }
    }
    fs::rename(written, file, error);
    if (error) fs::remove(written, error);
}

} // namespace

std::string badge_file_name(std::string_view url) {
    if (!url.starts_with(kMedia)) return {};
    url.remove_prefix(kMedia.size());
    for (const std::string_view folder : kFolders) {
        if (!url.starts_with(folder)) continue;
        std::string_view stem = url.substr(folder.size());
        if (!stem.ends_with(".png")) return {};
        stem.remove_suffix(4);
        if (stem.empty() || stem.size() > 40) return {};
        for (const char letter : stem) {
            const bool plain = (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'z') ||
                               (letter >= 'A' && letter <= 'Z') || letter == '_';
            if (!plain) return {};
        }
        std::string name(folder.substr(0, folder.size() - 1));
        return name + "_" + std::string(stem) + ".png";
    }
    return {};
}

Badges::Badges(fs::path folder, std::unique_ptr<Transport> transport)
    : folder_(std::move(folder)), requests_(std::move(transport)) {}

std::shared_ptr<const std::string> Badges::png(const std::string& url) {
    {
        const std::lock_guard lock(mutex_);
        if (const auto found = entries_.find(url); found != entries_.end()) return found->second.png;
        entries_[url] = {};
    }
    const std::string name = badge_file_name(url);
    std::shared_ptr<const std::string> kept = name.empty() ? nullptr : read_kept(folder_ / name);
    {
        const std::lock_guard lock(mutex_);
        Entry& entry = entries_[url];
        if (name.empty()) entry.state = State::failed;
        if (kept) entry = {State::ready, kept};
        if (entry.state != State::fetching) return entry.png;
    }
    requests_.send({url, {}, {}}, [this, url, name](const Response& response) { arrived(url, name, response); });
    return nullptr;
}

void Badges::arrived(const std::string& url, const std::string& name, const Response& response) {
    const bool good = response.status == 200 && picture(response.body);
    if (good) keep(folder_ / name, response.body);
    const std::lock_guard lock(mutex_);
    entries_[url] = good ? Entry{State::ready, std::make_shared<const std::string>(response.body)}
                         : Entry{State::failed, nullptr};
}

} // namespace disruptor::achievements
