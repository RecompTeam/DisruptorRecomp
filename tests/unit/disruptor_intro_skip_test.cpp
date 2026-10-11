#include "cpu_state.h"
#include "mod_plugins.h"

#include <chrono>
#include <cstdint>
#include <iostream>

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
}

namespace {
int g_writes = 0;
std::uint32_t g_written_address = 0;
std::uint32_t g_written_value = 0xFFFFFFFFu;
}  // namespace

void psx_mod_write_word(uint32_t address, uint32_t value) {
    ++g_writes;
    g_written_address = address;
    g_written_value = value;
}

namespace {
int g_started = 0;
std::uint8_t g_menu_byte = 0;
std::uint32_t g_read_address = 0;
int (*g_registered_query)(void) = nullptr;
std::uint32_t g_hooked_address = 0;
PSXModFunctionEntryCallback g_hooked_entry = nullptr;
}  // namespace

int psx_mod_game_started(void) { return g_started; }
uint8_t psx_mod_read_byte(uint32_t address) {
    g_read_address = address;
    return g_menu_byte;
}
int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    g_hooked_address = address;
    g_hooked_entry = callback;
    return 1;
}
extern "C" void psx_host_set_fast_forward_query(int (*query)(void)) { g_registered_query = query; }

#include "../../src/disruptor_intro_skip.cpp"

namespace {

constexpr std::uint32_t kTestLogoSite = 0x80020A80u;
constexpr std::uint32_t kTestLogoWord = 0x2A02005Eu;
constexpr std::uint32_t kTestFloorSite = 0x800147B0u;
constexpr std::uint32_t kTestFloorWord = 0x2A820003u;
constexpr std::uint32_t kTestButtonsSite = 0x80048678u;
constexpr std::uint32_t kTestButtonsWord = 0x30420940u;
constexpr std::uint32_t kTestStart = 0x0800u;

int g_failures = 0;
std::int64_t g_test_ms = 1000;

Clock::time_point test_now() {
    return Clock::time_point(std::chrono::milliseconds(g_test_ms));
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

struct Logo {
    std::uint32_t frame;
    std::uint32_t holding;
};

/* The game's own result of `slti $v0, $s0, 94` on a logo frame, then the seam. */
Logo logo_test(CPUState &cpu, std::int32_t frame, std::uint32_t word = kTestLogoWord, int phase = 1) {
    cpu.gpr[16] = static_cast<std::uint32_t>(frame);
    cpu.gpr[2] = frame < 94 ? 1u : 0u;
    disruptor_intro_skip_instruction_hook(&cpu, kTestLogoSite, word, phase);
    return {cpu.gpr[16], cpu.gpr[2]};
}

/* The game's own result of `slti $v0, $s4, 3`, then the seam. */
std::uint32_t floor_test(CPUState &cpu, std::int32_t movie, std::uint32_t word = kTestFloorWord, int phase = 1) {
    cpu.gpr[20] = static_cast<std::uint32_t>(movie);
    cpu.gpr[2] = movie < 3 ? 1u : 0u;
    disruptor_intro_skip_instruction_hook(&cpu, kTestFloorSite, word, phase);
    return cpu.gpr[2];
}

/* The game's own result of `andi $v0, $v0, 0x940` for the given pad word, then the seam. */
std::uint32_t buttons_test(CPUState &cpu, std::uint32_t pad, std::uint32_t word = kTestButtonsWord, int phase = 1) {
    cpu.gpr[2] = pad & 0x940u;
    disruptor_intro_skip_instruction_hook(&cpu, kTestButtonsSite, word, phase);
    return cpu.gpr[2];
}

bool untouched(const Logo &logo, std::int32_t frame) {
    return logo.frame == static_cast<std::uint32_t>(frame) && logo.holding == (frame < 94 ? 1u : 0u);
}

void test_off_changes_nothing() {
    CPUState cpu{};
    disruptor_intro_skip_set_enabled(0);
    expect(disruptor_intro_skip_enabled() == 0, "the switch reads back off");
    for (std::int32_t frame : {16, 24, 25, 60, 93, 94, 109})
        expect(untouched(logo_test(cpu, frame), frame), "off: a logo runs its full length");
    for (std::int32_t movie = 0; movie < 6; ++movie) {
        expect(floor_test(cpu, movie) == (movie < 3 ? 1u : 0u), "off: the game keeps its own floor decision");
        expect(buttons_test(cpu, 0u) == 0u && buttons_test(cpu, 0x40u) == 0x40u,
               "off: the player sees the real pad");
    }
}

void test_on_cuts_a_logo_where_a_button_would() {
    CPUState cpu{};
    disruptor_intro_skip_set_enabled(1);
    expect(disruptor_intro_skip_enabled() == 1, "the switch reads back on");
    for (std::int32_t frame : {16, 20, 24})
        expect(untouched(logo_test(cpu, frame), frame),
               "on: a logo is not cut before frame 25, where the game loads the next image");
    for (std::int32_t frame : {25, 26, 60, 93}) {
        const Logo logo = logo_test(cpu, frame);
        expect(logo.frame == 94u && logo.holding == 0u, "on: from frame 25 a logo goes straight to its fade-out");
    }
    for (std::int32_t frame : {94, 100, 109})
        expect(untouched(logo_test(cpu, frame), frame), "on: the fade-out itself runs as the game wrote it");
}

void test_on_ends_the_title_movie_only() {
    CPUState cpu{};
    disruptor_intro_skip_set_enabled(1);
    expect(floor_test(cpu, 0) == 0u, "on: the title movie gets no 120 frame floor");
    expect(buttons_test(cpu, 0u) == kTestStart, "on: its player loop sees START held");
    expect(buttons_test(cpu, 0x40u) == (0x40u | kTestStart), "on: a real button stays pressed beside it");
    for (std::int32_t movie : {1, 2, 3, 4, 14, 16, 100, -1}) {
        expect(floor_test(cpu, movie) == (movie < 3 ? 1u : 0u), "on: any other movie keeps the game's decision");
        expect(buttons_test(cpu, 0u) == 0u, "on: any other movie sees the real pad");
    }
    floor_test(cpu, 0);
    floor_test(cpu, 5);
    expect(buttons_test(cpu, 0u) == 0u, "the movie that starts next forgets the title movie before it");
    floor_test(cpu, 0);
    disruptor_intro_skip_set_enabled(0);
    floor_test(cpu, 0);
    expect(buttons_test(cpu, 0u) == 0u, "switching off takes effect at the next movie");
}

void test_only_the_reviewed_instructions_count() {
    CPUState cpu{};
    disruptor_intro_skip_set_enabled(1);
    expect(untouched(logo_test(cpu, 60, kTestLogoWord ^ 1u), 60) && untouched(logo_test(cpu, 60, kTestLogoWord, 0), 60),
           "a changed word or another phase leaves a logo alone");
    floor_test(cpu, 5);
    expect(floor_test(cpu, 0, kTestFloorWord ^ 1u) == 1u && floor_test(cpu, 0, kTestFloorWord, 0) == 1u,
           "a changed word or another phase leaves the floor decision alone");
    expect(buttons_test(cpu, 0u) == 0u, "and arms nothing");
    floor_test(cpu, 0);
    expect(buttons_test(cpu, 0u, kTestButtonsWord ^ 1u) == 0u && buttons_test(cpu, 0u, kTestButtonsWord, 0) == 0u,
           "a changed word or another phase presses nothing");
    cpu.gpr[2] = 0u;
    cpu.gpr[16] = 60u;
    disruptor_intro_skip_instruction_hook(&cpu, kTestButtonsSite + 4u, kTestButtonsWord, 1);
    disruptor_intro_skip_instruction_hook(&cpu, kTestLogoSite + 4u, kTestLogoWord, 1);
    disruptor_intro_skip_instruction_hook(nullptr, kTestButtonsSite, kTestButtonsWord, 1);
    expect(cpu.gpr[2] == 0u && cpu.gpr[16] == 60u, "another address and a missing CPU change nothing");

    for (int *comparator : {&g_ls_mode, &g_ls_replay_active}) {
        floor_test(cpu, 5);
        *comparator = 1;
        expect(untouched(logo_test(cpu, 60), 60), "a lockstep or replay run shows the retail logo");
        expect(floor_test(cpu, 0) == 1u, "a lockstep or replay run keeps the retail floor");
        *comparator = 0;
        expect(buttons_test(cpu, 0u) == 0u, "and was not armed meanwhile");
        floor_test(cpu, 0);
        *comparator = 1;
        expect(buttons_test(cpu, 0u) == 0u, "a lockstep or replay run sees the real pad");
        *comparator = 0;
    }
}

void test_the_skip_key() {
    CPUState cpu{};
    g_now = &test_now;
    disruptor_intro_skip_set_enabled(0);
    g_test_ms += 600;
    logo_test(cpu, 16);

    disruptor_intro_skip_request();
    expect(untouched(logo_test(cpu, 18), 18), "the key does not cut a logo before frame 25");
    g_test_ms += 5000;
    expect(untouched(logo_test(cpu, 24), 24), "nor one frame before it");
    Logo cut = logo_test(cpu, 25);
    expect(cut.frame == 94u && cut.holding == 0u, "a key pressed early cuts the logo at frame 25, even after its half second");
    expect(untouched(logo_test(cpu, 30), 30), "one press cuts once");

    logo_test(cpu, 16);
    disruptor_intro_skip_request();
    g_test_ms += 501;
    expect(untouched(logo_test(cpu, 40), 40), "a press older than half a second is forgotten");
    disruptor_intro_skip_request();
    g_test_ms += 500;
    cut = logo_test(cpu, 50);
    expect(cut.frame == 94u && cut.holding == 0u, "a press of half a second ago still cuts a logo past frame 25");

    logo_test(cpu, 16);
    disruptor_intro_skip_request();
    expect(untouched(logo_test(cpu, 100), 100), "a press during the fade-out changes nothing in it");
    g_test_ms += 5000;
    logo_test(cpu, 16);
    expect(untouched(logo_test(cpu, 30), 30), "and does not reach the next logo once it is stale");

    g_writes = 0;
    floor_test(cpu, 5);
    expect(buttons_test(cpu, 0u) == 0u && g_writes == 0, "without the key a movie sees the real pad");
    disruptor_intro_skip_request();
    expect(buttons_test(cpu, 0u) == kTestStart && g_writes == 1 && g_written_address == 0x800713C8u &&
               g_written_value == 0u,
           "the key drops the movie's floor and holds START");
    g_test_ms += 5000;
    expect(buttons_test(cpu, 0x40u) == (0x40u | kTestStart) && g_writes == 1,
           "START stays held to the movie's end and the floor is written once");
    expect(floor_test(cpu, 1) == 1u, "the next movie keeps the game's floor decision");
    expect(buttons_test(cpu, 0u) == 0u && g_writes == 1, "and is not ended by the old press");

    floor_test(cpu, 4);
    disruptor_intro_skip_request();
    g_test_ms += 501;
    expect(buttons_test(cpu, 0u) == 0u && g_writes == 1,
           "a press older than half a second does not end the movie it was made in");

    disruptor_intro_skip_request();
    g_test_ms += 100;
    floor_test(cpu, 5);
    expect(buttons_test(cpu, 0u) == 0u && g_writes == 1,
           "a press made before a movie began does not end it, even inside its half second");
    g_test_ms += 501;

    logo_test(cpu, 16);
    logo_test(cpu, 100);
    disruptor_intro_skip_request();
    g_test_ms += 50;
    logo_test(cpu, 105);
    g_test_ms += 150;
    logo_test(cpu, 16);
    expect(untouched(logo_test(cpu, 30), 30), "a press during one logo's fade-out is not taken by the next logo");
    g_test_ms += 100;
    disruptor_intro_skip_request();
    g_test_ms += 200;
    logo_test(cpu, 16);
    cut = logo_test(cpu, 25);
    expect(cut.frame == 94u && cut.holding == 0u, "a press between two logos is taken by the one that follows");
    g_test_ms += 501;

    disruptor_intro_skip_set_enabled(1);
    floor_test(cpu, 0);
    disruptor_intro_skip_request();
    expect(buttons_test(cpu, 0u) == kTestStart, "the key and the switch together still hold only START");
    disruptor_intro_skip_set_enabled(0);
    g_test_ms += 501;

    floor_test(cpu, 5);
    disruptor_intro_skip_request();
    g_ls_mode = 1;
    const int writes = g_writes;
    expect(buttons_test(cpu, 0u) == 0u && untouched(logo_test(cpu, 60), 60) && g_writes == writes,
           "a lockstep run ignores the key");
    g_ls_mode = 0;
    g_test_ms += 501;
}

/* A cold start: the game not yet started, the switch as given, the menu byte as the boot leaves it. */
void boot(int enabled) {
    g_boot_over = false;
    g_boot_frames = 0;
    g_started = 0;
    g_menu_byte = 0;
    disruptor_intro_skip_set_enabled(enabled);
}

int asked(int times) {
    int answered = 0;
    for (int i = 0; i < times; ++i) answered += disruptor_intro_skip_fast_forward();
    return answered;
}

void test_the_boot_runs_unseen_until_the_menu() {
    expect(g_registered_query == &disruptor_intro_skip_fast_forward && g_hooked_address == 0x80040E68u && g_hooked_entry,
           "the module gives the frontend its query and watches the play renderer");

    boot(1);
    expect(asked(50) == 0 && g_boot_frames == 0, "before the game starts nothing is asked for and nothing is counted");
    g_started = 1;
    expect(asked(700) == 700 && g_read_address == 0x800715FCu, "with the switch on the boot is fast-forwarded, by the menu byte");
    g_menu_byte = 0x18;
    expect(asked(1) == 0, "the main menu ends it");
    g_menu_byte = 0;
    expect(asked(100) == 0, "and it does not come back in that run");

    boot(1);
    g_started = 1;
    expect(asked(1500) == 1500 && asked(1) == 0 && asked(100) == 0, "a boot that never reaches the menu is let go after 1500 VBlanks");

    boot(0);
    g_started = 1;
    expect(asked(3000) == 0 && !g_boot_over, "with the switch off the boot is left alone, however long it takes");
    disruptor_intro_skip_set_enabled(1);
    expect(asked(1500) == 1500 && asked(1) == 0, "the switch turned on during the boot takes the rest of it, with the whole budget");

    boot(1);
    g_started = 1;
    const int first = asked(700);
    disruptor_intro_skip_set_enabled(0);
    const int paused = asked(3000);
    disruptor_intro_skip_set_enabled(1);
    expect(first == 700 && paused == 0 && asked(800) == 800 && asked(100) == 0,
           "the budget is one for the run: turning the switch off and on does not refill it");

    boot(0);
    g_started = 1;
    g_menu_byte = 0x18;
    expect(asked(1) == 0 && g_boot_over, "an unskipped boot is over at the menu too");
    disruptor_intro_skip_set_enabled(1);
    g_menu_byte = 0;
    expect(asked(10) == 0, "so the switch turned on later does nothing in that run");

    boot(1);
    g_started = 1;
    g_hooked_entry(nullptr, 0x80040E6Cu);
    expect(asked(5) == 5, "another function is not play");
    g_hooked_entry(nullptr, 0x80040E68u);
    expect(asked(100) == 0, "the play renderer ends it: a state loaded during the boot is in play");

    for (int *comparator : {&g_ls_mode, &g_ls_replay_active}) {
        boot(1);
        g_started = 1;
        *comparator = 1;
        expect(asked(3) == 0, "a lockstep run is never fast-forwarded");
        *comparator = 0;
        expect(asked(3) == 0, "nor after it");
    }
    boot(0);
}

}  // namespace

int main() {
    test_off_changes_nothing();
    test_on_cuts_a_logo_where_a_button_would();
    test_on_ends_the_title_movie_only();
    test_only_the_reviewed_instructions_count();
    test_the_skip_key();
    test_the_boot_runs_unseen_until_the_menu();
    if (g_failures) return 1;
    std::cout << "Disruptor intro skip tests passed\n";
    return 0;
}
