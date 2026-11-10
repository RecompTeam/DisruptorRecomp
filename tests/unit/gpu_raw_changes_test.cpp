#include "gpu_raw_changes.h"

#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    if (g_failures <= 20) std::cerr << "FAIL: " << message << '\n';
}

constexpr int kWidth = 64, kHeight = 32;

struct Copy {
    std::vector<uint16_t> texels = std::vector<uint16_t>(kWidth * kHeight);
    uint64_t held = 0;
};

struct Counts {
    int whole = 0, places = 0, nothing = 0;
};

void bring(const PsxRawChanges &changes, const std::vector<uint16_t> &picture, Copy &copy, Counts &counts) {
    PsxRawRect rects[PSX_RAW_CHANGE_RECTS];
    const int count = psx_raw_changes_since(&changes, copy.held, kWidth * kHeight, rects);
    if (count < 0) {
        copy.texels = picture;
        ++counts.whole;
    } else {
        for (int rect = 0; rect < count; ++rect)
            for (int y = rects[rect].y; y < rects[rect].y + rects[rect].h; ++y)
                for (int x = rects[rect].x; x < rects[rect].x + rects[rect].w; ++x)
                    copy.texels[y * kWidth + x] = picture[y * kWidth + x];
        ++(count ? counts.places : counts.nothing);
    }
    copy.held = changes.generation;
}

void test_a_copy_never_taken_takes_the_whole() {
    PsxRawChanges changes{};
    PsxRawRect rects[PSX_RAW_CHANGE_RECTS];
    expect(psx_raw_changes_since(&changes, 0, 1000, rects) == -1, "a picture never begun gives the whole");
    psx_raw_changes_begin(&changes);
    expect(changes.generation == 1 && psx_raw_changes_since(&changes, 0, 1000, rects) == -1, "a copy never taken takes the whole");
    expect(psx_raw_changes_since(&changes, 1, 1000, rects) == 0, "a copy of this very picture takes nothing");
    expect(psx_raw_changes_since(&changes, 2, 1000, rects) == -1, "a copy from past the picture's own count takes the whole");
}

void test_one_place_changed_often_is_one_place() {
    PsxRawChanges changes{};
    PsxRawRect rects[PSX_RAW_CHANGE_RECTS];
    psx_raw_changes_begin(&changes);
    const uint64_t held = changes.generation;
    for (int change = 0; change < 1000; ++change) psx_raw_changes_note(&changes, 40, 0, 20, 30);
    psx_raw_changes_note(&changes, 0, 5, 3, 2);
    expect(changes.generation == held + 1001, "every change is a generation");
    expect(psx_raw_changes_since(&changes, held, 2048, rects) == 2, "a thousand changes of one place and one of another are two places");
    expect(rects[0].x == 40 && rects[0].y == 0 && rects[0].w == 20 && rects[0].h == 30, "the first place is the one changed often");
    expect(psx_raw_changes_since(&changes, held + 1000, 2048, rects) == 1 && rects[0].x == 0 && rects[0].w == 3,
           "a copy taken before the last change takes that place alone");
    expect(psx_raw_changes_since(&changes, held, 606, rects) == -1, "places as large as the picture are the whole");
    expect(psx_raw_changes_since(&changes, held, 607, rects) == 2, "one texel under the whole is still its places");
}

void test_a_forgotten_change_takes_the_whole() {
    PsxRawChanges changes{};
    PsxRawRect rects[PSX_RAW_CHANGE_RECTS];
    psx_raw_changes_begin(&changes);
    const uint64_t first = changes.generation;
    for (int place = 0; place < PSX_RAW_CHANGE_RECTS; ++place) psx_raw_changes_note(&changes, place, 0, 1, 1);
    expect(psx_raw_changes_since(&changes, first, 100000, rects) == PSX_RAW_CHANGE_RECTS, "a full table still lists every place");
    psx_raw_changes_note(&changes, 0, 1, 1, 1);
    expect(psx_raw_changes_since(&changes, first, 100000, rects) == -1, "a copy older than a change the table dropped takes the whole");
    expect(psx_raw_changes_since(&changes, first + 1, 100000, rects) == PSX_RAW_CHANGE_RECTS,
           "a copy taken at the dropped change has it and takes the rest");
    psx_raw_changes_note(&changes, -1, 0, 4, 4);
    expect(psx_raw_changes_since(&changes, changes.generation - 1, 100000, rects) == -1, "a change with no place forgets every copy");
    psx_raw_changes_begin(&changes);
    expect(psx_raw_changes_since(&changes, changes.generation - 1, 100000, rects) == -1, "a picture begun anew forgets every copy");
}

void test_a_copy_brought_up_is_the_picture_through_every_change() {
    std::mt19937 random(20261110u);
    PsxRawChanges changes{};
    std::vector<uint16_t> picture(kWidth * kHeight);
    std::vector<Copy> copies(12);
    Counts counts;
    bool equal = true;
    psx_raw_changes_begin(&changes);
    for (int step = 0; step < 300000 && equal; ++step) {
        const unsigned kind = random() % 100u;
        if (kind < 60u) {
            int x, y, w, h;
            if (random() % 4u) {
                const unsigned slot = random() % 6u;
                x = 40; y = 0; w = 4 + (int)slot * 3; h = 20 + (int)slot;
            } else {
                x = (int)(random() % kWidth); y = (int)(random() % kHeight);
                w = 1 + (int)(random() % (unsigned)(kWidth - x)); h = 1 + (int)(random() % (unsigned)(kHeight - y));
                if (random() % 8u) { w = 1 + (w - 1) % 6; h = 1 + (h - 1) % 6; }
            }
            for (int row = y; row < y + h; ++row)
                for (int column = x; column < x + w; ++column) picture[row * kWidth + column] = (uint16_t)random();
            psx_raw_changes_note(&changes, x, y, w, h);
        } else if (kind < 99u) {
            Copy &copy = copies[random() % (kind < 90u ? 3u : copies.size())];
            bring(changes, picture, copy, counts);
            equal = copy.texels == picture;
        } else if (random() % 50u == 0) {
            for (auto &texel : picture) texel = (uint16_t)random();
            psx_raw_changes_begin(&changes);
        }
    }
    expect(equal, "a copy brought up to date is the picture, texel for texel");
    expect(counts.whole > 100 && counts.places > 10000 && counts.nothing > 100, "the whole, the places alone and nothing were each taken");
}

}  // namespace

int main() {
    test_a_copy_never_taken_takes_the_whole();
    test_one_place_changed_often_is_one_place();
    test_a_forgotten_change_takes_the_whole();
    test_a_copy_brought_up_is_the_picture_through_every_change();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_raw_changes: PASS\n";
    return 0;
}
