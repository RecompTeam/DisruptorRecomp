#include "game/render/movie_smoothing.h"

#include <atomic>

namespace {

std::atomic<bool> g_smooth_movies{true};

}  // namespace

extern "C" int disruptor_smooth_movies_enabled(void) { return g_smooth_movies.load(std::memory_order_relaxed) ? 1 : 0; }

extern "C" void disruptor_smooth_movies_set_enabled(int enabled) {
    g_smooth_movies.store(enabled != 0, std::memory_order_relaxed);
}
