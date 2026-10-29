#pragma once

#include "game/language/language_disc.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace PS1 {
class ISOReader;
}

namespace disruptor::launcher {

// A CHD read by the game's own reader, which takes it as one data track from sector 0. No sectors when the game
// would not mount it or it holds anything else, and why.
class ChdImage final : public DiscImage {
public:
    explicit ChdImage(const std::filesystem::path& path);
    ~ChdImage() override;
    ChdImage(const ChdImage&) = delete;
    ChdImage& operator=(const ChdImage&) = delete;
    bool raw_sector(uint32_t lba, uint8_t* raw) override;
    uint32_t sectors() override { return sectors_; }
    const std::string& refusal() const { return refusal_; }

private:
    std::unique_ptr<PS1::ISOReader> reader_;
    uint32_t sectors_ = 0;
    std::string refusal_;
};

bool is_chd(const std::filesystem::path& path);

// A CHD decoded, any other image read as raw 2352-byte sectors. Throws with the reason when it has no sectors.
std::unique_ptr<DiscImage> open_image(const std::filesystem::path& data);

} // namespace disruptor::launcher
