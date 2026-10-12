#include "save_point.h"

#include <cstdio>
#include <cstdlib>

namespace {

int g_request_waits = 0;
int g_asked = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

constexpr int kPeriodic = 1, kBetween = 0, kMarked = 1, kUnmarked = 0;

void a_marked_periodic_check_polls() {
    int owed = 0;
    g_request_waits = 0;
    expect(psx_save_poll_due(kPeriodic, kMarked, &owed) == 1, "a marked periodic check polls, a request waiting or not");
    expect(owed == 0, "and owes nothing");
    expect(psx_save_poll_due(kBetween, kMarked, &owed) == 0, "a marked check between periodic ones does not poll");
}

void an_unmarked_check_never_polls() {
    for (int waits = 0; waits < 2; ++waits) {
        for (int periodic = 0; periodic < 2; ++periodic) {
            for (int owed_before = 0; owed_before < 2; ++owed_before) {
                int owed = owed_before;
                g_request_waits = waits;
                expect(psx_save_poll_due(periodic, kUnmarked, &owed) == 0, "an unmarked check does not poll, whatever else holds");
                expect(owed == (periodic ? waits : owed_before), "it owes a periodic poll only when a request waits");
            }
        }
    }
}

void a_periodic_poll_missed_with_a_request_is_owed_to_the_next_marked_check() {
    int owed = 0;
    g_request_waits = 1;
    expect(psx_save_poll_due(kPeriodic, kUnmarked, &owed) == 0 && owed == 1, "the poll is owed");
    expect(psx_save_poll_due(kBetween, kUnmarked, &owed) == 0 && owed == 1, "an unmarked check in between leaves it owed");
    expect(psx_save_poll_due(kBetween, kMarked, &owed) == 1 && owed == 0, "the next marked check takes it");
    expect(psx_save_poll_due(kBetween, kMarked, &owed) == 0, "and the one after it does not poll again");
}

void nothing_is_owed_when_no_request_waits() {
    int owed = 0;
    g_request_waits = 0;
    expect(psx_save_poll_due(kPeriodic, kUnmarked, &owed) == 0 && owed == 0, "a periodic poll missed with nothing waiting is not owed");
    expect(psx_save_poll_due(kBetween, kMarked, &owed) == 0, "so the next marked check does not poll");
    owed = 1;
    expect(psx_save_poll_due(kPeriodic, kUnmarked, &owed) == 0 && owed == 0, "a request served elsewhere takes the owed poll away");
}

void the_request_is_asked_for_only_at_an_unmarked_periodic_check() {
    int owed = 0;
    g_asked = 0;
    for (int index = 0; index < 1000; ++index) (void)psx_save_poll_due(kBetween, kMarked, &owed);
    for (int index = 0; index < 1000; ++index) (void)psx_save_poll_due(kBetween, kUnmarked, &owed);
    (void)psx_save_poll_due(kPeriodic, kMarked, &owed);
    expect(g_asked == 0, "the other checks do not ask whether a request waits");
    (void)psx_save_poll_due(kPeriodic, kUnmarked, &owed);
    expect(g_asked == 1, "an unmarked periodic check asks once");
}

void periodic_polls_that_always_meet_an_unmarked_check_do_not_starve_a_request() {
    constexpr int kPeriod = 16384;
    int owed = 0, polls = 0, polled_at = -1;
    g_request_waits = 1;
    for (int check = 1; check <= 3 * kPeriod && g_request_waits; ++check) {
        const int periodic = check % kPeriod == 0;
        if (psx_save_poll_due(periodic, periodic ? kUnmarked : kMarked, &owed)) {
            ++polls;
            polled_at = check;
            g_request_waits = 0;
        }
    }
    expect(polls == 1, "the request is served");
    expect(polled_at == kPeriod + 1, "at the first marked check after the periodic one that met it");
}

}  // namespace

extern "C" int savestate_pending(void) {
    ++g_asked;
    return g_request_waits;
}

int main() {
    a_marked_periodic_check_polls();
    an_unmarked_check_never_polls();
    a_periodic_poll_missed_with_a_request_is_owed_to_the_next_marked_check();
    nothing_is_owed_when_no_request_waits();
    the_request_is_asked_for_only_at_an_unmarked_periodic_check();
    periodic_polls_that_always_meet_an_unmarked_check_do_not_starve_a_request();
    std::puts("save point tests passed");
    return 0;
}
