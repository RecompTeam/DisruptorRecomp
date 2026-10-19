#include "psx_cyc_overclock.h"

#include <cstdint>
#include <iostream>

extern "C" {
uint32_t g_psx_cyc_overclock_shift = 0;
uint32_t g_psx_cyc_overclock_carry = 0;
}

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

int main() {
    g_psx_cyc_overclock_carry = 3;
    expect(psx_cyc_overclocked_wait(22) == 22 && psx_cyc_overclocked_latency(22) == 22 && g_psx_cyc_overclock_carry == 3,
           "without the overclock a wait and a latency are whole and the carry is left alone");

    g_psx_cyc_overclock_shift = 2;
    g_psx_cyc_overclock_carry = 0;
    expect(psx_cyc_overclocked_wait(22) == 5 && g_psx_cyc_overclock_carry == 2, "at four times a wait of 22 is 5 and 2 carried");
    expect(psx_cyc_overclocked_wait(22) == 6 && g_psx_cyc_overclock_carry == 0, "the carry joins the next wait");
    expect(psx_cyc_overclocked_wait(3) == 0 && g_psx_cyc_overclock_carry == 3, "a wait under the divisor is carried whole");
    std::uint32_t charged = 0;
    g_psx_cyc_overclock_carry = 0;
    for (int wait = 0; wait < 400; ++wait) charged += psx_cyc_overclocked_wait(3);
    expect(charged == 300 && g_psx_cyc_overclock_carry == 0, "waits too short to charge alone add up to a quarter");

    g_psx_cyc_overclock_carry = 3;
    expect(psx_cyc_overclocked_latency(22) == 6 && psx_cyc_overclocked_latency(21) == 5 &&
               psx_cyc_overclocked_latency(2) == 1 && psx_cyc_overclocked_latency(1) == 0,
           "a latency goes to the nearest cycle");
    expect(g_psx_cyc_overclock_carry == 3, "and leaves the elapsed carry alone, a deadline may never be waited for");
    expect(psx_cyc_overclocked_wait(1) == 1 && g_psx_cyc_overclock_carry == 0,
           "so the carry an instruction left is still charged next");

    g_psx_cyc_overclock_shift = 3;
    g_psx_cyc_overclock_carry = 7;
    expect(psx_cyc_overclocked_wait(1) == 1 && g_psx_cyc_overclock_carry == 0, "at eight times a carry of 7 and 1 more make one cycle");
    expect(psx_cyc_overclocked_wait(0) == 0 && g_psx_cyc_overclock_carry == 0, "nothing waited is nothing charged");
    expect(psx_cyc_overclocked_latency(37) == 5 && psx_cyc_overclocked_latency(36) == 5 && psx_cyc_overclocked_latency(35) == 4,
           "a division's 37 at eight times is 5, 4.5 rounds up and 4.375 down");

    if (g_failures != 0) return 1;
    std::cout << "PSX overclocked waits tests passed\n";
    return 0;
}
