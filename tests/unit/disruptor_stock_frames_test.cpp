#include "../../src/game/frame/stock_frames.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace disruptor::stock_frames;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Memory {
    std::map<uint32_t, uint8_t> bytes;
    uint32_t byte(uint32_t address) const {
        const auto found = bytes.find(address);
        return found == bytes.end() ? 0u : found->second;
    }
    uint32_t word(uint32_t address) const {
        return byte(address) | byte(address + 1u) << 8 | byte(address + 2u) << 16 | byte(address + 3u) << 24;
    }
    void write_byte(uint32_t address, uint8_t value) { bytes[address] = value; }
    void write(uint32_t address, uint32_t value) {
        for (uint32_t at = 0; at < 4u; ++at) bytes[address + at] = static_cast<uint8_t>(value >> (8u * at));
    }
    void text(const char *value) {
        for (uint32_t at = 0; at < kMessageBytes; ++at)
            write_byte(kMessage + at, at < std::strlen(value) ? static_cast<uint8_t>(value[at]) : 0u);
    }
    std::string text() const {
        std::string value;
        for (uint32_t at = 0; at < kMessageBytes && byte(kMessage + at) != 0u; ++at) value += static_cast<char>(byte(kMessage + at));
        return value;
    }
};

void cadence() {
    Cadence cadence;
    expect(stock_frame(cadence, 7241u), "a first frame on an odd count is a stock frame");
    frame_ends(cadence, 7241u);
    expect(!stock_frame(cadence, 7242u), "a tick that leaves the count even ends no stock frame");
    expect(!stock_frame(cadence, 7243u), "and the frame keeps its decision whatever is asked later");
    frame_ends(cadence, 7242u);
    expect(stock_frame(cadence, 7243u), "a tick that leaves it odd does");
    frame_ends(cadence, 7243u);
    expect(stock_frame(cadence, 7245u), "two ticks a frame are a stock frame");
    frame_ends(cadence, 7245u);
    expect(!stock_frame(cadence, 7245u), "a frame without a tick takes turns with the frame before it");
    frame_ends(cadence, 7245u);
    expect(stock_frame(cadence, 7245u), "and the next one with it");
    frame_ends(cadence, 7245u);
    expect(stock_frame(cadence, 0u), "a count set back to 0 by a new level is a stock frame");
    forget(cadence);
    expect(!stock_frame(cadence, 42u), "the first frame after a loaded state goes by the count");
    Cadence even;
    expect(!stock_frame(even, 7240u), "so does a first frame on an even count");
    frame_ends(even, 7240u);
    expect(stock_frame(even, 7242u), "and two ticks after it, ending on an even count, are a stock frame");
}

void calls() {
    Cadence cadence;
    expect(first_call(cadence, 0x800410F8u), "a frame's first call from a site is taken");
    expect(!first_call(cadence, 0x800410F8u), "the hook run again for the same call is not");
    expect(first_call(cadence, 0x80042184u), "a call from the other site is");
    frame_ends(cadence, 1u);
    expect(first_call(cadence, 0x800410F8u), "the next frame takes the first site again");
    for (uint32_t site = 1u; site < kMostCalls; ++site) first_call(cadence, site);
    expect(!first_call(cadence, 0x12345678u), "past the most sites a frame keeps, a call is not taken");
}

void explosion() {
    Memory memory;
    expect(!explosion_steps(memory), "no explosion takes no step");
    memory.write_byte(kExplosion, 48u);
    expect(explosion_steps(memory), "an explosion with no effect before it takes a step");
    for (const uint32_t effect : kFirstEffects) {
        memory.write_byte(effect, 1u);
        expect(!explosion_steps(memory), "each first effect ends the run before the explosion");
        memory.write_byte(effect, 0u);
    }
    for (const uint32_t effect : kLastEffects) {
        memory.write_byte(effect, 3u);
        expect(!explosion_steps(memory), "and so does each last one");
        memory.write_byte(effect, 0u);
    }
    memory.write_byte(kPlayerState, 4u);
    expect(!explosion_steps(memory), "and the player's state 4");
    memory.write_byte(kPlayerState, 2u);
    memory.write_byte(kShortEffect, 29u);
    expect(!explosion_steps(memory), "and the short effect under 30");
    memory.write_byte(kShortEffect, 30u);
    expect(explosion_steps(memory), "but not from 30 on");
    memory.write_byte(kShortEffect, 0u);
    memory.write_byte(kExplosion, 0xFFu);
    expect(!explosion_steps(memory), "a count that would wrap is left alone");
    memory.write_byte(kExplosion, 9u);
    expect(!pace_explosion(memory, true) && memory.byte(kExplosion) == 9u, "a stock frame keeps its step");
    expect(pace_explosion(memory, false) && memory.byte(kExplosion) == 10u,
           "another frame is given the step it takes, and says the memory holds it for the frame");
    memory.write_byte(kFirstEffects[2], 1u);
    expect(!pace_explosion(memory, false) && memory.byte(kExplosion) == 10u, "nothing is given when the effects take nothing");
}

constexpr uint32_t kSoundsSite = 0x80043AD8u;
constexpr uint32_t kSoundBusy = 12u;

uint32_t age_of(const Memory &memory, uint32_t slot) { return memory.word(kSoundSlots + slot * kSoundSlotBytes + kSoundAge); }

/* The retail sound manager's ageing (0x80014228 to 0x800144E4) for a sound that does not loop, its voice on or not:
 * true when it cut `slot`. A voice still on takes its slot again, as after the stop's key off (0x80014328). */
bool game_sounds(Memory &memory, uint32_t slot, bool voice_on) {
    const uint32_t at = kSoundSlots + slot * kSoundSlotBytes;
    if (!voice_on && memory.byte(at + kSoundBusy) == 0u) return false;
    if (voice_on && static_cast<int32_t>(memory.word(at + kSoundAge)) >= 241) {
        memory.write_byte(at + kSoundBusy, 0u);
        memory.write(at + kSoundAge, 0u);
        return true;
    }
    if (voice_on) memory.write_byte(at + kSoundBusy, 1u);
    memory.write(at + kSoundAge, memory.word(at + kSoundAge) + 1u);
    return false;
}

/* The frames from a sound's start to its cut, with `ticks` a frame, the hook run `hook_runs` times a call and every
 * `missed`th frame's wait not seen. */
uint32_t sound_frames(uint32_t ticks, int hook_runs, bool busy, uint32_t missed = 0u) {
    Memory memory;
    Frames frames;
    memory.write(kTicks, 7241u);
    memory.write_byte(kSoundSlots + 3u * kSoundSlotBytes + kSoundBusy, busy ? 1u : 0u);
    for (uint32_t frame = 1; frame < 2000u; ++frame) {
        frame_over(memory, frames, false);
        memory.write(kTicks, memory.word(kTicks) + ticks);
        if (ticks == 1u)
            for (int run = 0; run < hook_runs; ++run) sounds_begin(memory, frames, kSoundsSite);
        if (game_sounds(memory, 3u, true)) return frame;
        if (missed == 0u || frame % missed != 0u) frame_over(memory, frames, true);
    }
    return 0u;
}

void sounds() {
    const uint32_t stock = sound_frames(2u, 1, true);
    expect(stock == 242u, "the stock loop cuts a sound at its 242nd drawn frame");
    expect(sound_frames(1u, 1, true) == 2u * stock - 1u, "under 60 FPS it lives as long, within a VBlank");
    expect(sound_frames(1u, 2, true) == 2u * stock - 1u, "a hook run again for one call sets nothing back twice");
    expect(sound_frames(1u, 1, false) == 2u * stock - 1u, "nor does a voice still on that takes its slot again");
    expect(sound_frames(1u, 1, true, 5u) == 2u * stock - 1u, "nor a frame whose wait is not seen");

    Memory memory;
    Frames frames;
    memory.write(kTicks, 7241u);
    frame_ends(frames.cadence, 7241u);
    memory.write(kTicks, 7242u);
    memory.write(kSoundSlots + kSoundAge, 30u);
    memory.write(kSoundSlots + kSoundSlotBytes + kSoundAge, 30u);
    sounds_begin(memory, frames, kSoundsSite);
    expect(saving_deferred(memory, frames), "a state waits while the manager's steps are to be set back");
    memory.write(kSoundSlots + kSoundAge, 31u);
    memory.write(kSoundSlots + kSoundSlotBytes + kSoundAge, 0u);
    frame_over(memory, frames, true);
    expect(age_of(memory, 0u) == 30u && age_of(memory, 1u) == 0u, "the frame's end sets a step back and leaves a freed slot");
    expect(!saving_deferred(memory, frames), "and ends the wait");

    memory.write(kTicks, 7243u);
    sounds_begin(memory, frames, kSoundsSite);
    memory.write(kSoundSlots + kSoundAge, 31u);
    frame_over(memory, frames, true);
    expect(age_of(memory, 0u) == 31u && !saving_deferred(memory, frames), "a stock frame keeps the manager's step");
}

constexpr uint32_t kTeleport = 0x80077637u, kEffectsSite = 0x800410F8u;

/* The screen effects as the retail code runs them (0x8003FE6C to 0x8004021C), written out apart from the hook's own test. */
void game_effects(Memory &memory) {
    if (memory.byte(0x80077636u) != 0u) return;
    if (memory.byte(0x80077634u) != 0u) return;
    if (memory.byte(0x80077637u) != 0u) return;
    if (memory.byte(0x8007145Cu) == 4u) return;
    const uint32_t shortly = memory.byte(0x80077647u);
    if (shortly != 0u && shortly < 30u) return;
    if (memory.byte(0x80077649u) != 0u) return;
    if (memory.byte(0x8007764Au) != 0u) return;
    const uint32_t left = memory.byte(0x8007763Bu);
    if (left != 0u) memory.write_byte(0x8007763Bu, static_cast<uint8_t>(left - 1u));
}

struct Reactor {
    uint32_t ended = 0;                     /* the tick of the teleport, 0 when the player died first */
    std::map<uint32_t, uint32_t> explosion; /* the explosion's count after each drawn frame, by its last tick */
};

/* The reactor's end: the count ends after tick 7779, the teleport comes 90 ticks later, the effects run once a drawn frame
 * (twice where a hook is run again for one call), and the frame loop takes the player at 1. */
Reactor reactor(uint32_t ticks_a_frame, bool paced, int hook_runs = 1) {
    Memory memory;
    Frames frames;
    Reactor seen;
    uint32_t ticks = 7779u, teleport = 90u;
    memory.write(kTicks, ticks);
    frame_over(memory, frames, true);
    memory.write_byte(kExplosion, 48u);
    for (int frame = 0; frame < 400; ++frame) {
        for (uint32_t tick = 0; tick < ticks_a_frame; ++tick) {
            memory.write(kTicks, ++ticks);
            if (teleport != 0u && --teleport == 0u) memory.write_byte(kTeleport, 1u);
        }
        for (int run = 0; paced && run < hook_runs; ++run) effects_begin(memory, frames, kEffectsSite);
        game_effects(memory);
        seen.explosion[ticks] = memory.byte(kExplosion);
        if (memory.byte(kExplosion) == 1u) return seen;
        if (memory.byte(kTeleport) != 0u) {
            seen.ended = ticks;
            return seen;
        }
        frame_over(memory, frames, true);
    }
    return seen;
}

void race() {
    const Reactor stock = reactor(2u, false), fast = reactor(1u, false), paced = reactor(1u, true), again = reactor(1u, true, 2);
    expect(stock.ended == 7869u, "the stock loop teleports the player at tick 7869");
    expect(stock.explosion.count(7869u) == 1u && stock.explosion.at(7869u) == 4u, "with the explosion at 4");
    expect(fast.ended == 0u, "a frame each tick lets the explosion kill the player first");
    expect(paced.ended == stock.ended, "paced, the teleport comes at its stock tick and the player lives");
    bool same = paced.explosion.size() == 2u * stock.explosion.size();
    for (const auto &[tick, left] : stock.explosion)
        same = same && paced.explosion.count(tick) == 1u && paced.explosion.at(tick) == left;
    expect(same, "and after each tick the stock loop draws, the explosion stands where the stock loop has it");
    expect(again.ended == stock.ended && again.explosion == paced.explosion, "an effects hook run twice for one call paces once");
}

void message() {
    Memory memory;
    Shown shown;
    memory.text("30 SECONDS");
    memory.write(kMessageTime, 666u);
    show_message(memory, shown, false);
    expect(shown.holding && memory.text() == "30 SECONDS" && memory.word(kMessageTime) == 666u,
           "before any stock frame, a frame keeps its own message, so that its draw takes no step from the time");
    memory.write(kMessageTime, drawn(read_message(memory)).time);
    give_back(memory, shown);
    expect(memory.word(kMessageTime) == 666u, "and the time is given back after the draw");
    show_message(memory, shown, true);
    memory.write(kMessageTime, drawn(read_message(memory)).time);
    give_back(memory, shown);
    expect(memory.word(kMessageTime) == 665u, "a stock frame's draw takes its own step from the time");

    const auto another = [&](const char *text, uint32_t time) {
        memory.text(text);
        memory.write(kMessageTime, time);
        show_message(memory, shown, false);
    };
    another("HEALTH BOOST NOT NEEDED", 60u);
    expect(memory.text() == "30 SECONDS" && memory.word(kMessageTime) == 666u, "another frame draws the stock frame's message");
    memory.write(kMessageTime, drawn(read_message(memory)).time);
    give_back(memory, shown);
    expect(memory.text() == "HEALTH BOOST NOT NEEDED" && memory.word(kMessageTime) == 60u,
           "and its own is back after the draw, its time untaken");

    another("HEALTH BOOST NOT NEEDED", 60u);
    give_back(memory, shown);
    expect(memory.text() == "HEALTH BOOST NOT NEEDED" && memory.word(kMessageTime) == 60u,
           "a state written before the draw gets the real message back too");

    another("HEALTH BOOST NOT NEEDED", 60u);
    memory.write(kMessageTime, drawn(read_message(memory)).time);
    memory.text("PICKED UP");
    give_back(memory, shown);
    expect(memory.text() == "PICKED UP" && memory.word(kMessageTime) == 665u, "a message written after the draw is the game's, whole");

    another("HEALTH BOOST NOT NEEDED", 60u);
    memory.write(kMessageTime, 1234u);
    give_back(memory, shown);
    expect(memory.text() == "30 SECONDS" && memory.word(kMessageTime) == 1234u, "and so is a time written after it");

    memory.text("PICKED UP");
    memory.write(kMessageTime, 70u);
    show_message(memory, shown, false);
    memory.write(kMessageTime, drawn(read_message(memory)).time);
    show_message(memory, shown, true);
    expect(!shown.holding && memory.text() == "PICKED UP" && memory.word(kMessageTime) == 70u && shown.kept == read_message(memory),
           "a draw that comes again without a wait first gives back what the last one showed in place");
    forget(shown);
    expect(!shown.seen && !shown.holding, "a loaded state forgets the kept message");
}

/* The reactor's count writes its message each tick, a pickup's the even ticks after it, and the message's draw shows the
 * buffer once a frame: what each frame shows. */
std::string reactor_messages(uint32_t ticks, uint32_t ticks_a_frame, bool paced) {
    Memory memory;
    Cadence cadence;
    Shown shown;
    frame_ends(cadence, ticks);
    std::string seen;
    for (int frame = 0; frame < 8; ++frame) {
        for (uint32_t tick = 0; tick < ticks_a_frame; ++tick) {
            memory.write(kTicks, ++ticks);
            memory.text("30 SECONDS");
            memory.write(kMessageTime, 666u);
            if ((ticks & 1u) == 0u) {
                memory.text("NOT NEEDED");
                memory.write(kMessageTime, 60u);
            }
        }
        if (paced) show_message(memory, shown, stock_frame(cadence, ticks));
        seen += memory.text() == "30 SECONDS" ? 'S' : 'N';
        memory.write(kMessageTime, drawn(read_message(memory)).time);
        give_back(memory, shown);
        frame_ends(cadence, ticks);
    }
    return seen;
}

void flicker() {
    expect(reactor_messages(3659u, 2u, false) == "SSSSSSSS", "the stock loop, its frames ending on odd ticks, shows the count's message");
    expect(reactor_messages(3660u, 1u, false) == "SNSNSNSN", "a frame each tick shows the two in turn");
    expect(reactor_messages(3660u, 1u, true) == "SSSSSSSS", "paced, each frame shows the count's message");
}

/* A message's time paced, frame by frame. */
void counted_message() {
    Memory memory;
    Cadence cadence;
    Shown shown;
    uint32_t ticks = 100u;
    frame_ends(cadence, ticks);
    memory.text("ZODIAC NOT NEEDED");
    memory.write(kMessageTime, 60u);
    std::vector<uint32_t> times;
    while (memory.word(kMessageTime) != 0u && times.size() < 400u) {
        memory.write(kTicks, ++ticks);
        show_message(memory, shown, stock_frame(cadence, ticks));
        memory.write(kMessageTime, drawn(read_message(memory)).time);
        give_back(memory, shown);
        frame_ends(cadence, ticks);
        times.push_back(memory.word(kMessageTime));
    }
    expect(times.size() == 119u && times[0] == 59u && times[1] == 59u && times[2] == 58u,
           "a message of 60 drawn frames loses one at each stock frame, 119 ticks to its last one");
}


void held() {
    Memory memory;
    Frames frames;
    memory.write(kTicks, 7241u);
    frame_over(memory, frames, true);
    memory.write(kTicks, 7242u);
    memory.write_byte(kExplosion, 20u);
    effects_begin(memory, frames, kEffectsSite);
    expect(memory.byte(kExplosion) == 21u && saving_deferred(memory, frames), "a state waits from the effects' hook");
    game_effects(memory);
    expect(memory.byte(kExplosion) == 20u && !saving_deferred(memory, frames), "until the effects have taken their step");
    effects_begin(memory, frames, kEffectsSite);
    expect(memory.byte(kExplosion) == 20u && !saving_deferred(memory, frames), "the hook run again for one call holds nothing");

    frame_over(memory, frames, true);
    memory.write(kTicks, 7243u);
    memory.text("30 SECONDS");
    memory.write(kMessageTime, 666u);
    message_begins(memory, frames, 0x80043CA4u);
    frame_over(memory, frames, true);
    memory.write(kTicks, 7244u);
    memory.text("NOT NEEDED");
    memory.write(kMessageTime, 60u);
    message_begins(memory, frames, 0x80043CA4u);
    expect(memory.text() == "30 SECONDS" && saving_deferred(memory, frames), "a state waits while a message is drawn in place");
    memory.write(kMessageTime, drawn(read_message(memory)).time);
    expect(saving_deferred(memory, frames), "after its time is taken too, as the draw reads its text later");
    message_begins(memory, frames, 0x80043CA4u);
    expect(frames.shown.holding && memory.text() == "30 SECONDS" && memory.word(kMessageTime) == 665u,
           "a second call of the draw in one frame does nothing");
    state_saving(memory, frames);
    expect(memory.text() == "NOT NEEDED" && memory.word(kMessageTime) == 60u, "a state written anyway gets the real message");
    message_begins(memory, frames, 0x80043CA4u);
    memory.write(kMessageTime, drawn(read_message(memory)).time);
    frame_over(memory, frames, true);
    expect(!saving_deferred(memory, frames), "the frame's wait ends the hold");

    memory.write(kTicks, 7245u);
    frame_over(memory, frames, true);
    memory.write(kTicks, 7246u);
    memory.write_byte(kExplosion, 20u);
    effects_begin(memory, frames, kEffectsSite);
    frame_over(memory, frames, false);
    expect(!saving_deferred(memory, frames), "a tick's start ends any hold");
    effects_begin(memory, frames, kEffectsSite);
    forget(frames);
    expect(!saving_deferred(memory, frames), "and so does a loaded state");
}

/* The message's text as each frame shows it and the message the game has after each frame, from a stock frame that
 * leaves the time at 1, with and without a state written at its wait and loaded at once. */
std::vector<std::pair<std::string, Message>> fading(bool loaded) {
    Memory memory;
    Frames frames;
    uint32_t ticks = 501u;
    memory.write(kTicks, ticks);
    memory.text("PICKED UP");
    memory.write(kMessageTime, 2u);
    std::vector<std::pair<std::string, Message>> seen;
    for (int frame = 0; frame < 4; ++frame) {
        message_begins(memory, frames, 0x80043CA4u);
        const std::string shown = memory.word(kMessageTime) != 0u ? memory.text() : "";
        memory.write(kMessageTime, drawn(read_message(memory)).time);
        frame_over(memory, frames, true);
        seen.emplace_back(shown, read_message(memory));
        if (loaded && frame == 0) {
            state_saving(memory, frames);
            forget(frames);
        }
        memory.write(kTicks, ++ticks);
    }
    return seen;
}

void loads() {
    expect(fading(true) == fading(false), "a state written at a stock frame's wait and loaded shows the message as the run that went on");
    for (const uint32_t ran : {1u, 2u}) {
        Memory memory;
        Frames frames, again;
        memory.write(kTicks, 100u);
        frame_over(memory, frames, true);
        memory.write(kTicks, 101u);
        stock_frame(frames.cadence, 101u);
        frame_over(memory, frames, true);
        loaded(memory, again);
        memory.write(kTicks, 101u + ran);
        expect(stock_frame(again.cadence, 101u + ran) == stock_frame(frames.cadence, 101u + ran),
               "a state written at a wait and loaded decides its next frame, of one tick or two, as the run that went on");
    }
}

} // namespace

int main() {
    cadence();
    calls();
    explosion();
    sounds();
    race();
    message();
    flicker();
    counted_message();
    held();
    loads();
    if (g_failures != 0) return 1;
    std::cout << "stock frames: PASS\n";
    return 0;
}
