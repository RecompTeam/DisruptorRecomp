#pragma once

#include <cstdint>

/* The game's asynchronous disc read (0x80011944): a0 the file's first sector, a1 the buffer, a2 the size, a3 the byte
 * offset, the caller's done flag on its stack. It sets the drive's position, starts CdRead and returns; the read's
 * callback (0x80011814) clears the busy byte and sets the flag. Served here, the sectors land in the buffer at once and
 * the read is complete before the function returns, and a patched branch at 0x8001199C skips the drive. */
namespace disruptor::streamed_reads {

inline constexpr uint32_t kRead = 0x80011944u;
inline constexpr uint32_t kBusy = 0x8007116Cu;     /* gp+32: a read runs */
inline constexpr uint32_t kFlag = 0x800716CCu;     /* gp+1408: the flag the running read sets */
inline constexpr uint32_t kSectors = 0x80071710u;  /* gp+1476 */
inline constexpr uint32_t kBuffer = 0x800713ECu;   /* gp+672 */
inline constexpr uint32_t kRamEnd = 0x800713A8u;   /* gp+604: a buffer ends below it */
inline constexpr uint32_t kPosition = 0x800715FCu; /* the CdlLOC the read sets: minute, second, sector in BCD */
inline constexpr uint32_t kServed = 25u;           /* $t9: the patched branch leaves the read while it is set */
inline constexpr uint32_t kSectorBytes = 2048u;
inline constexpr uint32_t kBriefingCount = 0x80071700u; /* gp+0x5B4: raised once a VBlank under the briefing */
inline constexpr int32_t kMissionStarts = 540;
inline constexpr int kBriefingVoice = 21;

struct Request {
    uint32_t lba, sectors, buffer, size, flag;
};

constexpr uint8_t bcd(uint32_t value) { return static_cast<uint8_t>(((value / 10u) << 4) | (value % 10u)); }

template <class Memory>
Request request(const Memory &memory, const uint32_t *gpr) {
    return {gpr[4] + (gpr[7] >> 11), (gpr[6] + kSectorBytes - 1u) >> 11, gpr[5], gpr[6], memory.word(gpr[29] + 16u)};
}

/* Only a read with a flag reaches the patched branch; one the game would refuse keeps its own error path, and one
 * started while another still runs goes to the drive, as on the console. */
template <class Memory>
bool servable(const Memory &memory, const Request &read) {
    return read.flag != 0u && read.sectors != 0u && read.buffer + read.size < memory.word(kRamEnd) &&
           memory.byte(kBusy) == 0u;
}

/* The load ends by writing the music over the briefing voice, so while it plays the drive keeps its own pace. */
template <class Memory>
bool briefing_heard(const Memory &memory, bool voice_playing) {
    return voice_playing && static_cast<int32_t>(memory.word(kBriefingCount)) < kMissionStarts;
}

/* What the function and its callback leave behind once the sectors are in the buffer. */
template <class Memory>
void complete(Memory &memory, const Request &read) {
    const uint32_t at = read.lba + 150u;
    memory.write_byte(kPosition, bcd(at / 4500u));
    memory.write_byte(kPosition + 1u, bcd(at / 75u % 60u));
    memory.write_byte(kPosition + 2u, bcd(at % 75u));
    memory.write(kSectors, read.sectors);
    memory.write(kBuffer, read.buffer);
    memory.write(kFlag, read.flag);
    memory.write_byte(kBusy, 0u);
    memory.write_byte(read.flag, 1u);
}

/* The store the patched branch took the place of: the drive's read sets the flag again when it ends. */
template <class Memory>
void decline(Memory &memory, const Request &read) {
    if (read.flag != 0u) memory.write_byte(read.flag, 0u);
}

} // namespace disruptor::streamed_reads
