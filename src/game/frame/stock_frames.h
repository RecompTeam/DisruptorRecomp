#pragma once

#include <array>
#include <cstdint>

/* The stock loop draws once in two ticks, after an odd one, so a timer the game counts in drawn frames runs at half the
 * tick rate and what a tick between two drawn frames leaves is never seen. Under 60 FPS gameplay every tick is drawn:
 * these keep the reactor's explosion at its stock length and show, in a frame the stock loop would not draw, the HUD
 * message of the last frame it would. */
namespace disruptor::stock_frames {

inline constexpr uint32_t kTicks = 0x800716F8u; /* gp+1452, one more at each tick (0x80039B6C) */

inline constexpr uint32_t kExplosion = 0x8007763Bu;   /* set to 48 when the reactor's count ends (0x80039C94), the player dies at 1 */
inline constexpr uint32_t kPlayerState = 0x8007145Cu; /* gp+784 */
inline constexpr uint32_t kFirstEffects[] = {0x80077636u, 0x80077634u, 0x80077637u}; /* each ends a run of the effects before it */
inline constexpr uint32_t kShortEffect = 0x80077647u; /* ends the run while it is under 30 */
inline constexpr uint32_t kLastEffects[] = {0x80077649u, 0x8007764Au};

inline constexpr uint32_t kMessage = 0x80077694u;     /* the HUD's message (0x8004281C) */
inline constexpr uint32_t kMessageBytes = 68u;        /* up to the next variable, 0x800776D8 */
inline constexpr uint32_t kMessageTime = 0x80071418u; /* gp+716: shown while not 0, one less each drawn frame (0x800427EC) */

inline constexpr uint32_t kSoundSlots = 0x80073DD8u; /* 22 slots of 24 bytes, one a playing sound (0x80012A0C) */
inline constexpr uint32_t kSoundSlotCount = 22u, kSoundSlotBytes = 24u;
inline constexpr uint32_t kSoundAge = 4u; /* one more each call of the sound manager (0x800144C8), cut at 241 (0x800142A4) */

using Ages = std::array<uint32_t, kSoundSlotCount>;

inline constexpr uint32_t kMostCalls = 6u;

/* Which frames under 60 FPS gameplay the stock loop draws as well, and which hooked calls this frame has had. */
struct Cadence {
    uint32_t ticks = 0;   /* the tick count at the last frame's wait */
    bool counted = false; /* a frame has ended since the start or a loaded state */
    bool decided = false;
    bool stock = true;
    std::array<uint32_t, kMostCalls> calls{};
    uint32_t call_count = 0;
};

/* Decided once a frame: one of its ticks left the count odd, or it ran two or more. A frame without a tick takes turns.
 * The first frame after the start or a loaded state goes by the count alone. */
inline bool stock_frame(Cadence &cadence, uint32_t ticks) {
    if (!cadence.decided) {
        const uint32_t ran = ticks - cadence.ticks;
        const bool odd = (ticks & 1u) != 0u;
        cadence.stock = !cadence.counted ? odd : ran >= 2u ? true : ran == 1u ? odd : !cadence.stock;
        cadence.decided = true;
    }
    return cadence.stock;
}

/* Whether this is the frame's first call from `site`: an entry block cut short runs its entry hook again for one call. */
inline bool first_call(Cadence &cadence, uint32_t site) {
    for (uint32_t at = 0; at < cadence.call_count; ++at)
        if (cadence.calls[at] == site) return false;
    if (cadence.call_count == kMostCalls) return false;
    cadence.calls[cadence.call_count++] = site;
    return true;
}

inline void frame_ends(Cadence &cadence, uint32_t ticks) {
    cadence.ticks = ticks;
    cadence.counted = true;
    cadence.decided = false;
    cadence.call_count = 0;
}

inline void forget(Cadence &cadence) { cadence = Cadence{}; }

/* Whether this run of the screen effects (0x8003FE6C) takes a step from the explosion (0x8004021C): each effect it tries
 * first ends the run. */
template <class Memory>
bool explosion_steps(const Memory &memory) {
    for (const uint32_t effect : kFirstEffects)
        if (memory.byte(effect) != 0u) return false;
    if (memory.byte(kPlayerState) == 4u) return false;
    const uint32_t shortly = memory.byte(kShortEffect);
    if (shortly != 0u && shortly < 30u) return false;
    for (const uint32_t effect : kLastEffects)
        if (memory.byte(effect) != 0u) return false;
    const uint32_t left = memory.byte(kExplosion);
    return left != 0u && left != 0xFFu;
}

/* At the effects' entry: in a frame the stock loop does not draw, the step they are about to take is given first.
 * True when it was: the memory holds a value for this frame until its draw. */
template <class Memory>
bool pace_explosion(Memory &memory, bool stock) {
    if (stock || !explosion_steps(memory)) return false;
    memory.write_byte(kExplosion, static_cast<uint8_t>(memory.byte(kExplosion) + 1u));
    return true;
}

template <class Memory>
Ages read_ages(const Memory &memory) {
    Ages ages{};
    for (uint32_t slot = 0; slot < kSoundSlotCount; ++slot)
        ages[slot] = memory.word(kSoundSlots + slot * kSoundSlotBytes + kSoundAge);
    return ages;
}

/* At the end of a frame the stock loop does not draw, with the ages the sound manager (0x80013544, once a frame from the
 * loop) had at its entry: each age it stepped is set back, so a sound is cut at 241 stock frames, 8 seconds, as in the
 * stock game, not at 4. Any other change is the game's, a slot freed. */
template <class Memory>
void pace_sounds(Memory &memory, const Ages &kept) {
    for (uint32_t slot = 0; slot < kSoundSlotCount; ++slot) {
        const uint32_t at = kSoundSlots + slot * kSoundSlotBytes + kSoundAge;
        if (memory.word(at) == kept[slot] + 1u) memory.write(at, kept[slot]);
    }
}

struct Message {
    std::array<uint8_t, kMessageBytes> text{};
    uint32_t time = 0;
    bool operator==(const Message &) const = default;
};

/* The message of the last stock frame, and what a frame the stock loop does not draw had while it showed that one. */
struct Shown {
    Message kept;
    Message real;
    bool seen = false;
    bool holding = false;
};

template <class Memory>
Message read_message(const Memory &memory) {
    Message message;
    for (uint32_t at = 0; at < kMessageBytes; ++at) message.text[at] = static_cast<uint8_t>(memory.byte(kMessage + at));
    message.time = memory.word(kMessageTime);
    return message;
}

template <class Memory>
void write_message(Memory &memory, const Message &message) {
    for (uint32_t at = 0; at < kMessageBytes; ++at) memory.write_byte(kMessage + at, message.text[at]);
    memory.write(kMessageTime, message.time);
}

/* What the message's draw (0x800427D0) leaves of a message it is given: its time one less while it is shown. */
inline Message drawn(Message message) {
    if (message.time != 0u) --message.time;
    return message;
}

/* At the frame's wait, or before a state is written: the real message back while the memory holds the kept one, drawn
 * or not yet. Anything else there was written by the game since, and is the game's. */
template <class Memory>
void give_back(Memory &memory, Shown &shown) {
    if (!shown.holding) return;
    shown.holding = false;
    const Message now = read_message(memory);
    if (now == shown.kept || now == drawn(shown.kept)) write_message(memory, shown.real);
}

/* At the message's draw: a stock frame's message is kept, another frame draws the kept one in place of its own. With
 * none kept yet, after the start or a load, it keeps its own, so its draw takes no step from the time either. */
template <class Memory>
void show_message(Memory &memory, Shown &shown, bool stock) {
    give_back(memory, shown);
    if (stock || !shown.seen) {
        shown.kept = read_message(memory);
        shown.seen = true;
        if (stock) return;
    }
    shown.real = read_message(memory);
    write_message(memory, shown.kept);
    shown.holding = true;
}

inline void forget(Shown &shown) { shown = Shown{}; }

/* What a hook left in memory for the game to take within this frame: no state may be written before it has. */
struct Hold {
    bool explosion = false;
    uint8_t written = 0; /* the explosion's count as the effects' hook left it, one more than they will */
    bool sounds = false; /* the manager stepped the ages in a frame the stock loop does not draw */
    Ages ages{};         /* as they were at its entry */
};

/* The hooks' state: the frame's cadence, the kept message and what is held. */
struct Frames {
    Cadence cadence;
    Shown shown;
    Hold hold;
};

/* The effects' entry, called from `site`. */
template <class Memory>
void effects_begin(Memory &memory, Frames &frames, uint32_t site) {
    if (!first_call(frames.cadence, site)) return;
    if (pace_explosion(memory, stock_frame(frames.cadence, memory.word(kTicks)))) {
        frames.hold.explosion = true;
        frames.hold.written = static_cast<uint8_t>(memory.byte(kExplosion));
    }
}

/* The sound manager's entry, called from `site`. */
template <class Memory>
void sounds_begin(const Memory &memory, Frames &frames, uint32_t site) {
    if (!first_call(frames.cadence, site) || stock_frame(frames.cadence, memory.word(kTicks))) return;
    frames.hold.ages = read_ages(memory);
    frames.hold.sounds = true;
}

/* The message draw's entry, called from `site`. */
template <class Memory>
void message_begins(Memory &memory, Frames &frames, uint32_t site) {
    if (first_call(frames.cadence, site)) show_message(memory, frames.shown, stock_frame(frames.cadence, memory.word(kTicks)));
}

/* The frame loop's wait after its last draw, or a tick's start: the frame is over. A tick after the frame's hooked calls
 * ends a frame whose wait was not seen, so the next frame's calls are taken. */
template <class Memory>
void frame_over(Memory &memory, Frames &frames, bool waited) {
    give_back(memory, frames.shown);
    if (frames.hold.sounds) pace_sounds(memory, frames.hold.ages);
    frames.hold = Hold{};
    if (waited || frames.cadence.call_count != 0u) frame_ends(frames.cadence, memory.word(kTicks));
}

/* Whether a state asked for now waits: the explosion's count is still the one the hook gave and the effects not yet
 * took, the sounds' ages are to be set back at the frame's end, or a message is drawn in place: its draw reads the text
 * after it takes from the time, so to the frame's end. */
template <class Memory>
bool saving_deferred(const Memory &memory, Frames &frames) {
    if (frames.hold.explosion && memory.byte(kExplosion) != frames.hold.written) frames.hold.explosion = false;
    return frames.hold.explosion || frames.hold.sounds || frames.shown.holding;
}

template <class Memory>
void state_saving(Memory &memory, Frames &frames) { give_back(memory, frames.shown); }

inline void forget(Frames &frames) { frames = Frames{}; }

/* A loaded state counts from its tick count: written while the frame loop waits, as nearly all are, it is the count at
 * the last wait, and the next frame is decided as the run that went on decided it. */
template <class Memory>
void loaded(const Memory &memory, Frames &frames) {
    forget(frames);
    frames.cadence.ticks = memory.word(kTicks);
    frames.cadence.counted = true;
}

} // namespace disruptor::stock_frames
