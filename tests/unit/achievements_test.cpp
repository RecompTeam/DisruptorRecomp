#include "game/achievements/account.h"
#include "game/achievements/badges.h"
#include "game/achievements/chime.h"
#include "game/achievements/guest_memory.h"
#include "game/achievements/login.h"
#include "game/achievements/policy.h"
#include "game/achievements/progress_file.h"
#include "game/achievements/requests.h"
#include "game/achievements/session.h"
#include "game/achievements/sound_clip.h"
#include "game/achievements/user_agent.h"
#include "launcher/achievements_account.h"
#include "rhash/md5.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <span>
#include <vector>

namespace fs = std::filesystem;
namespace achievements = disruptor::achievements;
using disruptor::launcher::AchievementsAccount;

namespace {

using namespace std::chrono_literals;

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAILED: " << message << "\n";
}

std::string read(const fs::path& file) {
    std::ostringstream text;
    text << std::ifstream(file, std::ios::binary).rdbuf();
    return text.str();
}

fs::path scratch(const char* name) {
    const fs::path folder = fs::temp_directory_path() / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

bool settles(const std::function<bool()>& done) {
    for (int waited = 0; waited < 500; ++waited) {
        if (done()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

constexpr const char* kSignedIn =
    R"({"Success":true,"User":"Player1","Token":"ApiToken0123","Score":120,"SoftcoreScore":5,"Messages":0})";
constexpr const char* kRefused =
    R"({"Success":false,"Status":401,"Code":"invalid_credentials","Error":"Invalid user/password combination."})";

// Answers every request with one canned response and keeps what it was asked.
class Canned final : public achievements::Transport {
public:
    explicit Canned(achievements::Response answer, std::chrono::milliseconds delay = 0ms)
        : answer_(std::move(answer)), delay_(delay) {}

    achievements::Response perform(const achievements::Request& request) override {
        std::this_thread::sleep_for(delay_);
        const std::lock_guard lock(mutex_);
        asked_.push_back(request);
        threads_.push_back(std::this_thread::get_id());
        return answer_;
    }

    std::vector<achievements::Request> asked() {
        const std::lock_guard lock(mutex_);
        return asked_;
    }
    std::vector<std::thread::id> threads() {
        const std::lock_guard lock(mutex_);
        return threads_;
    }

private:
    achievements::Response answer_;
    std::chrono::milliseconds delay_;
    std::mutex mutex_;
    std::vector<achievements::Request> asked_;
    std::vector<std::thread::id> threads_;
};

bool live() {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    const bool set = _dupenv_s(&value, &length, "DISRUPTOR_ACHIEVEMENTS_LIVE") == 0 && value != nullptr;
    std::free(value);
    return set;
#else
    return std::getenv("DISRUPTOR_ACHIEVEMENTS_LIVE") != nullptr;
#endif
}

void test_versions() {
    expect(achievements::numeric_version("v0.7.0") == "0.7.0", "a release tag loses its v");
    expect(achievements::numeric_version("0.2.0-alpha.1") == "0.2.0", "a pre-release keeps its numbers alone");
    expect(achievements::numeric_version("6.8.0-45-generic") == "6.8.0", "a kernel release keeps its numbers alone");
    expect(achievements::numeric_version("1.") == "1", "a trailing dot is dropped");
    expect(achievements::numeric_version("dev") == "0.0.0", "a name with no number is 0.0.0");
    expect(achievements::numeric_version(".1") == "0.0.0" && achievements::numeric_version("v") == "0.0.0",
           "a text that does not start with a digit is 0.0.0");
    expect(achievements::user_agent("v0.7.0", "Windows 10.0.26200") ==
               "DisruptorRecompiled/0.7.0 (Windows 10.0.26200) rcheevos/12.5",
           "the user agent is the client's name and version, the system, then the library");
    const std::string own = achievements::user_agent();
    expect(own.starts_with("DisruptorRecompiled/") && own.find(") rcheevos/") != std::string::npos,
           "this build's user agent has the same shape");
}

void test_account_file() {
    const fs::path folder = scratch("disruptor-achievements-account");
    expect(!achievements::read_account(folder), "no file is no account");
    achievements::write_account(folder, {"Player1", "ApiToken0123"});
    const auto account = achievements::read_account(folder);
    expect(account && account->username == "Player1" && account->token == "ApiToken0123",
           "an account reads back as written");
    const std::string text = read(folder / achievements::kAccountFile);
    expect(text.find("Player1") != std::string::npos, "the name is plain in the file");
#ifdef _WIN32
    expect(text.find("ApiToken0123") == std::string::npos && text.find("sealing = \"dpapi\"") != std::string::npos,
           "the token is sealed for the Windows user");
#else
    const fs::perms others = fs::perms::group_all | fs::perms::others_all;
    expect((fs::status(folder / achievements::kAccountFile).permissions() & others) == fs::perms::none,
           "the file is its owner's alone");
#endif
    expect(!fs::exists(folder / (std::string(achievements::kAccountFile) + ".new")), "no half-written file is left");
    achievements::write_account(folder, {"Player2", "ApiToken4567"});
    const auto again = achievements::read_account(folder);
    expect(again && again->username == "Player2" && again->token == "ApiToken4567",
           "signing in again replaces the account");
#ifndef _WIN32
    const fs::path stale = folder / (std::string(achievements::kAccountFile) + ".new");
    std::ofstream(stale, std::ios::binary) << "stale";
    fs::permissions(stale, fs::perms::others_read, fs::perm_options::add);
    std::ifstream held(stale, std::ios::binary);
    achievements::write_account(folder, {"Player2", "ApiToken4567"});
    std::ostringstream seen;
    seen << held.rdbuf();
    expect(seen.str() == "stale", "a file someone opened before the write never receives the token");
#endif
    bool refused = false;
    try {
        achievements::write_account(folder, {"Player\"1", "ApiToken0123"});
    } catch (const std::exception&) {
        refused = true;
    }
    const auto kept = achievements::read_account(folder);
    expect(refused && kept && kept->username == "Player2",
           "a name that would break the file is refused and the old account stays");
    std::ofstream(folder / achievements::kAccountFile, std::ios::binary | std::ios::trunc) << "username = [";
    expect(!achievements::read_account(folder), "a broken file is no account");
    std::ofstream(folder / achievements::kAccountFile, std::ios::binary | std::ios::trunc)
        << "username = \"Player1\"\ntoken = \"QUJD\"\nsealing = \"other\"\n";
    expect(!achievements::read_account(folder), "a token sealed some other way is no account");
    expect(achievements::erase_account(folder) && !fs::exists(folder / achievements::kAccountFile),
           "signing out removes the file");
    fs::create_directories(folder / achievements::kAccountFile / "inside");
    expect(!achievements::erase_account(folder), "a file that stays is said to stay");
    fs::remove_all(folder / achievements::kAccountFile);
    fs::remove_all(folder);
}

void test_login_answers() {
    const auto signed_in = achievements::login_outcome({200, kSignedIn, {}});
    expect(signed_in.signed_in && signed_in.account.username == "Player1" &&
               signed_in.account.token == "ApiToken0123" && signed_in.display_name == "Player1",
           "a successful login gives the name and the token");
    const auto refused = achievements::login_outcome({401, kRefused, {}});
    expect(!refused.signed_in && refused.answered && refused.error == "Invalid user/password combination.",
           "a refusal gives the server's words");
    const auto unanswered =
        achievements::login_outcome({achievements::kRetryable, {}, "No answer (WinHTTP error 12002)"});
    expect(!unanswered.signed_in && !unanswered.answered && unanswered.error == "No answer (WinHTTP error 12002)",
           "no answer says why");
    const auto garbled = achievements::login_outcome({200, "<html>maintenance</html>", {}});
    expect(!garbled.signed_in && !garbled.error.empty(), "an answer that is not the API's is no sign-in");
}

void test_login_request() {
    auto transport = std::make_unique<Canned>(achievements::Response{200, kSignedIn, {}});
    Canned& canned = *transport;
    achievements::Requests requests(std::move(transport));
    std::atomic<bool> done = false;
    achievements::begin_login(requests, "Player1", "secret word", [&](achievements::LoginOutcome outcome) {
        done = outcome.signed_in;
    });
    expect(settles([&] { return done.load(); }), "the login is answered");
    const auto asked = canned.asked();
    expect(asked.size() == 1 && asked[0].url.ends_with("/dorequest.php") &&
               asked[0].body.find("r=login2") != std::string::npos &&
               asked[0].body.find("u=Player1") != std::string::npos &&
               asked[0].body.find("p=secret+word") != std::string::npos &&
               asked[0].url.find("secret") == std::string::npos,
           "the password goes in the request's body, never in its address");
    std::string empty_error;
    achievements::begin_login(requests, "", "x",
                              [&](achievements::LoginOutcome outcome) { empty_error = outcome.error; });
    expect(!empty_error.empty() && canned.asked().size() == 1, "a login without a name is refused before any request");
}

void test_requests_order() {
    auto transport = std::make_unique<Canned>(achievements::Response{200, "ok", {}}, 20ms);
    Canned& canned = *transport;
    std::atomic<int> answered = 0;
    std::vector<int> order;
    std::mutex order_mutex;
    {
        achievements::Requests requests(std::move(transport));
        std::vector<std::thread::id> callers;
        const auto note = [&](int index) {
            return [&, index](const achievements::Response&) {
                const std::lock_guard lock(order_mutex);
                order.push_back(index);
                callers.push_back(std::this_thread::get_id());
                ++answered;
            };
        };
        for (int index = 0; index < 3; ++index)
            requests.send({"https://example.invalid/" + std::to_string(index), {}, {}}, note(index));
        expect(settles([&] { return answered.load() == 3; }), "every request is answered");
        expect(order == std::vector<int>{0, 1, 2}, "requests are answered in the order they were sent");
        const auto threads = canned.threads();
        bool same = callers.size() == 3 && threads.size() == 3;
        for (std::size_t index = 0; same && index < callers.size(); ++index) same = callers[index] == threads[index];
        expect(same && callers[0] != std::this_thread::get_id(), "the answers are given on the requests' thread");
    }
}

// Lets one request through at a time, when the test says so.
class Gate final : public achievements::Transport {
public:
    achievements::Response perform(const achievements::Request&) override {
        std::unique_lock lock(mutex_);
        ++arrived_;
        changed_.notify_all();
        changed_.wait(lock, [this] { return open_; });
        return {200, "ok", {}};
    }
    void wait_for(int requests) {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return arrived_ >= requests; });
    }
    void open() {
        const std::lock_guard lock(mutex_);
        open_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    int arrived_ = 0;
    bool open_ = false;
};

void test_requests_end() {
    auto transport = std::make_unique<Gate>();
    Gate& gate = *transport;
    std::atomic<int> answered = 0;
    std::thread opener;
    {
        achievements::Requests requests(std::move(transport));
        for (int index = 0; index < 5; ++index)
            requests.send({"https://example.invalid/" + std::to_string(index), {}, {}},
                          [&](const achievements::Response&) { ++answered; });
        gate.wait_for(1);
        opener = std::thread([&] {
            std::this_thread::sleep_for(50ms);
            gate.open();
        });
    }
    opener.join();
    expect(answered.load() == 1, "at the end the request in flight is answered and the four queued are not");
}

void test_launcher_account() {
    const fs::path folder = scratch("disruptor-achievements-launcher");
    {
        AchievementsAccount account(folder,
                                    [] { return std::make_unique<Canned>(achievements::Response{401, kRefused}); });
        expect(account.shown().state == AchievementsAccount::State::signed_out, "a new player is signed out");
        account.sign_in("Player1", "wrong");
        expect(settles([&] { return !account.busy(); }), "a refused sign-in ends");
        const auto shown = account.shown();
        expect(shown.state == AchievementsAccount::State::signed_out &&
                   shown.message == "Invalid user/password combination.",
               "a refused sign-in shows the server's words");
        expect(!fs::exists(folder / achievements::kAccountFile), "and leaves no file");
    }
    {
        AchievementsAccount account(folder,
                                    [] { return std::make_unique<Canned>(achievements::Response{200, kSignedIn}); });
        account.sign_in("Player1", "right");
        expect(settles([&] { return !account.busy(); }), "a sign-in ends");
        expect(account.shown().state == AchievementsAccount::State::signed_in && account.shown().name == "Player1",
               "a sign-in shows the name");
        const auto kept = achievements::read_account(folder);
        expect(kept && kept->token == "ApiToken0123", "and keeps the token for the game");
    }
    {
        AchievementsAccount account(folder, [] { return std::make_unique<Canned>(achievements::Response{}); });
        expect(account.shown().state == AchievementsAccount::State::signed_in, "the next launcher starts signed in");
        const fs::path file = folder / achievements::kAccountFile;
        const std::string kept = read(file);
        fs::remove(file);
        fs::create_directories(file / "inside");
        account.sign_out();
        expect(account.shown().state == AchievementsAccount::State::signed_in && !account.shown().message.empty(),
               "a sign-out that cannot remove the file stays signed in and says so");
        fs::remove_all(file);
        std::ofstream(file, std::ios::binary) << kept;
        account.sign_out();
        expect(account.shown().state == AchievementsAccount::State::signed_out && !achievements::read_account(folder),
               "signing out forgets the account");
    }
    fs::remove_all(folder);
}

void test_guest_reads() {
    std::vector<std::uint8_t> ram(achievements::kRamSize);
    std::vector<std::uint8_t> pad(achievements::kScratchpadSize);
    ram[0] = 0x11;
    ram[achievements::kRamSize - 2] = 0x21;
    ram[achievements::kRamSize - 1] = 0x22;
    pad[0] = 0x31;
    pad[1] = 0x32;
    pad[achievements::kScratchpadSize - 1] = 0x3F;
    std::array<std::uint8_t, 4> got{};
    expect(achievements::read_guest(0, got.data(), 1, ram, pad) == 1 && got[0] == 0x11, "address 0 is main RAM's first byte");
    expect(achievements::read_guest(achievements::kRamSize - 2, got.data(), 4, ram, pad) == 4 &&
               got == std::array<std::uint8_t, 4>{0x21, 0x22, 0x31, 0x32},
           "a read over the end of main RAM goes on into the scratchpad, as RA's addresses run");
    got = {};
    expect(achievements::read_guest(achievements::kScratchpadAddress + achievements::kScratchpadSize - 1, got.data(), 4,
                                    ram, pad) == 1 &&
               got[0] == 0x3F,
           "a read stops at the scratchpad's end");
    expect(achievements::read_guest(0x300000, got.data(), 4, ram, pad) == 0, "nothing lies past the scratchpad");
}

// An ISO 9660 image of 2048-byte sectors with SYSTEM.CNF and a PS-X EXE, for rc_hash's walk.
class Image final : public achievements::DiscSectors {
public:
    Image(std::string_view boot_line, std::string_view name, std::uint32_t text_size) : sectors_(40) {
        auto& pvd = sectors_[16];
        pvd[0] = 1;
        std::memcpy(pvd.data() + 1, "CD001", 5);
        pvd[128] = 0x00;
        pvd[129] = 0x08;
        record(pvd.data() + 156, 20, 2048, std::string_view("\0", 1), true);
        std::uint8_t* at = sectors_[20].data();
        at += record(at, 20, 2048, std::string_view("\0", 1), true);
        at += record(at, 20, 2048, std::string_view("\1", 1), true);
        cnf_ = std::string(boot_line) + "\r\nTCB = 4\r\n";
        at += record(at, 21, static_cast<std::uint32_t>(cnf_.size()), "SYSTEM.CNF;1", false);
        exe_.assign(2048 + text_size, 0);
        std::memcpy(exe_.data(), "PS-X EXE", 8);
        std::memcpy(exe_.data() + 28, &text_size, 4);
        for (std::size_t index = 2048; index < exe_.size(); ++index) exe_[index] = static_cast<char>(index * 7);
        // The file on the disc is a sector longer than its header says, as on a real disc.
        record(at, 22, static_cast<std::uint32_t>(exe_.size() + 2048), std::string(name) + ";1", false);
        std::memcpy(sectors_[21].data(), cnf_.data(), cnf_.size());
        for (std::size_t offset = 0; offset < exe_.size(); offset += 2048)
            std::memcpy(sectors_[22 + offset / 2048].data(), exe_.data() + offset,
                        std::min<std::size_t>(2048, exe_.size() - offset));
        sectors_[22 + exe_.size() / 2048].fill(0x55);
    }

    bool read(std::uint32_t sector, std::uint8_t* user_data) override {
        if (sector >= sectors_.size()) return false;
        std::memcpy(user_data, sectors_[sector].data(), 2048);
        return true;
    }
    std::uint32_t first_sector() override { return 0; }

    // MD5 of the name as SYSTEM.CNF writes it, then the executable as long as its header says, plus the header.
    std::string expected(std::string_view name) const {
        md5_state_t state;
        md5_init(&state);
        md5_append(&state, reinterpret_cast<const md5_byte_t*>(name.data()), static_cast<int>(name.size()));
        md5_append(&state, reinterpret_cast<const md5_byte_t*>(exe_.data()), static_cast<int>(exe_.size()));
        std::array<md5_byte_t, 16> digest{};
        md5_finish(&state, digest.data());
        std::string hex;
        for (const md5_byte_t byte : digest) {
            constexpr std::string_view digits = "0123456789abcdef";
            hex += digits[byte >> 4];
            hex += digits[byte & 15];
        }
        return hex;
    }

private:
    static std::size_t record(std::uint8_t* at, std::uint32_t sector, std::uint32_t size, std::string_view name,
                              bool folder) {
        const std::size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
        at[0] = static_cast<std::uint8_t>(length);
        std::memcpy(at + 2, &sector, 4);
        std::memcpy(at + 10, &size, 4);
        at[25] = folder ? 2 : 0;
        at[32] = static_cast<std::uint8_t>(name.size());
        std::memcpy(at + 33, name.data(), name.size());
        return length;
    }

    std::vector<std::array<std::uint8_t, 2048>> sectors_;
    std::string cnf_;
    std::string exe_;
};

void test_disc_hash() {
    Image image("BOOT = cdrom:\\slus_123.45;1", "SLUS_123.45", 3 * 2048);
    const auto hash = achievements::playstation_hash(image);
    expect(hash && *hash == image.expected("slus_123.45"),
           "the hash is the boot name as SYSTEM.CNF writes it and the executable as its header sizes it");
    Image other("BOOT = cdrom:\\slus_123.45;1", "SLUS_123.45", 2 * 2048);
    expect(achievements::playstation_hash(other) != hash, "another executable gives another hash");
}

// DISRUPTOR_DISC_BIN names the player's MODE2/2352 image of the US disc, whose hash RetroAchievements knows.
class RawImage final : public achievements::DiscSectors {
public:
    explicit RawImage(const fs::path& path) : file_(path, std::ios::binary) {}
    bool read(std::uint32_t sector, std::uint8_t* user_data) override {
        std::array<char, 2352> raw{};
        file_.seekg(static_cast<std::streamoff>(sector) * 2352);
        if (!file_.read(raw.data(), raw.size())) return false;
        std::memcpy(user_data, raw.data() + (raw[15] == 1 ? 16 : 24), 2048);
        return true;
    }
    std::uint32_t first_sector() override { return 0; }

private:
    std::ifstream file_;
};

std::string environment(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    std::string text;
    if (_dupenv_s(&value, &length, name) == 0 && value) text = value;
    std::free(value);
    return text;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

void test_real_disc() {
    const std::string image = environment("DISRUPTOR_DISC_BIN");
    if (image.empty()) return;
    RawImage disc(image);
    const auto hash = achievements::playstation_hash(disc);
    std::cout << "Disc hash: " << hash.value_or("none") << "\n";
    expect(hash == "13d1301bf01065b8cc584a4b42982de7", "the US disc gives the hash RetroAchievements knows as game 8876");
}

constexpr const char* kTokenLogin =
    R"({"Success":true,"User":"PLAYER1","Token":"ApiToken0123","Score":0,"SoftcoreScore":0,"Messages":0})";
constexpr const char* kTwoAchievements =
    R"({"Success":true,"GameId":8876,"Title":"Disruptor","ConsoleId":12,)"
    R"("ImageIconUrl":"https://media.retroachievements.org/Images/000001.png",)"
    R"("RichPresenceGameId":8876,"RichPresencePatch":"","Sets":[{"AchievementSetId":1,"GameId":8876,"Title":null,)"
    R"("Type":"core","ImageIconUrl":"https://media.retroachievements.org/Images/000001.png","Achievements":[)"
    R"({"ID":5501,"Title":"Health five","Description":"Have five health","Flags":3,"Points":5,)"
    R"("MemAddr":"0xH077660=5","Author":"Tester","BadgeName":"00234","Created":1367266583,"Modified":1376929305},)"
    R"({"ID":5502,"Title":"Scratch nine","Description":"A nine in the scratchpad","Flags":3,"Points":5,)"
    R"("MemAddr":"0xH200010=9","Author":"Tester","BadgeName":"00235","Created":1367266583,"Modified":1376929305})"
    R"(],"Leaderboards":[]}]})";

// RetroAchievements' answers by request, and every request it was sent.
class Server final : public achievements::Transport {
public:
    explicit Server(std::string set = kTwoAchievements,
                    std::string started = R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[]})")
        : set_(std::move(set)), started_(std::move(started)) {}

    achievements::Response perform(const achievements::Request& request) override {
        std::unique_lock lock(mutex_);
        sent_.push_back(request);
        const auto asks = [&](std::string_view what) { return request.body.find(what) != std::string::npos; };
        if (asks("a=5502")) released_.wait(lock, [this] { return !holding_; });
        if (asks("r=login2")) return {200, kTokenLogin, {}};
        if (asks("r=achievementsets")) return {200, set_, {}};
        if (asks("r=startsession")) return {200, started_, {}};
        if (asks("r=awardachievement") && asks("a=5501"))
            return {200, R"({"Success":true,"Score":5,"SoftcoreScore":5,"AchievementID":5501,"AchievementsRemaining":1})",
                    {}};
        if (asks("r=awardachievement") && asks("a=5502"))
            return {200, R"({"Success":true,"Score":10,"SoftcoreScore":10,"AchievementID":5502,"AchievementsRemaining":0})",
                    {}};
        if (asks("r=awardachievement")) {
            const std::size_t at = request.body.find("a=") + 2;
            const std::string id = request.body.substr(at, request.body.find('&', at) - at);
            return {200, R"({"Success":true,"Score":5,"SoftcoreScore":5,"AchievementID":)" + id +
                             R"(,"AchievementsRemaining":1})", {}};
        }
        if (asks("r=ping")) return {200, R"({"Success":true})", {}};
        return {404, R"({"Success":false,"Error":"Unknown request"})", {}};
    }
    std::vector<achievements::Request> sent() {
        const std::lock_guard lock(mutex_);
        return sent_;
    }
    // Holds the answer to the last unlock until let go, so no frame can see it before the test wants one to.
    void hold(bool held) {
        const std::lock_guard lock(mutex_);
        holding_ = held;
        released_.notify_all();
    }

private:
    std::string set_;
    std::string started_;
    std::mutex mutex_;
    std::condition_variable released_;
    std::vector<achievements::Request> sent_;
    bool holding_ = false;
};

bool any(const std::vector<achievements::Request>& sent, std::initializer_list<std::string_view> parts) {
    for (const auto& request : sent) {
        bool all = true;
        for (const std::string_view part : parts) all = all && request.body.find(part) != std::string::npos;
        if (all) return true;
    }
    return false;
}

struct Played {
    std::vector<achievements::Notice> notices;
    std::vector<achievements::Request> sent;
    std::vector<std::string> lines;
};

// Plays both achievements through. `last_frame`: whether a frame follows the server's word that the game is complete.
Played play_both(bool last_frame) {
    std::vector<std::uint8_t> ram(achievements::kRamSize);
    std::vector<std::uint8_t> pad(achievements::kScratchpadSize);
    auto transport = std::make_unique<Server>();
    Server& server = *transport;
    Played played;
    std::mutex lines_mutex;
    const auto said = [&](std::string_view part) {
        const std::lock_guard lock(lines_mutex);
        for (const auto& line : played.lines)
            if (line.find(part) != std::string::npos) return true;
        return false;
    };
    {
        achievements::Session session({"Player1", "ApiToken0123"}, "13d1301bf01065b8cc584a4b42982de7",
                                      std::move(transport), {ram, pad}, [&](std::string_view line) {
                                          const std::lock_guard lock(lines_mutex);
                                          played.lines.emplace_back(line);
                                      });
        const auto step = [&] {
            session.frame();
            for (auto& notice : session.take_notices()) played.notices.push_back(std::move(notice));
        };
        expect(settles([&] {
                   step();
                   return session.loaded();
               }),
               "the session signs in and loads the game");
        expect(session.progress() == "0 of 2 unlocked", "and tells how far the player is");
        step();
        step();
        ram[0x077660] = 5;
        step();
        expect(settles([&] { return any(server.sent(), {"r=awardachievement", "a=5501", "h=0"}); }),
               "a condition met in main RAM unlocks its achievement, in casual");
        pad[0x10] = 9;
        server.hold(true);
        step();
        expect(settles([&] { return any(server.sent(), {"r=awardachievement", "a=5502", "h=0"}); }),
               "and one met in the scratchpad unlocks its own");
        server.hold(false);
        expect(session.progress() == "2 of 2 unlocked", "both count as unlocked");
        expect(settles([&] { return said("completed"); }), "the server's answer says the game is complete");
        if (last_frame) step();
        played.sent = server.sent();
    }
    return played;
}

std::vector<achievements::NoticeKind> kinds_of(const std::vector<achievements::Notice>& notices) {
    std::vector<achievements::NoticeKind> kinds;
    for (const auto& notice : notices) kinds.push_back(notice.kind);
    return kinds;
}

void test_session() {
    const Played played = play_both(true);
    const auto& sent = played.sent;
    expect(any(sent, {"r=login2", "u=Player1", "t=ApiToken0123"}), "it signs in with the token");
    expect(any(sent, {"r=achievementsets", "m=13d1301bf01065b8cc584a4b42982de7"}), "it asks for the disc's hash");
    expect(any(sent, {"r=startsession", "h=0"}), "its session is a casual one");
    bool https = !sent.empty();
    for (const auto& request : sent) https = https && request.url.starts_with("https://");
    expect(https, "every request goes over https");
    using achievements::NoticeKind;
    expect(kinds_of(played.notices) == std::vector<NoticeKind>{NoticeKind::signed_in, NoticeKind::game_found,
                                                               NoticeKind::unlocked, NoticeKind::unlocked,
                                                               NoticeKind::mastered},
           "the notices are the sign-in, the game, both unlocks and the completion, in order");
    const auto& notices = played.notices;
    expect(notices.size() == 5 && notices[1].title == "Disruptor" && notices[2].title == "Health five" &&
               notices[2].badge.find("00234") != std::string::npos,
           "they carry the game's and the achievement's names and the badge's address");
    bool leaked = played.lines.empty();
    bool named = false;
    for (std::string line : played.lines) {
        named = named || line.find("<player> logged in") != std::string::npos;
        for (char& symbol : line) symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
        leaked = leaked || line.find("player1") != std::string::npos || line.find("apitoken0123") != std::string::npos;
    }
    expect(!leaked && named, "the log names neither the player, as either side spells the name, nor the token");
}

// Without the session's last frame rcheevos 12.5 reads a null game here and the test dies.
void test_session_end_with_completion_pending() {
    const Played played = play_both(false);
    using achievements::NoticeKind;
    expect(kinds_of(played.notices) == std::vector<NoticeKind>{NoticeKind::signed_in, NoticeKind::game_found,
                                                               NoticeKind::unlocked, NoticeKind::unlocked},
           "a session ended with the completion still pending ends cleanly");
}

constexpr const char* kCountedSet =
    R"({"Success":true,"GameId":8876,"Title":"Disruptor","ConsoleId":12,)"
    R"("ImageIconUrl":"https://media.retroachievements.org/Images/000001.png",)"
    R"("RichPresenceGameId":8876,"RichPresencePatch":"","Sets":[{"AchievementSetId":1,"GameId":8876,"Title":null,)"
    R"("Type":"core","ImageIconUrl":"https://media.retroachievements.org/Images/000001.png","Achievements":[)"
    R"({"ID":5601,"Title":"Three frames","Description":"A one for three frames","Flags":3,"Points":10,)"
    R"("MemAddr":"0xH000010=1.3.","Author":"Tester","BadgeName":"00301","Created":1367266583,"Modified":1376929305},)"
    R"({"ID":5602,"Title":"Off the record","Description":"A two","Flags":5,"Points":5,)"
    R"("MemAddr":"0xH000020=2","Author":"Tester","BadgeName":"00302","Created":1367266583,"Modified":1376929305})"
    R"(],"Leaderboards":[]}]})";

// A session over `server` with its memory, stepped by hand.
struct Rig {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(achievements::kRamSize);
    std::vector<std::uint8_t> pad = std::vector<std::uint8_t>(achievements::kScratchpadSize);
    Server* server = nullptr;
    std::vector<achievements::Notice> notices;
    std::unique_ptr<achievements::Session> session;

    explicit Rig(std::unique_ptr<Server> answers, achievements::SessionOptions options = {}) {
        server = answers.get();
        session = std::make_unique<achievements::Session>(achievements::Account{"Player1", "ApiToken0123"},
                                                          "13d1301bf01065b8cc584a4b42982de7", std::move(answers),
                                                          achievements::GuestMemory{ram, pad}, nullptr, options);
    }
    void step() {
        session->frame();
        for (auto& notice : session->take_notices()) notices.push_back(std::move(notice));
    }
    bool load() {
        return settles([&] {
            step();
            return session->loaded();
        });
    }
    bool unlocked(std::string_view title) const {
        for (const auto& notice : notices)
            if (notice.kind == achievements::NoticeKind::unlocked && notice.title == title) return true;
        return false;
    }
};

void test_profile_address() {
    expect(achievements::profile_address("Player") == "https://retroachievements.org/user/Player" &&
               achievements::profile_address("a b/../?") == "https://retroachievements.org/user/a%20b%2F..%2F%3F",
           "the profile address carries the name, anything but letters, digits and _-. escaped");
}

void test_policy() {
    using achievements::Act;
    using achievements::Verdict;
    bool casual = true;
    for (const Act act : {Act::load_state, Act::use_cheat, Act::start_mission, Act::debug_tools, Act::input_replay})
        casual = casual && achievements::judge(false, act) == Verdict::allowed && !achievements::explain(act).empty();
    expect(casual, "a casual session allows everything, and each act has a line for the player");
    expect(achievements::judge(true, Act::load_state) == Verdict::refused &&
               achievements::judge(true, Act::use_cheat) == Verdict::refused &&
               achievements::judge(true, Act::start_mission) == Verdict::refused,
           "hardcore refuses state loads, cheats and the Debug tab's mission start");
    expect(achievements::judge(true, Act::debug_tools) == Verdict::ends_hardcore &&
               achievements::judge(true, Act::input_replay) == Verdict::ends_hardcore,
           "a debug tool or an input replay turns a hardcore session casual");
}

void test_progress_file() {
    const fs::path folder = scratch("disruptor-achievements-progress");
    const fs::path state = folder / "slot_3.pst";
    const fs::path other = folder / "slot_4.pst";
    std::ofstream(state, std::ios::binary) << std::string(200000, 'a') << "state three";
    std::ofstream(other, std::ios::binary) << std::string(200000, 'a') << "state four";
    const fs::path file = achievements::progress_file_for(state);
    expect(file == folder / "slot_3.pst.achievements", "the progress sits beside its state");
    const auto digest = achievements::state_digest(state);
    const auto other_digest = achievements::state_digest(other);
    expect(digest && digest->size() == 32 && other_digest && *digest != *other_digest &&
               achievements::state_digest(state) == digest && !achievements::state_digest(folder / "none.pst"),
           "a state's digest is its content's, the same twice and different for another state");
    const std::string hash = "13d1301bf01065b8cc584a4b42982de7";
    const std::vector<std::uint8_t> progress = {1, 2, 3, 0, 255, 7};
    expect(achievements::write_progress(file, hash, *digest, progress), "progress is written");
    expect(achievements::read_progress(file, hash, *digest) == progress, "and read back as it was");
    expect(!achievements::read_progress(file, "00000000000000000000000000000000", *digest),
           "progress of another disc is not taken");
    expect(!achievements::read_progress(file, hash, *other_digest),
           "nor progress saved with another state, as one left when that state was overwritten");
    std::string bytes = read(file);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes.substr(0, bytes.size() - 1);
    expect(!achievements::read_progress(file, hash, *digest), "a cut file is not taken");
    expect(achievements::write_progress(file, hash, *digest, progress), "a new write replaces it");
    expect(!achievements::write_progress(file, hash, *digest, {}) && !fs::exists(file),
           "a write that fails takes the older file away");
    expect(!fs::exists(fs::path(file) += ".new"), "no half-written file is left");
    const fs::path real_size = folder / "real_size.pst";
    std::ofstream(real_size, std::ios::binary) << std::string(4729297, 's');
    const auto began = std::chrono::steady_clock::now();
    const auto digested = achievements::state_digest(real_size);
    const auto took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    std::cout << "State digest of a 4729297-byte state: " << took << " ms\n";
    expect(digested.has_value(), "a state of the real size is digested");
    fs::remove_all(folder);
}

void test_run_record() {
    achievements::RunRecord fresh;
    expect(fresh.may_start_hardcore() && !fresh.take_lapse(), "a run watched from its start may be hardcore");
    for (const auto act : {achievements::Act::load_state, achievements::Act::use_cheat, achievements::Act::start_mission}) {
        achievements::RunRecord run;
        run.allowed(act);
        expect(!run.may_start_hardcore(), "a state load, a cheat or a mission start before it keeps it casual");
    }
    achievements::RunRecord second;
    second.session_started();
    expect(!second.may_start_hardcore(), "so does a session before it, as one signed out of");
    achievements::RunRecord paused;
    paused.switched_off();
    expect(!paused.may_start_hardcore(), "and RetroAchievements switched off before it");
    expect(paused.take_lapse() && !paused.take_lapse(), "a switch-off is reported once, to end a hardcore session");
    paused.session_started();
    expect(!paused.may_start_hardcore(), "starting a session does not wipe the record");
    achievements::RunRecord raced;
    const bool asked = raced.may_start_hardcore();
    raced.switched_off();
    raced.session_started();
    expect(asked && raced.take_lapse(), "a switch-off between the question and the session's start is still reported");
}

constexpr std::string_view kPng = "\x89PNG\r\n\x1a\n-picture-";

// RetroAchievements' media server: a picture for the one badge it has, nothing for the rest.
class Media final : public achievements::Transport {
public:
    explicit Media(std::string answer = std::string(kPng)) : answer_(std::move(answer)) {}

    achievements::Response perform(const achievements::Request& request) override {
        const std::lock_guard lock(mutex_);
        asked_.push_back(request);
        if (request.url.ends_with("/Badge/00234.png")) return {200, answer_, {}};
        return {404, "missing", {}};
    }
    std::vector<achievements::Request> asked() {
        const std::lock_guard lock(mutex_);
        return asked_;
    }

private:
    std::string answer_;
    std::mutex mutex_;
    std::vector<achievements::Request> asked_;
};

void test_badges() {
    expect(achievements::badge_file_name("https://media.retroachievements.org/Badge/00234_lock.png") ==
                   "Badge_00234_lock.png" &&
               achievements::badge_file_name("https://media.retroachievements.org/Images/000001.png") ==
                   "Images_000001.png",
           "badges and game icons are kept under their own names");
    bool refused = true;
    for (const char* url : {"https://example.org/Badge/00234.png", "http://media.retroachievements.org/Badge/1.png",
                            "https://media.retroachievements.org/Badge/../account.png",
                            "https://media.retroachievements.org/Badge/1.png?x=1",
                            "https://media.retroachievements.org/Other/1.png",
                            "https://media.retroachievements.org/Badge/.png"})
        refused = refused && achievements::badge_file_name(url).empty();
    expect(refused, "any other address, a path out of the folder among them, has no name");

    const fs::path folder = scratch("disruptor-achievements-badges");
    const std::string url = "https://media.retroachievements.org/Badge/00234.png";
    {
        auto transport = std::make_unique<Media>();
        Media& media = *transport;
        achievements::Badges badges(folder, std::move(transport));
        expect(badges.png(url) == nullptr, "a picture not yet here starts its fetch");
        expect(settles([&] { return badges.png(url) != nullptr; }), "and arrives");
        expect(*badges.png(url) == kPng, "as the server sent it");
        expect(badges.png("https://example.org/Badge/00234.png") == nullptr &&
                   badges.png("https://media.retroachievements.org/Badge/00999.png") == nullptr,
               "a foreign address and a missing picture give nothing");
        settles([&] { return media.asked().size() == 2; });
        const auto asked = media.asked();
        expect(asked.size() == 2 && asked[0].url == url && asked[0].body.empty() && asked[0].content_type.empty(),
               "each picture is asked for once, with a plain GET, and the foreign address never");
    }
    expect(read(folder / "Badge_00234.png") == kPng && !fs::exists(folder / "Badge_00999.png"),
           "a fetched picture is kept in the folder, a missing one is not");
    {
        auto transport = std::make_unique<Media>();
        Media& media = *transport;
        achievements::Badges badges(folder, std::move(transport));
        const auto kept = badges.png(url);
        expect(kept && *kept == kPng && media.asked().empty(), "the next run reads it from the folder without asking");
    }
    fs::remove_all(folder);
    {
        auto transport = std::make_unique<Media>("<html>not a picture</html>");
        Media& media = *transport;
        achievements::Badges badges(folder, std::move(transport));
        (void)badges.png(url);
        settles([&] { return media.asked().size() == 1; });
        std::this_thread::sleep_for(50ms);
        expect(badges.png(url) == nullptr && !fs::exists(folder / "Badge_00234.png"),
               "an answer that is not a PNG is neither shown nor kept");
    }
    fs::remove_all(folder);
}

void test_chime() {
    achievements::Chime chime;
    std::vector<std::int16_t> stereo(2 * 4800, 0);
    chime.mix(stereo.data(), 4800, 48000);
    bool silent = true;
    for (const std::int16_t sample : stereo) silent = silent && sample == 0;
    expect(silent && !chime.playing(), "nothing plays until asked");
    chime.start(achievements::Chime::Tune::unlock);
    chime.mix(stereo.data(), 4800, 48000);
    bool sounding = false;
    bool same = true;
    for (std::size_t frame = 0; frame < 4800; ++frame) {
        sounding = sounding || stereo[2 * frame] != 0;
        same = same && stereo[2 * frame] == stereo[2 * frame + 1];
    }
    expect(sounding && same && chime.playing(), "an unlock plays the same on both sides");
    std::vector<std::int16_t> loud(2 * 4800, 32767);
    chime.start(achievements::Chime::Tune::mastery);
    chime.mix(loud.data(), 4800, 48000);
    bool clipped = true;
    for (const std::int16_t sample : loud) clipped = clipped && sample > 0;
    expect(clipped, "added to loud game sound it stops at the top instead of wrapping round");
    std::vector<std::int16_t> rest(2 * 48000, 0);
    chime.mix(rest.data(), 48000, 48000);
    chime.mix(rest.data(), 48000, 48000);
    expect(!chime.playing(), "and ends by itself");
}

// A WAV file of `channels` interleaved `samples` (raw little-endian bytes), with any chunks put before its format.
std::string wav(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits, const std::string& samples,
                const std::string& before = "", std::uint32_t data_size = UINT32_MAX) {
    const auto word = [](std::uint32_t value) {
        return std::string{static_cast<char>(value), static_cast<char>(value >> 8), static_cast<char>(value >> 16),
                           static_cast<char>(value >> 24)};
    };
    const auto half = [](std::uint16_t value) { return std::string{static_cast<char>(value), static_cast<char>(value >> 8)}; };
    std::string described = half(format) + half(channels) + word(rate) + word(rate * channels * bits / 8) +
                            half(static_cast<std::uint16_t>(channels * bits / 8)) + half(bits);
    if (format == 0xFFFE) described += half(22) + half(bits) + word(3) + half(1) + std::string(14, '\0');
    const std::string body = before + "fmt " + word(static_cast<std::uint32_t>(described.size())) + described + "data" +
                             word(data_size == UINT32_MAX ? static_cast<std::uint32_t>(samples.size()) : data_size) + samples;
    return "RIFF" + word(static_cast<std::uint32_t>(4 + body.size())) + "WAVE" + body;
}

std::string halves(std::initializer_list<int> values) {
    std::string bytes;
    for (const int value : values) {
        bytes += static_cast<char>(value & 0xFF);
        bytes += static_cast<char>((value >> 8) & 0xFF);
    }
    return bytes;
}

void test_wav() {
    std::string why;
    const auto stereo = achievements::read_wav(wav(1, 2, 22050, 16, halves({1000, -1000, 2000, -2000})), why);
    expect(stereo && stereo->rate == 22050 && stereo->stereo == std::vector<std::int16_t>{1000, -1000, 2000, -2000},
           "16-bit stereo is taken as it is");
    const auto bytes = achievements::read_wav(wav(1, 1, 8000, 8, std::string{'\x80', '\xFF', '\x00'}), why);
    expect(bytes && bytes->stereo == std::vector<std::int16_t>{0, 0, 32512, 32512, -32768, -32768},
           "8-bit sound is centred on 128 and a single channel goes to both sides");
    const auto deep = achievements::read_wav(wav(1, 1, 44100, 24, std::string{'\x56', '\x34', '\x12', '\x00', '\x00', '\x80'}), why);
    expect(deep && deep->stereo == std::vector<std::int16_t>{0x1234, 0x1234, -32768, -32768}, "24-bit keeps its top 16 bits");
    const auto wide = achievements::read_wav(wav(1, 1, 44100, 32, std::string{'\x00', '\x00', '\xFF', '\x7F'}), why);
    expect(wide && wide->stereo == std::vector<std::int16_t>{0x7FFF, 0x7FFF}, "and so does 32-bit");
    std::string floats;
    for (const float value : {0.5f, 2.0f, std::nanf("")}) floats.append(reinterpret_cast<const char*>(&value), sizeof value);
    const auto real = achievements::read_wav(wav(3, 1, 48000, 32, floats), why);
    expect(real && real->stereo == std::vector<std::int16_t>{16384, 16384, 32767, 32767, 0, 0},
           "float sound is scaled, held within full scale, and a NaN is silence");
    const auto extended = achievements::read_wav(wav(0xFFFE, 2, 48000, 16, halves({7, 8})), why);
    expect(extended && extended->stereo == std::vector<std::int16_t>{7, 8}, "the extensible format names its PCM inside");
    const auto three = achievements::read_wav(wav(1, 3, 48000, 16, halves({1, 2, 3, 4, 5, 6})), why);
    expect(three && three->stereo == std::vector<std::int16_t>{1, 2, 4, 5}, "of three channels the first two play");
    const auto listed = achievements::read_wav(wav(1, 1, 48000, 16, halves({9}), std::string("LIST\x03\x00\x00\x00" "abc\0", 12)), why);
    expect(listed && listed->stereo == std::vector<std::int16_t>{9, 9}, "a chunk before the format is passed over, odd ones with their pad byte");
    const auto unfinished = achievements::read_wav(wav(1, 1, 48000, 16, halves({3, 4}), "", 0xFFFFFFF0u), why);
    expect(unfinished && unfinished->frames() == 2, "a data size never filled in reads to the file's end");

    const auto refused = [&](const std::string& bytes, const char* reason) {
        why.clear();
        return !achievements::read_wav(bytes, why) && why.find(reason) != std::string::npos;
    };
    expect(refused("RIFX....WAVE", "not a WAV"), "a file that is no WAV is refused");
    expect(refused(wav(2, 1, 22050, 4, std::string(16, '\0')), "compressed"), "and so is a compressed one");
    expect(refused(wav(1, 1, 4000, 16, halves({1})), "out of range"), "and one at a rate the game cannot play");
    expect(refused(wav(1, 0, 22050, 16, halves({1})), "out of range"), "and one of no channels");
    expect(refused(wav(1, 1, 22050, 16, ""), "no sound"), "and one with no sound");
    expect(refused(wav(1, 1, 8000, 8, std::string(8000 * 10 + 1, '\x80')), "longer than 10 seconds"), "and one longer than 10 seconds");
    expect(refused(std::string(achievements::kMostSoundBytes + 1, '\0'), "larger than 8 MB"), "and a file larger than 8 MB");
    const std::string head = wav(1, 1, 22050, 16, halves({1}));
    expect(refused(head.substr(0, 20), "does not say"), "and one cut off before its format");
}

void test_unlock_sound_file() {
    const fs::path folder = scratch("disruptor-achievements-sound");
    const fs::path chosen = folder / "picked" / "ding.wav", kept = folder / achievements::kUnlockSoundFile;
    fs::create_directories(chosen.parent_path());
    const auto write = [](const fs::path& file, const std::string& bytes) { std::ofstream(file, std::ios::binary) << bytes; };
    std::string why = "unset";
    expect(!achievements::load_unlock_sound(folder, why) && why == "unset", "with no sound kept nothing loads and nothing is wrong");
    const std::string sound = wav(1, 1, 22050, 16, halves({100, 200, 300}));
    write(chosen, sound);
    expect(achievements::keep_unlock_sound(chosen, folder).empty() && read(kept) == sound, "a WAV the player picks is copied whole");
    fs::remove(chosen);
    why.clear();
    const auto loaded = achievements::load_unlock_sound(folder, why);
    expect(loaded && loaded->frames() == 3 && why.empty(), "and plays from the copy when the original is gone");
    write(chosen, "not a sound at all");
    expect(achievements::keep_unlock_sound(chosen, folder).find("not a WAV") != std::string::npos && read(kept) == sound,
           "a file that is no sound is refused and the kept one stays");
    expect(achievements::keep_unlock_sound(folder / "missing.wav", folder) == "the file cannot be read", "a missing file is named so");
    expect(!fs::exists(fs::path(kept).concat(".new")), "no half-written copy is left");
    write(kept, "broken");
    why.clear();
    expect(!achievements::load_unlock_sound(folder, why) && why.find("not a WAV") != std::string::npos,
           "a kept file that no longer reads says why it does not play");
    expect(achievements::drop_unlock_sound(folder) && !fs::exists(kept), "Default takes the sound away");
    expect(achievements::drop_unlock_sound(folder), "and finds nothing wrong when there is none");
    fs::remove_all(folder);
}

void test_chime_clip() {
    achievements::Chime chime;
    auto clip = std::make_shared<achievements::Clip>();
    clip->rate = 24000;
    clip->stereo = {0, 0, 1000, -1000, 2000, -2000, 3000, -3000};
    chime.use(clip);
    chime.start(achievements::Chime::Tune::unlock);
    std::vector<std::int16_t> out(2 * 10, 0);
    chime.mix(out.data(), 10, 48000);
    expect(out == std::vector<std::int16_t>{0, 0, 500, -500, 1000, -1000, 1500, -1500, 2000, -2000, 2500, -2500, 0, 0, 0, 0, 0, 0, 0, 0}
               && !chime.playing(),
           "the player's sound plays at its own rate, read between its frames, and ends at its last");
    std::vector<std::int16_t> loud(2 * 4, 32000);
    chime.start(achievements::Chime::Tune::mastery);
    chime.mix(loud.data(), 4, 24000);
    expect(loud[2] == 32767 && loud[3] == 31000, "it plays for a mastery too, held within full scale");
    chime.use(nullptr);
    chime.start(achievements::Chime::Tune::unlock);
    std::vector<std::int16_t> bells(2 * 4800, 0);
    chime.mix(bells.data(), 4800, 48000);
    bool sounding = false;
    for (const std::int16_t sample : bells) sounding = sounding || sample != 0;
    expect(sounding, "without one the chime's own tune plays again");
    auto short_clip = std::make_shared<achievements::Clip>();
    short_clip->rate = 24000;
    short_clip->stereo = {5, 5};
    chime.use(short_clip);
    chime.start(achievements::Chime::Tune::unlock);
    std::vector<std::int16_t> again(2 * 4800, 0);
    chime.mix(again.data(), 4800, 48000);
    sounding = false;
    for (const std::int16_t sample : again) sounding = sounding || (sample != 0 && sample != 5);
    expect(sounding, "a sound of one frame is not taken: the chime's own tune plays");
}

void test_session_progress() {
    Rig rig(std::make_unique<Server>(kCountedSet), {.unofficial = true});
    expect(rig.session->save_progress().empty(), "before the game is loaded there is no progress to keep");
    expect(rig.load(), "the session loads the set");
    rig.step();
    const auto list = rig.session->achievements();
    const auto entry = [&](std::uint32_t id) {
        for (const auto& found : list)
            if (found.id == id) return found;
        return achievements::Entry{};
    };
    expect(list.size() == 2 && entry(5601).badge.ends_with("/Badge/00301.png") &&
               entry(5601).locked_badge.ends_with("/Badge/00301_lock.png") && !entry(5601).unlocked &&
               entry(5601).points == 10,
           "the list has each achievement with both pictures");
    expect(entry(5602).unofficial && !entry(5601).unofficial, "an unofficial one is marked so");
    rig.ram[0x10] = 1;
    rig.step();
    rig.step();
    const std::vector<std::uint8_t> two_frames = rig.session->save_progress();
    expect(!two_frames.empty(), "two of the three frames are kept");
    expect(rig.session->load_progress({}), "a state without progress starts the counting over");
    rig.step();
    expect(!rig.unlocked("Three frames"), "so one more frame is not enough");
    expect(rig.session->load_progress(two_frames), "the kept progress goes back");
    rig.step();
    expect(rig.unlocked("Three frames"), "and one more frame unlocks it");
    expect(settles([&] { return any(rig.server->sent(), {"r=awardachievement", "a=5601"}); }),
           "the unlock goes to the server");
    rig.ram[0x20] = 2;
    rig.step();
    bool unofficial = false;
    for (const auto& notice : rig.notices)
        unofficial = unofficial || (notice.kind == achievements::NoticeKind::unlocked && notice.unofficial &&
                                    notice.title == "Off the record");
    std::this_thread::sleep_for(50ms);
    expect(unofficial && !any(rig.server->sent(), {"r=awardachievement", "a=5602"}),
           "an unofficial unlock is shown and never sent");
}

void test_session_hardcore() {
    Rig rig(std::make_unique<Server>(), {.hardcore = true});
    expect(rig.load() && rig.session->hardcore(), "a hardcore session loads in hardcore");
    expect(any(rig.server->sent(), {"r=startsession", "h=1"}), "and tells the server so");
    rig.session->end_hardcore();
    expect(!rig.session->hardcore(), "it can go on in casual without a restart");
    rig.step();
    rig.ram[0x077660] = 5;
    rig.step();
    expect(settles([&] { return any(rig.server->sent(), {"r=awardachievement", "a=5501", "h=0"}); }),
           "and its unlocks are casual from then on");
}

void test_session_encore() {
    const std::string started =
        R"({"Success":true,"Unlocks":[{"ID":5501,"When":1591132445}],"HardcoreUnlocks":[],"ServerNow":1704430000})";
    for (const bool encore : {false, true}) {
        Rig rig(std::make_unique<Server>(kTwoAchievements, started), {.encore = encore});
        expect(rig.load(), "the session loads with one achievement already unlocked");
        rig.step();
        rig.ram[0x077660] = 5;
        rig.step();
        rig.step();
        expect(rig.unlocked("Health five") == encore,
               encore ? "Encore lets an unlocked achievement trigger again" : "without Encore it stays quiet");
    }
}

void test_session_sign_out() {
    Rig rig(std::make_unique<Server>());
    expect(rig.load(), "the session loads");
    rig.step();
    rig.session->sign_out();
    rig.step();
    bool said = false;
    for (const auto& notice : rig.notices) said = said || notice.kind == achievements::NoticeKind::signed_out;
    expect(said && !rig.session->loaded() && rig.session->achievements().empty() && rig.session->progress().empty(),
           "signing out unloads the game and empties the list");
}

// DISRUPTOR_ACHIEVEMENTS_LIVE=1 asks the real server about a name that is not there, through this platform's transport.
void test_live_refusal() {
    if (!live()) return;
    std::atomic<bool> done = false;
    achievements::LoginOutcome outcome;
    std::string address;
    {
        // The real transport behind one that notes the address, so the request's scheme is seen.
        class Noted final : public achievements::Transport {
        public:
            explicit Noted(std::string& address)
                : address_(address), real_(achievements::platform_transport(achievements::user_agent())) {}
            achievements::Response perform(const achievements::Request& request) override {
                address_ = request.url;
                return real_->perform(request);
            }

        private:
            std::string& address_;
            std::unique_ptr<achievements::Transport> real_;
        };
        achievements::Requests requests(std::make_unique<Noted>(address));
        achievements::begin_login(requests, "DisruptorRecompNoSuchPlayer", "not a password",
                                  [&](achievements::LoginOutcome answer) {
                                      outcome = std::move(answer);
                                      done = true;
                                  });
        expect(settles([&] { return done.load(); }), "the server answers");
    }
    std::cout << "Live request: " << address << "\nLive answer: " << outcome.error << "\n";
    expect(address == "https://retroachievements.org/dorequest.php", "the login goes to the server over https");
    expect(!outcome.signed_in && outcome.answered && outcome.error.starts_with("Invalid user/password combination"),
           "the server refuses a name it does not know, in its own words");
}

} // namespace

int main(int count, char** words) {
    // One test by name, for repeating a single one.
    const std::string only = count > 1 ? words[1] : "";
    const std::vector<std::pair<std::string, void (*)()>> tests = {
        {"versions", test_versions},           {"account_file", test_account_file},
        {"login_answers", test_login_answers}, {"login_request", test_login_request},
        {"requests_order", test_requests_order}, {"requests_end", test_requests_end},
        {"launcher_account", test_launcher_account}, {"guest_reads", test_guest_reads},
        {"disc_hash", test_disc_hash},         {"real_disc", test_real_disc},
        {"session", test_session},             {"session_end", test_session_end_with_completion_pending},
        {"profile_address", test_profile_address}, {"policy", test_policy},
        {"run_record", test_run_record},               {"progress_file", test_progress_file},
        {"badges", test_badges},               {"chime", test_chime},
        {"wav", test_wav},                     {"chime_clip", test_chime_clip},
        {"unlock_sound_file", test_unlock_sound_file},
        {"session_progress", test_session_progress}, {"session_hardcore", test_session_hardcore},
        {"session_encore", test_session_encore}, {"session_sign_out", test_session_sign_out},
        {"live_refusal", test_live_refusal},
    };
    for (const auto& [name, test] : tests)
        if (only.empty() || only == name) test();
    if (g_failures == 0) std::cout << "Achievements tests passed\n";
    return g_failures == 0 ? 0 : 1;
}