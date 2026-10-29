#include "render_thread.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

std::vector<int32_t> g_replayed;
std::thread::id g_exec_thread;
std::atomic<int> g_current{0};

void exec(void *, const RtCmd *cmd, const void *payload) {
    int32_t value;
    std::memcpy(&value, payload, sizeof value);
    g_replayed.push_back(value);
    g_exec_thread = std::this_thread::get_id();
    if (cmd->op != 1) g_replayed.push_back(-1);
}

void ctx(void *, int current) { g_current = current ? 1 : 0; }

void record(int32_t value) {
    void *payload = rt_cmd_begin(1, 0, sizeof value);
    std::memcpy(payload, &value, sizeof value);
    rt_cmd_commit();
}

} // namespace

int main() {
    RtConfig config{};
    config.ring_bytes = size_t{1} << 20;
    config.max_frames = 2;
    config.exec = exec;
    config.ctx = ctx;
    expect(rt_start(&config) == 1 && rt_running(), "the render thread starts");
    for (int32_t frame = 0; frame < 50; ++frame) {
        for (int32_t record_index = 0; record_index < 100; ++record_index) record(frame * 100 + record_index);
        rt_frame_end();
    }
    rt_acquire("test");
    expect(rt_held() && g_current == 1, "a sync point hands the context to the caller");
    expect(g_replayed.size() == 5000, "every record ran before the sync point returned");
    bool ordered = true;
    for (size_t i = 0; i < g_replayed.size(); ++i) ordered = ordered && g_replayed[i] == static_cast<int32_t>(i);
    expect(ordered, "records run in the order they were recorded");
    expect(g_exec_thread != std::this_thread::get_id(), "records run on the render thread");
    rt_release();
    record(5000);
    rt_frame_end();
    rt_stop();
    expect(!rt_running() && g_replayed.size() == 5001, "stopping runs what was left and ends the thread");
    if (g_failures) return 1;
    std::cout << "render thread core: PASS\n";
    return 0;
}