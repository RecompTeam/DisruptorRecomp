#include "game/session/streamed_reads.h"

#include "cdrom.h"
#include "cpu_state.h"
#include "game/guest.h"
#include "game/session/diagnostics.h"
#include "mod_plugins.h"
#include "spu.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

namespace reads = disruptor::streamed_reads;

/* PSX_DISRUPTOR_INSTANT_STREAMING=0 leaves every read to the drive, to tell a fault of the adapter from the game's. */
bool g_enabled = true;

bool serve(disruptor::Guest &guest, const reads::Request &read) {
    std::vector<uint8_t> data(static_cast<size_t>(read.sectors) * reads::kSectorBytes);
    if (!cdrom_read_user_sectors(read.lba, read.sectors, data.data()) ||
        !psx_mod_dma_write_ram(read.buffer, data.data(), static_cast<uint32_t>(data.size()), static_cast<int>(read.lba)))
        return false;
    reads::complete(guest, read);
    return true;
}

bool briefing_voice_playing() {
    SpuVoiceState voice{};
    spu_get_voice_state(reads::kBriefingVoice, &voice);
    return voice.active != 0;
}

void read_entry(CPUState *cpu, uint32_t) {
    disruptor::Guest guest;
    const reads::Request read = reads::request(guest, cpu->gpr);
    const bool served = g_enabled && !reads::briefing_heard(guest, briefing_voice_playing()) && reads::servable(guest, read) &&
                        serve(guest, read);
    cpu->gpr[reads::kServed] = served ? 1u : 0u;
    if (!served) reads::decline(guest, read);
    if (served && disruptor_diagnostics_detailed())
        disruptor_diagnostics_note("streamed read served: sector %u, %u sectors into %08X for %08X", read.lba, read.sectors,
                                   read.buffer, cpu->gpr[31]);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_streamed_reads) {
    const char *forced = std::getenv("PSX_DISRUPTOR_INSTANT_STREAMING");
    g_enabled = !(forced && forced[0] == '0');
    if (psx_mod_register_function_entry_plugin("disruptor.streamed_reads", reads::kRead, read_entry)) return;
    std::fprintf(stderr, "disruptor: failed to register the streamed reads hook, the patched read at %08X needs it\n",
                 reads::kRead);
    std::abort();
}

}  // namespace
