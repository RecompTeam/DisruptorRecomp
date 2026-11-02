#include "gpu_supplemental.h"

#include <cstdint>
#include <iostream>
#include <memory>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    if (g_failures <= 20) std::cerr << "FAIL: " << message << '\n';
}

void test_a_range_holds_its_bytes_and_no_others() {
    auto s = std::make_unique<PsxSupplemental>();
    psx_supplemental_frame(s.get());
    psx_supplemental_add(s.get(), 0x1000u, 0x1034u);
    expect(psx_supplemental_has(s.get(), 0x1000u) && psx_supplemental_has(s.get(), 0x1033u), "a packet's first and last byte are in");
    expect(!psx_supplemental_has(s.get(), 0x0FFFu) && !psx_supplemental_has(s.get(), 0x1034u), "the bytes either side are not");
}

void test_ranges_are_found_among_many() {
    auto s = std::make_unique<PsxSupplemental>();
    psx_supplemental_frame(s.get());
    for (uint32_t i = 0; i < 1000u; i++) psx_supplemental_add(s.get(), 0x10000u + i * 0x100u, 0x10000u + i * 0x100u + 0x34u);
    bool found = true, missed = true;
    for (uint32_t i = 0; i < 1000u; i++) {
        found = found && psx_supplemental_has(s.get(), 0x10000u + i * 0x100u + 0x10u);
        missed = missed && !psx_supplemental_has(s.get(), 0x10000u + i * 0x100u + 0x40u);
    }
    expect(found && missed, "every range is found and every gap between them is not");
}

void test_touching_ranges_join_and_a_backward_one_is_dropped() {
    auto s = std::make_unique<PsxSupplemental>();
    psx_supplemental_frame(s.get());
    psx_supplemental_add(s.get(), 0x2000u, 0x2034u);
    psx_supplemental_add(s.get(), 0x2034u, 0x2068u);
    expect(s->count[s->frame & 1u] == 1u && psx_supplemental_has(s.get(), 0x2050u), "a packet right after the last joins its range");
    psx_supplemental_add(s.get(), 0x1000u, 0x1034u);
    psx_supplemental_add(s.get(), 0x2060u, 0x2090u);
    expect(!psx_supplemental_has(s.get(), 0x1010u) && !psx_supplemental_has(s.get(), 0x2080u),
           "a range before or inside the last is dropped so the order holds");
    psx_supplemental_add(s.get(), 0x3000u, 0x3000u);
    expect(!psx_supplemental_has(s.get(), 0x3000u), "an empty range holds nothing");
}

void test_a_frame_keeps_the_one_before_and_forgets_the_one_before_that() {
    auto s = std::make_unique<PsxSupplemental>();
    psx_supplemental_frame(s.get());
    psx_supplemental_add(s.get(), 0x1000u, 0x1034u);
    psx_supplemental_frame(s.get());
    psx_supplemental_add(s.get(), 0x8000u, 0x8034u);
    expect(psx_supplemental_has(s.get(), 0x1000u) && psx_supplemental_has(s.get(), 0x8000u),
           "the packets of the frame being drawn stay while the next one is built");
    psx_supplemental_frame(s.get());
    expect(!psx_supplemental_has(s.get(), 0x1000u) && psx_supplemental_has(s.get(), 0x8000u), "two frames back they are gone");
    psx_supplemental_reset(s.get());
    expect(!psx_supplemental_has(s.get(), 0x8000u), "a reset or a loaded state forgets them all");
}

void test_a_full_table_drops_the_rest() {
    auto s = std::make_unique<PsxSupplemental>();
    psx_supplemental_frame(s.get());
    for (uint32_t i = 0; i <= PSX_SUPPLEMENTAL_RANGES; i++) psx_supplemental_add(s.get(), 0x100000u + i * 0x40u, 0x100000u + i * 0x40u + 0x34u);
    expect(psx_supplemental_has(s.get(), 0x100000u + (PSX_SUPPLEMENTAL_RANGES - 1u) * 0x40u) &&
               !psx_supplemental_has(s.get(), 0x100000u + PSX_SUPPLEMENTAL_RANGES * 0x40u),
           "past the table a range is drawn as the game made it");
}

void test_a_band_range_keeps_its_band() {
    auto s = std::make_unique<PsxSupplemental>();
    psx_supplemental_frame(s.get());
    psx_supplemental_add(s.get(), 0x1000u, 0x1034u);
    psx_supplemental_band(s.get(), 0x1034u, 0x1068u, 199, 223);
    psx_supplemental_band(s.get(), 0x1068u, 0x109Cu, 199, 223);
    psx_supplemental_band(s.get(), 0x109Cu, 0x10D0u, -PSX_SUPPLEMENTAL_FAR, 223);
    expect(s->count[s->frame & 1u] == 3u, "a range joins the one before only with its band");
    const PsxSupplementalRange *whole = psx_supplemental_find(s.get(), 0x1010u), *band = psx_supplemental_find(s.get(), 0x1090u);
    expect(whole && whole->left > whole->right, "a face told whole has no band");
    expect(band && band->left == 199 && band->right == 223 && band->end == 0x109Cu, "a face told with a band keeps it");
    expect(psx_supplemental_find(s.get(), 0x10A0u)->left == -PSX_SUPPLEMENTAL_FAR, "a band open on one side keeps that");
    expect(!psx_supplemental_find(s.get(), 0x10D0u), "past the last range there is none");
}

void test_pieces_of_a_face_against_its_band() {
    PsxSupplementalPiece pieces[3];
    expect(psx_supplemental_pieces(190, 210, 199, 223, pieces) == 2 && pieces[0].outside && pieces[0].high == 199 && !pieces[1].outside &&
               pieces[1].low == 199 && pieces[1].high == 223,
           "a face over the left edge: past it as the widened walk's, then inside as the game's");
    expect(psx_supplemental_pieces(150, 300, 199, 223, pieces) == 3 && pieces[0].outside && !pieces[1].outside && pieces[2].outside &&
               pieces[2].low == 223,
           "a face over both edges is three pieces");
    expect(psx_supplemental_pieces(200, 220, 199, 223, pieces) == 1 && !pieces[0].outside, "a face inside is one piece, the game's");
    expect(psx_supplemental_pieces(199, 223, 199, 223, pieces) == 1, "corners on the edges are inside");
    expect(psx_supplemental_pieces(150, 190, 199, 223, pieces) == 1 && pieces[0].outside, "a face wholly past an edge is one piece");
    expect(psx_supplemental_pieces(150, 300, PSX_SUPPLEMENTAL_FAR, -PSX_SUPPLEMENTAL_FAR, pieces) == 1 && pieces[0].outside,
           "a whole face is one piece, outside");
    expect(psx_supplemental_pieces(150, 300, -PSX_SUPPLEMENTAL_FAR, 223, pieces) == 2 && !pieces[0].outside && pieces[1].outside,
           "a band open on the left cuts on the right alone");
}

}  // namespace

int main() {
    test_a_range_holds_its_bytes_and_no_others();
    test_ranges_are_found_among_many();
    test_touching_ranges_join_and_a_backward_one_is_dropped();
    test_a_frame_keeps_the_one_before_and_forgets_the_one_before_that();
    test_a_full_table_drops_the_rest();
    test_a_band_range_keeps_its_band();
    test_pieces_of_a_face_against_its_band();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_supplemental: PASS\n";
    return 0;
}
