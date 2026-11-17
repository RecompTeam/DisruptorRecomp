#include "gpu_supplemental.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <random>

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

const PsxSupplementalRange *scan(const PsxSupplemental &s, uint32_t address) {
    for (uint32_t side = 0u; side < 2u; side++)
        for (uint32_t i = 0u; i < s.count[side]; i++)
            if (s.ranges[side][i].start <= address && address < s.ranges[side][i].end) return &s.ranges[side][i];
    return nullptr;
}

void test_the_search_is_a_scan_of_every_range() {
    std::mt19937 random(20261109u);
    uint32_t differed = 0u, found = 0u, missed = 0u;
    for (uint32_t ranges : {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 31u, 100u, 1000u}) {
        auto s = std::make_unique<PsxSupplemental>();
        for (int frame = 0; frame < 2; frame++) {
            psx_supplemental_frame(s.get());
            uint32_t next = 0x1000u + static_cast<uint32_t>(frame) * 0x40u;
            for (uint32_t i = 0u; i < ranges + static_cast<uint32_t>(frame); i++) {
                const uint32_t start = next + (random() % 4u ? 4u + random() % 0x80u : 0u);
                next = start + 4u + random() % 0x60u;
                psx_supplemental_band(s.get(), start, next, static_cast<int32_t>(i), static_cast<int32_t>(i + 1u + random() % 2u));
            }
            for (uint32_t address = 0xF00u; address < next + 0x100u; address++) {
                const PsxSupplementalRange *expected = scan(*s, address);
                if (psx_supplemental_find(s.get(), address) != expected) differed++;
                (expected ? found : missed)++;
            }
        }
    }
    expect(differed == 0u && found > 10000u && missed > 10000u, "every byte's answer is the first range that holds it, for tables of every size");
}

void test_the_last_answer_is_the_search_through_every_change() {
    auto s = std::make_unique<PsxSupplemental>();
    PsxSupplementalLast last = {};
    std::mt19937 random(20261110u);
    expect(!psx_supplemental_find_again(s.get(), &last, 0u), "an empty table and no lookup yet answer nothing for address 0");
    uint32_t next = 0x1000u, asked = 0u, differed = 0u, repeated = 0u;
    for (int step = 0; step < 200000; step++) {
        const uint32_t roll = random() % 100u;
        if (roll < 1u) {
            psx_supplemental_reset(s.get());
            next = 0x1000u;
        } else if (roll < 4u) {
            psx_supplemental_frame(s.get());
            next = 0x1000u + (random() % 64u) * 4u;
        } else if (roll < 30u) {
            const uint32_t start = next + (random() % 3u) * 0x34u, end = start + 0x34u;
            if (random() % 4u) psx_supplemental_add(s.get(), start, end);
            else psx_supplemental_band(s.get(), start, end, static_cast<int32_t>(random() % 320u), static_cast<int32_t>(random() % 320u));
            next = end;
        } else {
            if (random() % 3u) asked = 0x1000u + (random() % (next - 0x1000u + 0x100u));
            else repeated++;
            const PsxSupplementalRange *found = psx_supplemental_find(s.get(), asked);
            if (psx_supplemental_find_again(s.get(), &last, asked) != found) differed++;
        }
    }
    expect(differed == 0u && repeated > 10000u, "the remembered answer is the search's after resets, frames, new and joined ranges");
    psx_supplemental_reset(s.get());
    psx_supplemental_frame(s.get());
    psx_supplemental_add(s.get(), 0x1000u, 0x1034u);
    expect(psx_supplemental_find_again(s.get(), &last, 0x1040u) == nullptr, "a byte past the range is outside");
    psx_supplemental_add(s.get(), 0x1034u, 0x1068u);
    expect(psx_supplemental_find_again(s.get(), &last, 0x1040u) != nullptr, "and inside once the next packet joins the range");
    const uint64_t writes = s->writes;
    psx_supplemental_reset(s.get());
    expect(s->writes > writes && psx_supplemental_find_again(s.get(), &last, 0x1040u) == nullptr,
           "a reset moves the count on and forgets the answer");
}

}  // namespace

void test_supplemental_visibility_rules() {
    expect(psx_supplemental_draw_kind(1, 0) == 1, "an added room cannot cover original geometry");
    expect(psx_supplemental_draw_kind(1, 1) == 1, "the reference view excludes added rooms");
    expect(psx_supplemental_draw_kind(2, 0) == 0, "regular edge pieces keep the original face over sky and world");
    expect(psx_supplemental_draw_kind(2, 1) == 0, "the reference view uses the original whole face");
    expect(psx_supplemental_draw_kind(0, 0) == 0 && psx_supplemental_draw_kind(0, 1) == 0, "original faces remain original");
}

void test_span_geometry_selects_the_draw_policy() {
    struct Case {
        int near_face;
        int32_t least, most;
        int expected_draw;
    };
    const Case cases[] = {{1, 130, 180, 1}, {0, 50, 250, 1}, {0, 50, 150, 0},
                          {0, 150, 250, 0}, {0, 100, 250, 0}, {0, 50, 200, 0}};
    for (const Case &example : cases) {
        const int kind = psx_supplemental_span_kind(example.near_face, example.least, example.most, 100, 200);
        expect(psx_supplemental_draw_kind(kind, 0) == example.expected_draw, "near and two-edge overhangs stay restricted, regular edge pieces sort by depth");
        expect(psx_supplemental_draw_kind(kind, 1) == 0, "the reference view retains every part of the original face");
    }
}

int main() {
    test_supplemental_visibility_rules();
    test_span_geometry_selects_the_draw_policy();
    test_a_range_holds_its_bytes_and_no_others();
    test_ranges_are_found_among_many();
    test_touching_ranges_join_and_a_backward_one_is_dropped();
    test_a_frame_keeps_the_one_before_and_forgets_the_one_before_that();
    test_a_full_table_drops_the_rest();
    test_a_band_range_keeps_its_band();
    test_the_search_is_a_scan_of_every_range();
    test_the_last_answer_is_the_search_through_every_change();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_supplemental: PASS\n";
    return 0;
}
