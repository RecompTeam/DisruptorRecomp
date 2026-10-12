#ifndef DISRUPTOR_LANGUAGE_DISC_H
#define DISRUPTOR_LANGUAGE_DISC_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace disruptor {

constexpr uint32_t kRawSector = 2352;
constexpr uint32_t kSectorData = 2048;
constexpr uint32_t kHintBytes = 0x5000;  /* a level's hint, 320 by 32 in 16 bits: what the US code loads of it */
constexpr uint32_t kTallHintBytes = 0xA000;  /* one twice as tall, as the Japanese disc has them */

class DiscImage {
  public:
    virtual ~DiscImage() = default;
    virtual bool raw_sector(uint32_t lba, uint8_t *raw) = 0;
    virtual uint32_t sectors() = 0;
};

struct KnownDisc {
    const char *language;  /* in English, as a launcher lists it */
    bool words;  /* its menus and texts are laid out too, and not only its movies, speech and hints */
};

/* Which of the known discs this is, by its boot file alone: whether it fits the US disc is for LanguageDisc::build to say. */
bool known_disc(DiscImage &other, KnownDisc &known);

/* The US disc with another region's movies, speech and words laid over it, sector by sector. */
class LanguageDisc {
  public:
    /* False, with the reason, when the other disc is not one of the known ones or does not fit the US disc. */
    bool build(DiscImage &home, DiscImage &other, std::string &why);
    bool raw_sector(uint32_t lba, uint8_t *raw);
    uint32_t sectors() const { return sectors_; }
    /* The words of the other executable for the strings of the US one, as disruptor_language_load_pack takes them. */
    const std::vector<uint8_t> &pack() const { return pack_; }
    /* Whether the hints laid out are twice as tall as the US code shows without help. */
    bool tall_hints() const { return tall_hints_; }
    /* The language of the disc laid out, as KnownDisc names it: empty until build succeeds. */
    const char *language() const { return language_; }

  private:
    struct Run {
        uint32_t first;
        uint32_t count;
        bool from_other;
        uint32_t source;
    };

    void add(bool from_other, uint32_t source, uint32_t count);
    void set_data(uint32_t lba, const uint8_t *data);

    DiscImage *home_ = nullptr;
    DiscImage *other_ = nullptr;
    std::vector<Run> runs_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> data_;
    std::vector<uint8_t> pack_;
    uint32_t sectors_ = 0;
    bool tall_hints_ = false;
    const char *language_ = "";
};

}  // namespace disruptor

#endif
