#pragma once

#include <array>
#include <cstddef>
#include <string_view>

/* The game's words for a console memory card, said of the save files the port keeps instead. Each is written over the
 * one it replaces, so it is never longer. The small f and g are signs of the game's fonts and stay. */
namespace disruptor::save_words {

struct Word {
    std::string_view card, saves;
};

inline constexpr std::array<Word, 11> kWords = {{
    {"NO MEMORY CARD", "NO SAVE DATA"},
    {"COULD NOT READ MEMORY CARD", "COULD NOT READ SAVES"},
    {"CHECK MEMORY CARD", "CHECK SAVE FOLDER"},
    {"CARD IS NOT FORMATTED", "SAVE DATA IS DAMAGED"},
    {"ALL FILES WILL BE ERASED", "ALL SAVES WILL BE ERASED"},
    {"FORMAT CARD IN LEFT SLOT f", "RESET SAVE DATA f"},
    {"FORMAT CARD IN RIGHT SLOT f", "RESET SAVE DATA f"},
    {"FORMATTING ggg", "RESETTING ggg"},
    {"FORMATING ggg", "RESETTING ggg"},
    {"FORMAT COMPLETE", "RESET COMPLETE"},
    {"FORMAT FAILED", "RESET FAILED"},
}};

constexpr size_t longest() {
    size_t most = 0;
    for (const Word &word : kWords) most = word.card.size() > most ? word.card.size() : most;
    return most;
}

constexpr bool each_fits() {
    for (const Word &word : kWords)
        if (word.saves.size() > word.card.size()) return false;
    return true;
}
static_assert(each_fits(), "a word for saves is written over the card's and must not run past it");

constexpr std::string_view replacement(std::string_view text) {
    for (const Word &word : kWords)
        if (word.card == text) return word.saves;
    return {};
}

}  // namespace disruptor::save_words
