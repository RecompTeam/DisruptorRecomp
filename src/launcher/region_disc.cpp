#include "region_disc.h"

#include "disc_import.h"
#include "game/language/language_disc.h"

#include <cstdint>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {
class RawImage final : public DiscImage {
public:
    explicit RawImage(const fs::path& data) : file_(data, std::ios::binary) {
        std::error_code failed;
        const auto size = fs::file_size(data, failed);
        if (file_ && !failed && size % kRawSector == 0) sectors_ = static_cast<uint32_t>(size / kRawSector);
    }
    bool raw_sector(uint32_t lba, uint8_t* raw) override {
        if (lba >= sectors_) return false;
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(lba) * kRawSector);
        return static_cast<bool>(file_.read(reinterpret_cast<char*>(raw), kRawSector));
    }
    uint32_t sectors() override { return sectors_; }

private:
    std::ifstream file_;
    uint32_t sectors_ = 0;
};

std::wstring wide(const std::string& ascii) { return {ascii.begin(), ascii.end()}; }
} // namespace

std::optional<RegionDisc> region_disc(const fs::path& selected) {
    try {
        RawImage image(resolve_image(selected));
        KnownDisc known{};
        if (known_disc(image, known)) return RegionDisc{known.language, known.words};
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

RegionDisc check_region_disc(const fs::path& home_data, const fs::path& selected) {
    RawImage home(home_data), other(resolve_image(selected));
    if (!other.sectors())
        throw std::runtime_error("The image is not a raw disc image of 2352-byte sectors.");
    KnownDisc known{};
    if (!known_disc(other, known))
        throw std::runtime_error("The image is not a Disruptor disc of another region that this build knows.");
    LanguageDisc laid_out;
    std::string why;
    if (!laid_out.build(home, other, why))
        throw std::runtime_error(std::string("The ") + known.language + " disc does not fit the US disc: " + why + ".");
    return {known.language, known.words};
}

std::wstring describe(const RegionDisc& disc) {
    return wide(disc.language) + (disc.words
        ? L" disc: menus, texts, movies, speech and hints."
        : L" disc: movies, speech and hints. Its menus are in English on the disc itself.");
}

} // namespace disruptor::launcher
