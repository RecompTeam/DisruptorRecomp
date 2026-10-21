#include "cpu_state.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

extern "C" {
uint32_t g_psx_cyc_overclock_shift = 0;
uint32_t g_psx_cyc_overclock_carry = 0;
int g_ls_replay_active = 0;
int g_event_step_conservative = 0;
int psx_in_device_service = 0;
uint32_t g_psx_cyc_batch = 0;
uint32_t g_psx_cyc_batch_limit = 0;
int g_psx_cyc_bb_defer = 0;
}

namespace {

struct Clock {
    uint64_t published = 0;
    std::vector<uint64_t> log;
};

Clock g_clock;

uint32_t room() { return static_cast<uint32_t>(1u + g_clock.published % 70u); }

void publish(uint32_t cycles) {
    g_clock.published += static_cast<uint64_t>(g_psx_cyc_batch) + cycles;
    g_psx_cyc_batch = 0;
    g_psx_cyc_batch_limit = 0;
    g_clock.log.push_back(g_clock.published);
}

}  // namespace

extern "C" void psx_cyc_charge_slow(uint32_t cycles) {
    if (g_ls_replay_active || g_event_step_conservative) {
        publish(cycles);
        return;
    }
    if (psx_in_device_service) {
        g_clock.published += cycles;
        g_clock.log.push_back(g_clock.published | (1ull << 63));
        return;
    }
    const uint32_t prior = g_psx_cyc_batch;
    const uint32_t sum = prior + cycles;
    if (sum >= prior) {
        if (g_psx_cyc_bb_defer > 0) {
            g_psx_cyc_batch = sum;
            return;
        }
        if (prior == 0u) g_psx_cyc_batch_limit = room() < 64u ? room() : 64u;
        if (sum < g_psx_cyc_batch_limit) {
            g_psx_cyc_batch = sum;
            return;
        }
    }
    publish(cycles);
}

namespace {

struct Machine {
    CPUState cpu{};
    uint32_t carry = 0, batch = 0, limit = 0;
    Clock clock;
};

void load(const Machine& machine) {
    g_psx_cyc_overclock_carry = machine.carry;
    g_psx_cyc_batch = machine.batch;
    g_psx_cyc_batch_limit = machine.limit;
    g_clock = machine.clock;
}

void save(Machine& machine) {
    machine.carry = g_psx_cyc_overclock_carry;
    machine.batch = g_psx_cyc_batch;
    machine.limit = g_psx_cyc_batch_limit;
    machine.clock = g_clock;
}

bool same_timing(const CPUState& a, const CPUState& b) {
    return std::memcmp(a.read_absorb, b.read_absorb, sizeof(a.read_absorb)) == 0 &&
           a.read_absorb_which == b.read_absorb_which && a.read_fudge == b.read_fudge &&
           a.ld_which_t == b.ld_which_t && a.ld_absorb == b.ld_absorb;
}

}  // namespace

int main() {
    std::mt19937 random(20261019u);
    auto pick = [&](uint32_t below) { return static_cast<uint32_t>(random() % below); };
    int failures = 0;
    for (int trial = 0; trial < 200000; ++trial) {
        Machine start;
        for (auto& slot : start.cpu.read_absorb) slot = static_cast<uint8_t>(pick(3) == 0 ? 0 : pick(6));
        start.cpu.read_absorb_which = static_cast<uint8_t>(pick(4) == 0 ? 0 : pick(32));
        start.cpu.read_fudge = static_cast<uint8_t>(pick(2) == 0 ? 0x20 : pick(32));
        start.cpu.ld_which_t = static_cast<uint8_t>(pick(2) == 0 ? 0x20 : pick(32));
        start.cpu.ld_absorb = pick(8);
        g_psx_cyc_overclock_shift = pick(6) == 0 ? 1u + pick(2) : 0u;
        start.carry = g_psx_cyc_overclock_shift ? pick(1u << g_psx_cyc_overclock_shift) : 0u;
        g_ls_replay_active = pick(20) == 0;
        g_event_step_conservative = pick(20) == 0;
        psx_in_device_service = pick(20) == 0;
        g_psx_cyc_bb_defer = pick(4) == 0;
        start.batch = pick(3) == 0 ? 0u : pick(70);
        start.limit = pick(3) == 0 ? 0u : start.batch + pick(8);
        start.clock.published = pick(1000);

        const unsigned count = 2u + pick(3);
        uint32_t masks[4] = {};
        for (unsigned k = 0; k < count; ++k) {
            for (int bit = 0; bit < 3; ++bit) {
                if (pick(2)) masks[k] |= 1u << (pick(3) == 0 ? start.cpu.read_absorb_which : pick(32));
            }
        }

        Machine one_by_one = start;
        load(one_by_one);
        for (unsigned k = 0; k < count; ++k) psx_cyc_step_inline(&one_by_one.cpu, masks[k]);
        save(one_by_one);

        Machine fused = start;
        load(fused);
        psx_cyc_run_inline(&fused.cpu, masks, count);
        save(fused);

        if (!same_timing(one_by_one.cpu, fused.cpu) || one_by_one.carry != fused.carry ||
            one_by_one.batch != fused.batch || one_by_one.limit != fused.limit ||
            one_by_one.clock.published != fused.clock.published || one_by_one.clock.log != fused.clock.log) {
            if (++failures <= 5) std::cerr << "FAIL: trial " << trial << " of " << count << " steps differs\n";
        }
    }
    if (failures != 0) {
        std::cerr << failures << " trials differ\n";
        return 1;
    }
    std::cout << "PSX fused cycle steps tests passed\n";
    return 0;
}
