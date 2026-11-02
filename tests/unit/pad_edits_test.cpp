#include "../../psxrecomp/runtime/include/pad_edits.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

constexpr uint16_t kNone = 0xFFFFu, kCross = 1u << 14, kSquare = 1u << 15, kUp = 1u << 4;

}  // namespace

int main() {
    PsxPadEdits edits;
    psx_pad_edits_clear(&edits);
    require(!psx_pad_edits_any(&edits, 0) && psx_pad_edits_apply(&edits, 0, 0xFFEFu) == 0xFFEFu,
            "without edits the re-sample stands as the devices read it");

    psx_pad_edits_press(&edits, 0, kCross);
    require(psx_pad_edits_apply(&edits, 0, kNone) == static_cast<uint16_t>(kNone & ~kCross),
            "a press holds when the device lets the button go between the two samples");
    require(psx_pad_edits_apply(&edits, 0, static_cast<uint16_t>(kNone & ~kUp)) ==
                static_cast<uint16_t>(kNone & ~kCross & ~kUp),
            "a press keeps what the late sample adds");
    require(!psx_pad_edits_any(&edits, 1), "the other slot is untouched");

    psx_pad_edits_clear(&edits);
    psx_pad_edits_word(&edits, 0, kNone);
    require(psx_pad_edits_apply(&edits, 0, static_cast<uint16_t>(kNone & ~kSquare)) == kNone,
            "a whole word the game wrote holds over a press made between the two samples");

    psx_pad_edits_press(&edits, 0, kSquare);
    require(psx_pad_edits_apply(&edits, 0, 0x0000u) == static_cast<uint16_t>(kNone & ~kSquare),
            "a press after a whole word applies over the word, not over the devices");

    psx_pad_edits_word(&edits, 0, 0x1234u);
    require(psx_pad_edits_apply(&edits, 0, kNone) == 0x1234u, "a later whole word drops the presses before it");

    psx_pad_edits_clear(&edits);
    psx_pad_edits_release(&edits, 0, kSquare);
    require(psx_pad_edits_apply(&edits, 0, static_cast<uint16_t>(kNone & ~kSquare)) == kNone,
            "a release holds when the device presses the button between the two samples");
    psx_pad_edits_press(&edits, 0, kSquare);
    require(psx_pad_edits_apply(&edits, 0, kNone) == static_cast<uint16_t>(kNone & ~kSquare),
            "the later of a release and a press of one button wins");

    const uint8_t centred[4] = {128, 128, 128, 128};
    psx_pad_edits_sticks(&edits, 1, centred);
    require(edits.has_sticks[1] && edits.sticks[1][2] == 128 && !edits.has_sticks[0], "sticks are kept per slot");
    std::cout << "pad edits: PASS\n";
    return 0;
}
