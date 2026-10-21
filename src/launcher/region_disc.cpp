#include "region_disc.h"

#include "disc_import.h"
#include "game/language/language_disc.h"
#include "raw_image.h"

#include <cstdint>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {
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
        throw std::runtime_error("The image is not a raw disc image of 2352-byte sectors");
    KnownDisc known{};
    if (!known_disc(other, known))
        throw std::runtime_error("The image is not a Disruptor disc of another region that this build knows");
    LanguageDisc laid_out;
    std::string why;
    if (!laid_out.build(home, other, why))
        throw std::runtime_error(std::string("The ") + known.language + " disc does not fit the US disc: " + why);
    return {known.language, known.words};
}

std::wstring describe(const RegionDisc& disc) {
    return wide(disc.language) + (disc.words
        ? L" disc: menus, texts, movies, speech and hints"
        : L" disc: movies, speech and hints\nIts menus are in English on the disc itself");
}

} // namespace disruptor::launcher
