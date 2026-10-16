#pragma once

#include <array>
#include <cstdint>

namespace disruptor::wheel {

enum class Kind { none, weapons, psionics };

struct Entry {
    const char *name = "";
    bool owned = false;
    bool equipped = false;
    int resource = -1; // -1 for a weapon with no ammunition counter
};

struct View {
    Kind kind = Kind::none;
    std::array<Entry, 10> entries{};
    int count = 0;
    int highlighted = -1;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
};

const View &view();
int sector(float x, float y, int count);
void draw(int width, int height);

} // namespace disruptor::wheel

extern "C" {
int disruptor_selection_wheels_enabled(void);
void disruptor_selection_wheels_set_enabled(int enabled);
void disruptor_selection_wheels_set_available(int available);
int disruptor_selection_wheel_open(void);
void disruptor_selection_wheel_cancel(void);
/* Called after the runtime's final input sample, before modern control injection.
 * Returns nonzero when this frame belongs to the wheel, including its close. */
int disruptor_selection_wheel_frame(void);
/* Protect selector bindings in early samples, including frames that fast-forward skips presenting. */
void disruptor_selection_wheel_guard_input(void);
void disruptor_selection_wheel_controller_axis(int axis, int value);
uint16_t disruptor_selection_wheel_filter_press(uint16_t press);
}
