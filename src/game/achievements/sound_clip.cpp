#include "game/achievements/sound_clip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

namespace disruptor::achievements {

namespace fs = std::filesystem;

namespace {

constexpr std::uint16_t kPcm = 1, kFloat = 3, kExtensible = 0xFFFE;
constexpr std::uint32_t kLowestRate = 8000, kHighestRate = 192000;

std::uint32_t u32(std::string_view bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) value |= std::uint32_t(static_cast<unsigned char>(bytes[at + index])) << (8 * index);
    return value;
}

std::uint16_t u16(std::string_view bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[at]) | static_cast<unsigned char>(bytes[at + 1]) << 8);
}

std::int16_t sample(std::string_view data, std::size_t at, std::uint16_t format, std::uint16_t bits) {
    if (format == kFloat) {
        float value = 0.0f;
        std::memcpy(&value, data.data() + at, sizeof value);
        if (!std::isfinite(value)) return 0;
        return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
    }
    switch (bits) {
    case 8: return static_cast<std::int16_t>((static_cast<int>(static_cast<unsigned char>(data[at])) - 128) * 256);
    case 16: return static_cast<std::int16_t>(u16(data, at));
    case 24: {
        const std::uint32_t low = u16(data, at), high = static_cast<unsigned char>(data[at + 2]);
        return static_cast<std::int16_t>(static_cast<std::int32_t>((high << 24) | (low << 8)) >> 16);
    }
    default: return static_cast<std::int16_t>(static_cast<std::int32_t>(u32(data, at)) >> 16);
    }
}

// The file's bytes, or none with `why` when it is too large or cannot be read.
std::optional<std::string> file_bytes(const fs::path& file, std::string& why) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(file, error);
    if (!error && size > kMostSoundBytes) {
        why = "the file is larger than 8 MB";
        return std::nullopt;
    }
    std::ifstream in{file, std::ios::binary};
    if (error || !in) {
        why = "the file cannot be read";
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

std::optional<Clip> read_wav(std::string_view bytes, std::string& why) {
    if (bytes.size() > kMostSoundBytes) {
        why = "the file is larger than 8 MB";
        return std::nullopt;
    }
    if (bytes.size() < 12 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "WAVE") {
        why = "the file is not a WAV sound";
        return std::nullopt;
    }
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    std::string_view data;
    bool described = false, found = false;
    for (std::size_t at = 12; at + 8 <= bytes.size() && !found;) {
        const std::string_view id = bytes.substr(at, 4);
        const std::size_t size = u32(bytes, at + 4), body = at + 8, left = bytes.size() - body;
        if (id == "fmt " && size >= 16 && size <= left) {
            format = u16(bytes, body);
            channels = u16(bytes, body + 2);
            rate = u32(bytes, body + 4);
            bits = u16(bytes, body + 14);
            if (format == kExtensible && size >= 26) format = u16(bytes, body + 24);
            described = true;
        } else if (id == "data" && described) {
            data = bytes.substr(body, std::min(size, left)); // a writer that never came back to fill the size in leaves it too large
            found = true;
        }
        if (size > left) break;
        at = body + size + (size & 1u);
    }
    if (!described) {
        why = "the WAV does not say how its sound is stored";
        return std::nullopt;
    }
    const bool pcm = format == kPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    if (!pcm && !(format == kFloat && bits == 32)) {
        why = "the WAV is compressed: only plain PCM or 32-bit float sound plays";
        return std::nullopt;
    }
    if (channels == 0 || channels > 8 || rate < kLowestRate || rate > kHighestRate) {
        why = "the WAV's channels or rate are out of range";
        return std::nullopt;
    }
    const std::size_t width = std::size_t(bits / 8) * channels, frames = width ? data.size() / width : 0;
    if (frames == 0) {
        why = "the WAV holds no sound";
        return std::nullopt;
    }
    if (frames > std::size_t(rate) * kMostSoundSeconds) {
        why = "the sound is longer than 10 seconds";
        return std::nullopt;
    }
    Clip clip;
    clip.rate = static_cast<int>(rate);
    clip.stereo.reserve(2 * frames);
    const std::size_t step = bits / 8;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t at = frame * width;
        const std::int16_t left = sample(data, at, format, bits);
        clip.stereo.push_back(left);
        clip.stereo.push_back(channels > 1 ? sample(data, at + step, format, bits) : left);
    }
    return clip;
}

std::optional<Clip> load_unlock_sound(const fs::path& folder, std::string& why) {
    const fs::path file = folder / kUnlockSoundFile;
    std::error_code error;
    if (!fs::is_regular_file(file, error)) return std::nullopt;
    const std::optional<std::string> bytes = file_bytes(file, why);
    return bytes ? read_wav(*bytes, why) : std::nullopt;
}

std::string keep_unlock_sound(const fs::path& chosen, const fs::path& folder) {
    std::string why;
    const std::optional<std::string> bytes = file_bytes(chosen, why);
    if (!bytes || !read_wav(*bytes, why)) return why;
    const fs::path target = folder / kUnlockSoundFile, fresh = fs::path(target).concat(".new");
    std::error_code error;
    fs::create_directories(target.parent_path(), error);
    bool kept = false;
    if (std::ofstream out{fresh, std::ios::binary | std::ios::trunc}; out)
        kept = static_cast<bool>(out.write(bytes->data(), static_cast<std::streamsize>(bytes->size())).flush());
    if (kept) {
        fs::rename(fresh, target, error);
        kept = !error;
    }
    if (kept) return {};
    fs::remove(fresh, error);
    return "the sound could not be kept with your files";
}

bool drop_unlock_sound(const fs::path& folder) {
    std::error_code error;
    fs::remove(folder / kUnlockSoundFile, error);
    return !error;
}

} // namespace disruptor::achievements
