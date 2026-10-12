#ifndef PSX_GAME_HOST_H
#define PSX_GAME_HOST_H

/* What the host asks of the game project it is built into. A project that defines PSX_HAS_GAME_HOST supplies every one. */

#include <stddef.h>
#include <stdint.h>

typedef struct CPUState CPUState;

#ifdef __cplusplus
namespace PSXRecompV4 { struct UserSettings; }
/* Called when the user's settings are read, before the drive first reads the disc. True asks for a boot without the BIOS animation. */
bool psx_game_settings_loaded(const PSXRecompV4::UserSettings &settings);
extern "C" {
#endif

/* Called when the disc's serial is known, `length` characters of it. 0 stops the start. */
int psx_game_configure(const char *game_id, size_t length);
/* Nonzero asks for a boot without the BIOS animation whatever the settings say. */
int psx_game_fast_boot_forced(void);
/* Called when save states have their folder, before any can be asked for. */
void psx_game_states_configured(void);
/* Called after a save state is restored. */
void psx_game_state_loaded(void);
/* A key went down or up. `plain` is set for a press without Ctrl, Alt or the system key and for every release. Nonzero when the game took a press. */
int psx_game_key(int scancode, uint32_t keycode, int down, int plain);
/* Called around the sampling of the pad, outside netplay. */
void psx_game_before_input(void);
void psx_game_after_input(void);
/* Nonzero when the four-cornered polygon whose command sits at `command_address` is the game's second copy of the one
 * before it, grown outwards. Then each corner's source address and source word are those of the first copy's corner. */
int psx_game_grown_quad_sources(uint32_t command_address, const uint32_t *packet, uint32_t source_address[4],
                                uint32_t source_word[4]);
/* Supplied by the generated code, from [[recompiler.instruction_hook]]: the game's calls for an instruction that ran from RAM. */
void psx_game_instruction_ran(CPUState *cpu, uint32_t address, uint32_t instruction);

#ifdef __cplusplus
}
#endif

#endif
