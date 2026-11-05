#include "render_thread.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
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

std::atomic<int32_t> g_next{0};
std::atomic<bool> g_out_of_order{false};

void exec_counted(void *, const RtCmd *, const void *payload) {
    int32_t value;
    std::memcpy(&value, payload, sizeof value);
    if (value != g_next.load(std::memory_order_relaxed)) g_out_of_order = true;
    g_next.store(value + 1, std::memory_order_relaxed);
    if (value % 997 == 0) std::this_thread::sleep_for(std::chrono::microseconds(50));
    else if (value % 61 == 0) std::this_thread::yield();
}

void record_sized(int32_t value, uint32_t bytes) {
    void *payload = rt_cmd_begin(1, 0, bytes);
    std::memcpy(payload, &value, sizeof value);
    rt_cmd_commit();
}

/* Both threads keep falling asleep and waking each other: a lost wake leaves them asleep and the watchdog fails. */
void sleep_and_wake_stress() {
    constexpr int32_t kRecords = 200000;
    RtConfig config{};
    config.ring_bytes = size_t{1} << 20;
    config.max_frames = 1;
    config.exec = exec_counted;
    config.ctx = ctx;
    expect(rt_start(&config) == 1, "the render thread starts again");
    std::atomic<bool> done{false};
    std::thread watchdog([&done] {
        int32_t seen = -1;
        for (int still = 0; !done.load(); ++still) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const int32_t now = g_next.load();
            if (now != seen) { seen = now; still = 0; }
            if (still == 300) {
                std::cerr << "FAIL: no progress for 30 s at record " << now << ", a wake was lost\n";
                std::_Exit(1);
            }
        }
    });
    for (int32_t value = 0; value < kRecords; ++value) {
        record_sized(value, value % 3 == 0 ? 8192u : 4u);
        if (value % (value < kRecords / 2 ? 37 : 600) == 36) rt_frame_end();
        if (value % 4001 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    rt_frame_end();
    RtStats stats{};
    rt_get_stats(&stats);
    rt_stop();
    done = true;
    watchdog.join();
    expect(g_next.load() == kRecords && !g_out_of_order, "every record runs once, in order, through frame waits and full rings");
    expect(stats.backpressure_waits > 0 && stats.ring_full_waits > 0, "the run waited on frames in flight and on a full ring");
}

bool ran_up_to(int32_t count) {
    for (int tries = 0; tries < 2000 && g_next.load() < count; ++tries)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return g_next.load() == count;
}

/* Records go over in batches: a frame end, an empty frame's marker and a sync point must each hand over what is left. */
void frame_ends_and_syncs_hand_over_the_rest() {
    g_next = 0;
    g_out_of_order = false;
    RtConfig config{};
    config.ring_bytes = size_t{1} << 20;
    config.max_frames = 1;
    config.exec = exec_counted;
    config.ctx = ctx;
    expect(rt_start(&config) == 1, "the render thread starts for the hand-over checks");
    std::atomic<bool> done{false};
    std::thread watchdog([&done] {
        for (int still = 0; !done.load(); ++still) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (still == 100) {
                std::cerr << "FAIL: a frame marker never reached the render thread\n";
                std::_Exit(1);
            }
        }
    });
    for (int32_t value = 0; value < 5; ++value) record_sized(value, 4u);
    rt_frame_end();
    expect(ran_up_to(5), "a frame end hands over the records before it without a later one");
    for (int frame = 0; frame < 50; ++frame) rt_frame_end();
    for (int32_t value = 5; value < 12; ++value) record_sized(value, 4u);
    rt_acquire("test");
    expect(g_next.load() == 12 && !g_out_of_order, "a sync point runs the records after the last frame end");
    rt_release();
    rt_stop();
    done = true;
    watchdog.join();
}

/* A record that reserved more than it used gives the rest back: the ring holds what was used, in order, round its end. */
void a_shrunk_record_gives_back_its_room() {
    constexpr int32_t kRecords = 50000;
    g_next = 0;
    g_out_of_order = false;
    RtConfig config{};
    config.ring_bytes = size_t{1} << 20;
    config.max_frames = 2;
    config.exec = exec_counted;
    config.ctx = ctx;
    expect(rt_start(&config) == 1, "the render thread starts for the shrunk records");
    for (int32_t value = 0; value < kRecords; ++value) {
        void *payload = rt_cmd_begin(1, 0, 8192);
        std::memcpy(payload, &value, sizeof value);
        rt_cmd_commit_bytes(sizeof value);
        if (value % 100 == 99) rt_frame_end();
    }
    rt_frame_end();
    RtStats stats{};
    rt_get_stats(&stats);
    rt_stop();
    expect(g_next.load() == kRecords && !g_out_of_order, "every shrunk record runs once, in order");
    expect(stats.bytes == uint64_t{kRecords} * 32u, "a shrunk record keeps only its header and the payload it used");
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
    sleep_and_wake_stress();
    frame_ends_and_syncs_hand_over_the_rest();
    a_shrunk_record_gives_back_its_room();
    if (g_failures) return 1;
    std::cout << "render thread core: PASS\n";
    return 0;
}
