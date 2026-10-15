#pragma once

#include "game/language/language_disc.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace disruptor::launcher {

// A disc image file of 2352-byte sectors. No sectors when the file cannot be read or is not whole sectors.
class RawImage final : public DiscImage {
public:
    explicit RawImage(const std::filesystem::path& data) : file_(data, std::ios::binary) {
        std::error_code failed;
        const auto size = std::filesystem::file_size(data, failed);
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

} // namespace disruptor::launcher
