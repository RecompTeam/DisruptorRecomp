#pragma once

#include <filesystem>

// A running game marks the folder of the player's files, so a launcher opened after it keeps its hands off them.
namespace disruptor::game_mark {

// Held for the game's life. Any number of games may hold the mark of one folder, and a failure only leaves it unmarked.
class Mark {
public:
    explicit Mark(const std::filesystem::path& user);
    ~Mark();
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;

private:
    void* handle_ = nullptr;
    int descriptor_ = -1;
};

// True while a game holds the mark of `user`.
bool marked(const std::filesystem::path& user);

} // namespace disruptor::game_mark
