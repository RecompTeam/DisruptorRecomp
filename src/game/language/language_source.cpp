/*
 * Stands the language disc in front of the game disc.
 *
 * The player names an image of another region's disc. When the runtime sets
 * its save states up, or at the latest the first time the drive reads the
 * game disc, the two are laid out as one (see language_disc.cpp),
 * and from then on every sector comes from that layout. A disc that is not a
 * known version, or does not fit, is refused whole and the game disc is read
 * as it is.
 */

#include "game/language/language.h"

#include "game/language/language_disc.h"
#include "iso_overlay.h"
#include "mod_plugins.h"
#include "savestate.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

class IsoImage final : public disruptor::DiscImage {
  public:
    explicit IsoImage(void *handle) : handle_(handle) {}
    bool raw_sector(uint32_t lba, uint8_t *raw) override { return iso_read_raw_sector_plain(handle_, lba, raw) != 0; }
    uint32_t sectors() override { return iso_sector_count_plain(handle_); }

  private:
    void *handle_;
};

enum class State { kUnbuilt, kOn, kRefused };

std::string g_path;
void *g_game = nullptr;
State g_state = State::kUnbuilt;
std::unique_ptr<IsoImage> g_home;
std::unique_ptr<IsoImage> g_other;
disruptor::LanguageDisc g_disc;

bool g_states_apart = false;

/* A save state holds the game's copy of the disc's table of WAD.IN: a laid out disc's states get a folder of their own, once. */
void keep_states_apart() {
    if (g_states_apart) return;
    uint32_t bios = 0, entry = 0;
    savestate_get_integrity(&bios, &entry);
    const char *folder = savestate_dir();
    if (entry == 0u || !folder || folder[0] == '\0') return;
    savestate_configure((std::string(folder) + "/" + g_disc.language()).c_str(), bios, entry);
    g_states_apart = true;
}

bool laid_out(void *handle) {
    if (handle == g_game && g_state != State::kUnbuilt) {
        if (g_state == State::kOn) keep_states_apart();
        return g_state == State::kOn;
    }
    g_game = handle;
    g_state = State::kRefused;
    std::string why = "the image could not be opened";
    if (!g_other) {
        if (void *other = iso_open_plain(g_path.c_str())) g_other = std::make_unique<IsoImage>(other);
    }
    g_home = std::make_unique<IsoImage>(handle);
    if (g_other && g_disc.build(*g_home, *g_other, why)) {
        g_state = State::kOn;
        keep_states_apart();
    } else {
        std::fprintf(stderr, "disruptor: the language disc %s was refused: %s\n", g_path.c_str(), why.c_str());
    }
    return g_state == State::kOn;
}

int read(void *handle, uint32_t lba, uint8_t *raw) {
    if (!laid_out(handle)) return 0;
    return g_disc.raw_sector(lba, raw) ? 1 : -1;
}

uint32_t sectors(void *handle) { return laid_out(handle) ? g_disc.sectors() : 0u; }

void closed(void *handle) {
    if (handle != g_game) return;
    g_game = nullptr;
    g_state = State::kUnbuilt;
}

const PsxIsoOverlay kOverlay = {read, sectors, closed};

PSX_MOD_CONSTRUCTOR(register_disruptor_language_source) {
    disruptor_language_set_disc(std::getenv("PSX_DISRUPTOR_LANGUAGE_DISC"));
}

}  // namespace

extern "C" void disruptor_language_set_disc(const char *path) {
    if (!path || path[0] == '\0' || !g_path.empty()) return;
    g_path = path;
    iso_set_overlay(&kOverlay);
}

extern "C" void disruptor_language_settle(void) {
    if (void *game = iso_game_disc(); game && !g_path.empty()) (void)laid_out(game);
}

extern "C" int disruptor_language_disc_tall_hints(void) { return g_state == State::kOn && g_disc.tall_hints() ? 1 : 0; }

extern "C" int disruptor_language_disc_pack(const uint8_t **data, uint32_t *size) {
    if (g_state != State::kOn || g_disc.pack().empty()) return 0;
    *data = g_disc.pack().data();
    *size = static_cast<uint32_t>(g_disc.pack().size());
    return 1;
}
