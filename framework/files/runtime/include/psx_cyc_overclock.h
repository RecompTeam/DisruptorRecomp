#ifndef PSX_CYC_OVERCLOCK_H
#define PSX_CYC_OVERCLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t g_psx_cyc_overclock_shift;
extern uint32_t g_psx_cyc_overclock_carry;

/* The enhancement overclock of psx_cyc.h for what the CPU waits outside an instruction's own charge. Both return
 * `cycles` while it is off. */

/* A wait paid now (an instruction cache refill, a main RAM load off the inline path): divided as psx_cyc_charge
 * divides, sharing its carry of elapsed time. */
static inline uint32_t psx_cyc_overclocked_wait(uint32_t cycles) {
    if (!g_psx_cyc_overclock_shift) return cycles;
    const uint32_t total = g_psx_cyc_overclock_carry + cycles;
    g_psx_cyc_overclock_carry = total & ((1u << g_psx_cyc_overclock_shift) - 1u);
    return total >> g_psx_cyc_overclock_shift;
}

/* A latency that sets a deadline (GTE, multiplier): to the nearest cycle and without the carry, since the deadline
 * may expire unread or be replaced and so is no elapsed time. */
static inline uint32_t psx_cyc_overclocked_latency(uint32_t cycles) {
    if (!g_psx_cyc_overclock_shift) return cycles;
    return (cycles + (1u << (g_psx_cyc_overclock_shift - 1u))) >> g_psx_cyc_overclock_shift;
}

#ifdef __cplusplus
}
#endif

#endif
