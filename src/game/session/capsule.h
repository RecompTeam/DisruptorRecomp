#ifndef DISRUPTOR_CAPSULE_H
#define DISRUPTOR_CAPSULE_H

#include <stdint.h>

#ifdef __cplusplus
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::capsule {

/* What went into the game at one VBlank, and what its memory was then. */
struct Frame {
    uint16_t pad[2] = {0xFFFF, 0xFFFF}; /* active low, as the game reads them */
    uint32_t byte_at = 0;               /* a byte a module wrote into the game this frame, 0 for none */
    uint8_t byte = 0;
    uint32_t byte2_at = 0;              /* a second write, e.g. weapon selection alongside mouse yaw */
    uint8_t byte2 = 0;
    float pitch = 0.0f;                 /* the vertical look, which lives outside the game */
    uint64_t memory = 0;

    void note_byte(uint32_t address, uint8_t value) {
        if (byte_at == 0u || byte_at == address) {
            byte_at = address;
            byte = value;
        } else {
            byte2_at = address;
            byte2 = value;
        }
    }
};

/* first_cycle: the emulated clock at the first frame's VBlank, which places every later one. */
std::string encode(const std::vector<Frame> &frames, uint64_t first_cycle);
bool decode(std::string_view bytes, std::vector<Frame> &frames, uint64_t &first_cycle);
uint64_t digest(const uint8_t *bytes, size_t size);
/* A 24-bit bottom-up BMP of `width` by `height` from rows of RGB. */
std::string bitmap(const std::vector<uint8_t> &rgb, uint32_t width, uint32_t height);

/* What an F12 press asks for. With Ctrl a recording starts or ends. Without it the newest capsule's state is loaded, or a recording that
 * runs is ended: loading would end it anyway. A held key's repeats and Shift without Ctrl, once a save state key, ask for nothing. */
enum class Press { kNothing, kRecord, kLoad };
Press press(bool repeat, bool ctrl, bool shift, bool recording);

/* A capsule kept on disk, as a list shows it. */
struct Stored {
    std::string name;    /* its folder under capsules/: the second its recording began, with "-2" and on for more in that second */
    int64_t began = 0;   /* that second of the Unix epoch */
    uint32_t frames = 0; /* its length in VBlanks, 0 when the recording was never finished */
    uint32_t pictures = 0; /* its preview pictures, numbered from 0 without a gap */
    int64_t revised = 0; /* when the last of them was written, in the file system's own count: it is replaced past the eightieth */
};
bool stored_name(std::string_view name, int64_t &began);
/* The length a capsule.toml states, 0 when it states none. */
uint32_t stored_frames(std::string_view about);
/* A length in whole seconds, rounded, at sixty VBlanks a second. */
uint32_t stored_seconds(uint32_t frames);
/* The capsules under `all`, the newest first. */
std::vector<Stored> stored(const std::filesystem::path &all);
/* The newest capsule whose recording was finished, or nothing. */
std::string newest(const std::vector<Stored> &kept);
/* The vertical look a capsule begins with: false unless its note and its input are whole and of one length. */
inline constexpr uintmax_t kInputBytes = 1u << 20; /* the longest recording, ten minutes, is 756 KB */
bool begins(const std::filesystem::path &folder, float &pitch);
/* Removes one capsule's folder. A name that is no capsule's is refused, so nothing else under `all` or outside it can be named. */
bool discard(const std::filesystem::path &all, std::string_view name);
/* A recording keeps a small picture of the screen every half second from its first second on, eighty at most: past that the last is replaced. */
inline constexpr uint32_t kPreviewWidth = 240, kPreviewFirst = 60, kPreviewEvery = 30, kPreviewMost = 80;
bool preview_due(size_t frame);
uint32_t preview_slot(uint32_t taken);
std::string preview_name(uint32_t number);
/* A picture's file, a binary PPM, from rows of RGB top down, and back: refused unless it is that and whole. */
std::string preview(const uint8_t *rgb, uint32_t width, uint32_t height);
bool preview_decode(std::string_view file, uint32_t &width, uint32_t &height, std::string_view &rgb);
/* A picture's file as bytes, empty unless it is a regular file of at most kPreviewBytes. */
inline constexpr uintmax_t kPreviewBytes = 8u << 20;
std::string preview_read(const std::filesystem::path &file);
/* A folder as a file URL, for the host to open. */
std::string folder_url(const std::filesystem::path &folder);
/* Where this session keeps its capsules, and the name of the one it is recording now, or none. */
std::filesystem::path home();
std::string recording();
/* Asks for a capsule's state to be loaded into the running game, which then stands where the recording began: false when refused. */
bool load(std::string_view name);

}  /* namespace disruptor::capsule */

extern "C" {
#endif

/* Once per VBlank, before the pads are sampled: a recording takes what the last VBlank left in them and the game's memory now. */
void disruptor_capsule_before_input(void);
/* Once per VBlank, after the pads are sampled: a replay puts the recorded frame in their place. */
void disruptor_capsule_after_input(void);
void disruptor_capsule_note_byte(uint32_t address, uint8_t value);
/* The frontend calls this when a save state has been loaded, before the game runs on. */
void disruptor_capsule_state_loaded(void);
/* A word to show for a second when a recording starts, ends or is refused, or null. A visible overlay holds the in-between frames off, so it is not shown longer. */
const char *disruptor_capsule_notice(void);
/* Ctrl+F12: the first press saves a state into a new folder under capsules/ and records from there, the second ends it. */
void disruptor_capsule_toggle(void);
/* F12 went down, a repeat of a held key included: it is the capsule's and never a save state key, see Press. */
void disruptor_capsule_key(int repeat, int ctrl, int shift);
/* Nonzero while the frontend's save state folder is a capsule's: its own keys would save and load there. */
int disruptor_capsule_holds_states(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* DISRUPTOR_CAPSULE_H */
