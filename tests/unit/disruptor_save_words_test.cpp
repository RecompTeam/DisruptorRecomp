#include "game/language/save_words.h"

#include <iostream>

namespace {

using namespace disruptor::save_words;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

}  // namespace

int main() {
    expect(replacement("NO MEMORY CARD") == "NO SAVE DATA", "a card's word becomes the saves'");
    expect(replacement("FORMAT CARD IN LEFT SLOT f") == replacement("FORMAT CARD IN RIGHT SLOT f"),
           "both slots reset the same save data");
    expect(replacement("FORMATING ggg") == "RESETTING ggg", "the game's misspelt FORMATING is replaced too");
    expect(replacement("NO MEMORY CARD ").empty() && replacement("no memory card").empty(), "only the exact text");
    expect(replacement("GAME SAVES UNAVAILABLE").empty(), "a text about saves already stays");
    expect(replacement("").empty(), "an empty string stays");
    for (const Word &word : kWords) {
        expect(word.saves.size() <= word.card.size(), "each word fits over the one it replaces");
        expect(replacement(word.saves).empty(), "a replaced word is never replaced again");
        expect(word.saves.find("CARD") == std::string_view::npos, "no word for saves names a card");
    }
    expect(longest() == 27, "the longest card text is FORMAT CARD IN RIGHT SLOT f");
    if (g_failures) return 1;
    std::cout << "save words: ok\n";
    return 0;
}
