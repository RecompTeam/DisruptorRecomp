#include "game/language/save_words.h"

#include "cpu_state.h"
#include "mod_plugins.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

constexpr uint32_t kGuest = 0x80010000u;
uint8_t g_guest[256];
int g_writes = 0;
uint32_t g_hooked[4];
PSXModFunctionEntryCallback g_callbacks[4];
int g_hooks = 0;

void put(uint32_t at, const char *text, size_t room) {
    std::memset(g_guest + (at - kGuest), 0xAA, room);
    std::memcpy(g_guest + (at - kGuest), text, std::strlen(text) + 1);
}

std::string read(uint32_t at) { return reinterpret_cast<const char *>(g_guest + (at - kGuest)); }

void draw(uint32_t routine, uint32_t at) {
    CPUState cpu{};
    cpu.gpr[4] = at;
    for (int index = 0; index < g_hooks; ++index)
        if (g_hooked[index] == routine) g_callbacks[index](&cpu, routine);
}

}  // namespace

int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    if (g_hooks == 4) return 0;
    g_hooked[g_hooks] = address;
    g_callbacks[g_hooks++] = callback;
    return 1;
}

uint8_t psx_mod_read_byte(uint32_t address) {
    return address >= kGuest && address - kGuest < sizeof(g_guest) ? g_guest[address - kGuest] : 0u;
}

void psx_mod_write_byte(uint32_t address, uint8_t value) {
    ++g_writes;
    if (address >= kGuest && address - kGuest < sizeof(g_guest)) g_guest[address - kGuest] = value;
}

int main() {
    expect(g_hooks == 2 && g_hooked[0] == 0x80044A10u && g_hooked[1] == 0x80044BDCu, "both text renderers are hooked at load");
    constexpr uint32_t kText = kGuest + 16u;
    put(kText, "COULD NOT READ MEMORY CARD", 40);
    draw(0x80044BDCu, kText);
    expect(read(kText) == "COULD NOT READ SAVES", "the menu font's card text is said of saves, ended");
    expect(g_guest[kText - kGuest + 22] == 'C' && g_guest[kText - kGuest + 27] == 0xAA,
           "nothing past the old text's end is written");
    const int writes = g_writes;
    draw(0x80044BDCu, kText);
    expect(g_writes == writes && read(kText) == "COULD NOT READ SAVES", "a replaced text is left alone after");
    put(kText, "FORMAT FAILED", 40);
    draw(0x80044A10u, kText);
    expect(read(kText) == "RESET FAILED", "the small font's card text is replaced too");
    put(kText, "NO MEMORY CARDS", 40);
    draw(0x80044A10u, kText);
    expect(read(kText) == "NO MEMORY CARDS", "a longer text that starts the same stays");
    put(kText, "GAME SAVES UNAVAILABLE", 40);
    draw(0x80044A10u, kText);
    expect(read(kText) == "GAME SAVES UNAVAILABLE", "another text stays");
    std::memset(g_guest, 'A', sizeof(g_guest));
    const int before = g_writes;
    draw(0x80044A10u, kGuest);
    expect(g_writes == before, "a run of letters with no end in reach is left alone");
    draw(0x80044A10u, 0u);
    expect(g_writes == before, "a null string is left alone");
    if (g_failures) return 1;
    std::cout << "save words hooks: ok\n";
    return 0;
}
