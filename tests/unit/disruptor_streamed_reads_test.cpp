#include "game/session/streamed_reads.h"

#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace {

using namespace disruptor::streamed_reads;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Memory {
    std::unordered_map<uint32_t, uint32_t> words;
    std::unordered_map<uint32_t, uint8_t> bytes;
    uint32_t word(uint32_t address) const { return words.contains(address) ? words.at(address) : 0u; }
    uint32_t byte(uint32_t address) const { return bytes.contains(address) ? bytes.at(address) : 0u; }
    void write(uint32_t address, uint32_t value) { words[address] = value; }
    void write_byte(uint32_t address, uint8_t value) { bytes[address] = value; }
};

constexpr uint32_t kStack = 0x801FFE00u, kFlagAt = 0x80071724u, kBufferAt = 0x80100000u;

struct Call {
    Memory memory;
    uint32_t gpr[32] = {};
};

Call area_read(uint32_t flag) {
    Call call;
    call.gpr[4] = 220u;            /* the file's first sector */
    call.gpr[5] = kBufferAt;
    call.gpr[6] = 300000u;
    call.gpr[7] = 69839u * 2048u;  /* its byte offset in the file */
    call.gpr[29] = kStack;
    call.memory.words[kStack + 16u] = flag;
    call.memory.words[kRamEnd] = 0x801C0000u;
    return call;
}

void test_request() {
    const Call call = area_read(kFlagAt);
    const Request read = request(call.memory, call.gpr);
    expect(read.lba == 70059u, "the sector: the file's first plus the offset in sectors");
    expect(read.sectors == 147u, "whole sectors, the last one partly used");
    expect(read.buffer == kBufferAt && read.flag == kFlagAt, "the buffer, and the flag from the caller's stack");
}

void test_servable() {
    Call call = area_read(kFlagAt);
    expect(servable(call.memory, request(call.memory, call.gpr)), "a read with a flag that fits is served");
    expect(!servable(area_read(0u).memory, request(area_read(0u).memory, area_read(0u).gpr)),
           "without a flag the patched branch is never reached");
    call.memory.words[kRamEnd] = kBufferAt + 300000u;
    expect(!servable(call.memory, request(call.memory, call.gpr)), "a buffer that runs to the end keeps the game's error path");
    call = area_read(kFlagAt);
    call.gpr[6] = 0u;
    expect(!servable(call.memory, request(call.memory, call.gpr)), "an empty read is left to the drive");
    call = area_read(kFlagAt);
    call.memory.bytes[kBusy] = 1u;
    expect(!servable(call.memory, request(call.memory, call.gpr)), "a read over one the drive still runs goes to the drive");
}

void test_complete() {
    Call call = area_read(kFlagAt);
    call.memory.bytes[kBusy] = 1u;
    const Request read = request(call.memory, call.gpr);
    complete(call.memory, read);
    expect(call.memory.byte(kFlagAt) == 1u && call.memory.byte(kBusy) == 0u, "done, as the read's callback leaves it");
    expect(call.memory.word(kFlag) == kFlagAt && call.memory.word(kSectors) == 147u && call.memory.word(kBuffer) == kBufferAt,
           "the function's own bookkeeping");
    expect(call.memory.byte(kPosition) == 0x15u && call.memory.byte(kPosition + 1u) == 0x36u &&
               call.memory.byte(kPosition + 2u) == 0x09u,
           "the position CdIntToPos gives sector 70059: 15:36:09");
}

void test_decline() {
    Call call = area_read(kFlagAt);
    call.memory.bytes[kFlagAt] = 1u;
    decline(call.memory, request(call.memory, call.gpr));
    expect(call.memory.byte(kFlagAt) == 0u, "a read left to the drive clears its flag, as the patched store did");
    Call none = area_read(0u);
    decline(none.memory, request(none.memory, none.gpr));
    expect(none.memory.bytes.empty(), "without a flag nothing is written");
}

void test_briefing_heard() {
    Memory memory;
    memory.words[kBriefingCount] = 133u;
    expect(briefing_heard(memory, true), "the voice plays under the briefing: the drive keeps its pace");
    expect(!briefing_heard(memory, false), "once the voice ends the rest of the load is served");
    memory.words[kBriefingCount] = static_cast<uint32_t>(-200);
    expect(briefing_heard(memory, true), "a language disc's longer wait sends the count below zero");
    memory.words[kBriefingCount] = 541u;
    expect(!briefing_heard(memory, true), "in the mission the voice is another sound");
}

}  // namespace

int main() {
    test_request();
    test_servable();
    test_complete();
    test_decline();
    test_briefing_heard();
    if (g_failures != 0) return 1;
    std::cout << "disruptor streamed reads: PASS\n";
    return 0;
}
