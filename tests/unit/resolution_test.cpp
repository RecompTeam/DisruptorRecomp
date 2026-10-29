#include "game/settings/resolution.h"

#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

using namespace disruptor::resolution;

void a_saved_size_reads_as_its_two_numbers() {
    const Size size = parse("1920x1080");
    expect(size.width == 1920 && size.height == 1080, "1920x1080 is 1920 by 1080");
    expect(parse("").width == 0 && parse("").height == 0, "the desktop's choice is 0 by 0");
    expect(parse("Desktop").width == 0, "the label of the desktop's choice is no size");
    expect(parse("1920x").width == 0 && parse("x1080").width == 0, "half a size is none");
    expect(parse("1920x1080p").width == 0 && parse("0x1080").width == 0, "trailing text or a zero side is none");
}

void the_list_and_its_saved_values_agree() {
    expect(index_of("") == 0 && at(0).empty(), "the desktop's choice is first and saved empty");
    expect(index_of("2560x1600") > 0 && at(index_of("2560x1600")) == "2560x1600", "a listed size reads back as itself");
    expect(index_of("1234x567") == 0, "a size the list lacks shows as the desktop's");
    for (int i = 1; i < static_cast<int>(std::size(kChoices)); ++i)
        expect(parse(kChoices[i]).width > 0, "every listed size parses");
}

} // namespace

int main() {
    a_saved_size_reads_as_its_two_numbers();
    the_list_and_its_saved_values_agree();
    if (g_failures) return 1;
    std::cout << "resolution: PASS\n";
    return 0;
}