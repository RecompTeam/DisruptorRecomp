#ifndef PSX_HOST_PICK_SLOT_H
#define PSX_HOST_PICK_SLOT_H

/*
 * A file pick handed from the thread that shows the dialog to the thread
 * that asked for it. One pick at a time: a second cannot open before the
 * first has been taken.
 */

#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

class PsxHostPickSlot {
  public:
    /* False while a pick is open or waits to be taken. */
    bool open() {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::kNone) return false;
        state_ = State::kOpen;
        return true;
    }

    /* The dialog could not be shown: the pick is over and there is nothing to take. */
    void abandon() {
        const std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::kNone;
    }

    /* The dialog ended, with a path or with an empty one. */
    void close(std::string path) {
        const std::lock_guard<std::mutex> lock(mutex_);
        picked_ = std::move(path);
        state_ = State::kClosed;
    }

    /* 1 with the path in `out`, 0 with nothing to take, -1 for no path or one that does not fit: 1 and -1 end the pick. */
    int take(char *out, int size) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::kClosed) return 0;
        state_ = State::kNone;
        if (!out || size <= 0 || picked_.empty() || picked_.size() >= static_cast<std::size_t>(size)) return -1;
        std::memcpy(out, picked_.c_str(), picked_.size() + 1);
        return 1;
    }

  private:
    enum class State { kNone, kOpen, kClosed };

    std::mutex mutex_;
    State state_ = State::kNone;
    std::string picked_;
};

#endif
