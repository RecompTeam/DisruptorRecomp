#include "state_stamp.h"

#include "boot_state.h"
#include "overlay_api.h"

#include <charconv>
#include <format>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

constexpr std::size_t hash_at = 16, abi_at = 20, version_at = 24;

std::uint32_t word(const std::string& bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + index])) << (8 * index);
    return value;
}

void put_word(std::string& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) bytes[at + index] = static_cast<char>((value >> (8 * index)) & 0xFF);
}

// `state_<entry>_slot<NN>.pst`, the name savestate.c writes.
std::optional<int> slot_of(const fs::path& file) {
    const std::string name = file.filename().string();
    if (name.size() != 25 || !name.starts_with("state_") || name.substr(14, 5) != "_slot" || !name.ends_with(".pst"))
        return std::nullopt;
    std::uint32_t entry = 0;
    if (std::from_chars(name.data() + 6, name.data() + 14, entry, 16).ptr != name.data() + 14) return std::nullopt;
    int slot = -1;
    if (std::from_chars(name.data() + 19, name.data() + 21, slot).ptr != name.data() + 21) return std::nullopt;
    if (slot < 0 || slot > 11 || name != state_name(entry, slot)) return std::nullopt;
    return slot;
}

void collect(const fs::path& folder, const std::string& language, const StateStamp& own, std::vector<SavedState>& found) {
    std::error_code failed;
    for (const auto& entry : fs::directory_iterator(folder, failed)) {
        if (!entry.is_regular_file(failed) || entry.path().extension() != ".pst") continue;
        auto state = read_state(entry.path());
        if (!state || state->stamp == own) continue;
        state->language = language;
        found.push_back(*state);
    }
}

} // namespace

StateStamp build_stamp() {
    return {static_cast<std::uint32_t>(PSX_OVERLAY_CODEGEN_HASH), static_cast<std::int32_t>(PSX_OVERLAY_ABI_TAG),
            static_cast<std::uint32_t>(PSX_OVERLAY_CODEGEN_VER)};
}

std::string stamp_text(const StateStamp& stamp) {
    return std::format("{:08x}-{}-{}", stamp.codegen_hash, stamp.abi_tag, stamp.codegen_version);
}

std::string state_name(std::uint32_t entry, int slot) { return std::format("state_{:08X}_slot{:02d}.pst", entry, slot); }

std::optional<SavedState> read_state(const fs::path& file) {
    const auto slot = slot_of(file);
    if (!slot) return std::nullopt;
    std::ifstream input(file, std::ios::binary);
    std::string header(BOOT_STATE_HEADER_WIRE_BYTES, '\0');
    if (!input.read(header.data(), static_cast<std::streamsize>(header.size()))) return std::nullopt;
    if (word(header, 0) != BOOT_STATE_MAGIC || word(header, 4) != BOOT_STATE_VERSION) return std::nullopt;
    SavedState state{file, {}, *slot, word(header, 12)};
    state.stamp = {word(header, hash_at), static_cast<std::int32_t>(word(header, abi_at)), word(header, version_at)};
    return state;
}

std::vector<SavedState> stale_states(const fs::path& saves) {
    std::vector<SavedState> found;
    const StateStamp own = build_stamp();
    collect(saves, {}, own, found);
    std::error_code failed;
    for (const auto& entry : fs::directory_iterator(saves, failed))
        if (entry.is_directory(failed) && entry.path().filename() != "old-states")
            collect(entry.path(), entry.path().filename().string(), own, found);
    return found;
}

std::optional<std::string> read_whole(const fs::path& file) {
    std::error_code failed;
    const auto size = fs::file_size(file, failed);
    if (failed) return std::nullopt;
    std::ifstream input(file, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) || input.peek() != std::ifstream::traits_type::eof())
        return std::nullopt;
    return bytes;
}

bool write_whole(const fs::path& file, std::string_view bytes) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    return !output.fail();
}

std::string restamped(std::string bytes, const StateStamp& stamp) {
    if (bytes.size() < BOOT_STATE_HEADER_WIRE_BYTES || word(bytes, 0) != BOOT_STATE_MAGIC ||
        word(bytes, 4) != BOOT_STATE_VERSION)
        return {};
    put_word(bytes, hash_at, stamp.codegen_hash);
    put_word(bytes, abi_at, static_cast<std::uint32_t>(stamp.abi_tag));
    put_word(bytes, version_at, stamp.codegen_version);
    return bytes;
}

std::string fingerprint(std::string_view bytes) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (const char byte : bytes) hash = (hash ^ static_cast<unsigned char>(byte)) * 0x100000001B3ull;
    return std::format("{:016x}", hash);
}

} // namespace disruptor::launcher
