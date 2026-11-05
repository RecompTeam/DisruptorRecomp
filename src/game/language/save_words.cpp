#include "game/language/save_words.h"

#include "cpu_state.h"
#include "mod_plugins.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

namespace words = disruptor::save_words;

constexpr std::array<uint32_t, 2> kTextRoutines = {0x80044A10u, 0x80044BDCu};  /* $a0: the string */
constexpr int kString = 4;

void text_entry(CPUState *cpu, uint32_t) {
    if (!cpu) return;
    const uint32_t at = cpu->gpr[kString];
    if (at == 0u) return;
    std::array<char, words::longest() + 1> text{};
    size_t length = 0;
    for (; length < text.size(); ++length) {
        const auto byte = static_cast<char>(psx_mod_read_byte(at + static_cast<uint32_t>(length)));
        if (byte == '\0') break;
        text[length] = byte;
    }
    if (length == text.size()) return;
    const std::string_view saves = words::replacement({text.data(), length});
    if (saves.empty()) return;
    for (size_t index = 0; index < saves.size(); ++index)
        psx_mod_write_byte(at + static_cast<uint32_t>(index), static_cast<uint8_t>(saves[index]));
    psx_mod_write_byte(at + static_cast<uint32_t>(saves.size()), 0u);
}

}  // namespace

PSX_MOD_CONSTRUCTOR(register_disruptor_save_words) {
    for (const uint32_t routine : kTextRoutines)
        if (!psx_mod_register_function_entry_plugin("disruptor.save_words.text", routine, text_entry))
            std::fprintf(stderr, "disruptor: failed to register the save words hook at 0x%08X\n", routine);
}
