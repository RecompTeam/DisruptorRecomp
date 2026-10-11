#include "../../psxrecomp-overlay/runtime/include/host_pick_slot.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* What a pick that closed with `path` gives a taker with `size` bytes at `out`. */
int taken(PsxHostPickSlot &slot, const std::string &path, char *out, int size) {
    if (!slot.open()) return -2;
    slot.close(path);
    return slot.take(out, size);
}

}  // namespace

int main() {
    PsxHostPickSlot slot;
    char out[8] = "unset";
    expect(slot.take(out, 8) == 0, "there is nothing to take before a pick");
    expect(slot.open() && !slot.open(), "one pick is open at a time");
    expect(slot.take(out, 8) == 0 && std::string(out) == "unset", "an open pick gives nothing yet");
    slot.close("C:\\a.cue");
    expect(!slot.open(), "a pick that closed waits to be taken before another opens");
    expect(slot.take(out, 8) == -1 && std::string(out) == "unset", "a path that needs one byte more than there is room for is not written");
    expect(slot.take(out, 8) == 0, "and the pick is over");
    expect(taken(slot, "C:\\a.cu", out, 8) == 1 && std::string(out) == "C:\\a.cu", "a path that fits to the byte is handed over");
    expect(slot.take(out, 8) == 0, "once");
    expect(taken(slot, "", out, 8) == -1, "a dialog that closed without a path says so");
    expect(taken(slot, "x", out, 0) == -1 && taken(slot, "x", out, -1) == -1 && taken(slot, "x", out, -2147483647 - 1) == -1 && taken(slot, "x", nullptr, 8) == -1,
           "no room, a negative room or no place to write gives no path");
    expect(std::string(out) == "C:\\a.cu", "and writes nothing");
    expect(taken(slot, "x", out, 2) == 1 && std::string(out) == "x", "a pick after a refused one works");
    expect(slot.open(), "a pick opens after the last was taken");
    slot.abandon();
    expect(slot.take(out, 8) == 0 && slot.open(), "an abandoned pick leaves nothing to take and lets the next open");
    if (g_failures != 0) return 1;
    std::cout << "host pick slot: PASS\n";
    return 0;
}
