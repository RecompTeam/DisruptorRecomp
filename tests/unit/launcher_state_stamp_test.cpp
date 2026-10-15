#include "launcher/state_stamp.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using namespace disruptor::launcher;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

void put_word(std::string& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) bytes[at + index] = static_cast<char>((value >> (8 * index)) & 0xFF);
}

// A state as boot_state.c writes it: the header's nine words, then sections the launcher never reads.
void put_state(const fs::path& file, const StateStamp& stamp, std::uint32_t magic = 0x50535842u, std::uint32_t version = 3) {
    std::string bytes(36 + 64, '\x5A');
    put_word(bytes, 0, magic);
    put_word(bytes, 4, version);
    put_word(bytes, 8, 0xC4DAEB88u);
    put_word(bytes, 12, 0x80048CE4u);
    put_word(bytes, 16, stamp.codegen_hash);
    put_word(bytes, 20, static_cast<std::uint32_t>(stamp.abi_tag));
    put_word(bytes, 24, stamp.codegen_version);
    put_word(bytes, 28, 16);
    put_word(bytes, 32, 0);
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << bytes;
}

std::string whole(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}

void test_names() {
    expect(state_name(0x80048CE4u, 0) == "state_80048CE4_slot00.pst", "slot 0 is named as the game names it");
    expect(state_name(0x80048CE4u, 11) == "state_80048CE4_slot11.pst", "and slot 11");
}

void test_reading_and_finding() {
    const fs::path saves = fs::temp_directory_path() / "disruptor-state-stamp-test";
    fs::remove_all(saves);
    const StateStamp own = build_stamp();
    const StateStamp foreign{0x11111111u, own.abi_tag, own.codegen_version};
    put_state(saves / state_name(0x80048CE4u, 0), own);
    put_state(saves / state_name(0x80048CE4u, 3), foreign);
    put_state(saves / "German" / state_name(0x80048CE4u, 1), foreign);
    put_state(saves / state_name(0x80048CE4u, 4), foreign, 0x12345678u);
    put_state(saves / state_name(0x80048CE4u, 5), foreign, 0x50535842u, 2);
    put_state(saves / "state_80048CE4_slot03.pst.bak", foreign);
    put_state(saves / "old-states" / "11111111-21-9" / state_name(0x80048CE4u, 3), foreign);

    const auto state = read_state(saves / state_name(0x80048CE4u, 3));
    expect(state && state->slot == 3 && state->entry == 0x80048CE4u && state->stamp == foreign,
           "a state's slot, entry and stamp are read");
    expect(!read_state(saves / state_name(0x80048CE4u, 4)), "a file of another magic is no state");
    expect(!read_state(saves / state_name(0x80048CE4u, 5)), "nor one of another format version");
    expect(!read_state(saves / "state_80048CE4_slot03.pst.bak"), "nor a file of another name");

    const auto stale = stale_states(saves);
    expect(stale.size() == 2, "the two foreign states are found, this build's and the kept originals are not");
    bool english = false, german = false;
    for (const SavedState& found : stale) {
        english = english || (found.slot == 3 && found.language.empty());
        german = german || (found.slot == 1 && found.language == "German");
    }
    expect(english && german, "one in the US disc's folder and one in German's");

    const auto bytes = read_whole(saves / state_name(0x80048CE4u, 3));
    expect(bytes && bytes->size() == 100, "a state is read whole");
    const std::string after = restamped(*bytes, own);
    std::size_t differing = 0;
    for (std::size_t index = 0; index < bytes->size() && index < after.size(); ++index) differing += (*bytes)[index] != after[index];
    expect(after.size() == bytes->size() && after.substr(36) == bytes->substr(36) && after.substr(0, 16) == bytes->substr(0, 16),
           "only the stamp's words change");
    expect(differing > 0 && differing <= 12, "and they do");
    const auto other = read_whole(saves / state_name(0x80048CE4u, 4));
    expect(other && restamped(*other, own).empty(), "bytes that are no state are not restamped");
    expect(write_whole(saves / "written.pst", after) && read_whole(saves / "written.pst") == after, "bytes are written whole");
    expect(!read_whole(saves / "missing.pst"), "a missing file reads as nothing");
    expect(fingerprint(*bytes) != fingerprint(after) && fingerprint(after) == fingerprint(after) && fingerprint(after).size() == 16,
           "a fingerprint tells two contents apart and names one the same each time");
    expect(stamp_text(foreign) == "11111111-" + std::to_string(own.abi_tag) + "-" + std::to_string(own.codegen_version),
           "a stamp's text names its three parts");
    fs::remove_all(saves);
}

} // namespace

int main() {
    test_names();
    test_reading_and_finding();
    if (g_failures == 0) std::cout << "Launcher state stamp tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
