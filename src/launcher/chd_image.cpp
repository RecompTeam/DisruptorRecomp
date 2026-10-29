#include "chd_image.h"

#include "iso_reader.h"
#include "raw_image.h"

#include <exception>
#include <stdexcept>

namespace fs = std::filesystem;
namespace disruptor::launcher {

ChdImage::ChdImage(const fs::path& path) : reader_(std::make_unique<PS1::ISOReader>()) {
    // The game opens it by this narrow name too: UTF-8 under the code page both programs' manifests set.
    std::string name;
    try {
        name = path.string();
    } catch (const std::exception&) {
    }
    if (name.empty() || !reader_->Open(name)) {
        refusal_ = "Cannot read the CHD\nIt may be damaged, or need the parent CHD it was made against";
        return;
    }
    if (reader_->TrackCount() != 1 || reader_->TrackIsAudio(1) || reader_->TrackPregapLBA(1) != 0 ||
        reader_->TrackStartLBA(1) != 0) {
        refusal_ = "Unsupported CHD layout\nThe supported USA dump has one MODE2/2352 track with no pregap\n"
                   "Multi-track or offset images cannot be imported by this build";
        return;
    }
    sectors_ = reader_->GetSectorCount();
}

ChdImage::~ChdImage() = default;

bool ChdImage::raw_sector(uint32_t lba, uint8_t* raw) { return lba < sectors_ && reader_->ReadRawSector(lba, raw); }

bool is_chd(const fs::path& path) {
    const auto extension = path.extension().u8string();
    return extension.size() == 4 && extension[0] == u8'.' && (extension[1] | 0x20) == u8'c' &&
           (extension[2] | 0x20) == u8'h' && (extension[3] | 0x20) == u8'd';
}

std::unique_ptr<DiscImage> open_image(const fs::path& data) {
    if (is_chd(data)) {
        auto image = std::make_unique<ChdImage>(data);
        if (!image->sectors()) throw std::runtime_error(image->refusal());
        return image;
    }
    auto image = std::make_unique<RawImage>(data);
    if (!image->sectors()) throw std::runtime_error("The image is not a raw disc image of 2352-byte sectors");
    return image;
}

} // namespace disruptor::launcher
