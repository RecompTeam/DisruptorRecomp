#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// What the launcher asks of the operating system: processes, the environment, the shell and one instance.
// platform_windows.cpp and platform_posix.cpp hold the two answers. Text is UTF-8 throughout.
namespace disruptor::launcher::platform {

using Variables = std::vector<std::pair<std::string, std::string>>;

// This process's environment, in its own order.
Variables environment();
// `base` with each of `given` set and each of `dropped` taken out. Names compare as the system compares them.
Variables with(Variables base, const Variables& given, const std::vector<std::string>& dropped = {});

struct Launch {
    std::filesystem::path executable;
    std::vector<std::string> arguments; // after the program's own name
    std::filesystem::path folder;       // the working folder
    Variables environment;
    std::filesystem::path output;       // standard output and error, the file made anew. Empty: discarded.
    std::string heading;                // written to `output` before the program starts
    bool ends_with_launcher = false;    // killed should the launcher end first
};

// A program the launcher started. Its standard input is the null device. It keeps running when this goes,
// unless it was started to end with the launcher.
class Process {
public:
    // Throws std::system_error when the program cannot be started.
    explicit Process(const Launch& launch);
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // The exit code once the program has ended. An exit code that reads as "still active" on Windows counts too.
    std::optional<int> ended();
    std::optional<int> wait(std::chrono::milliseconds most);
    // Ends the program and the programs it started, and waits up to ten seconds for it. False when it is still there.
    bool stop();

private:
    struct State;
    std::unique_ptr<State> state_;
};

std::filesystem::path executable_path();
// The game's executable in the program folder: DisruptorRecompiled.exe on Windows, DisruptorRecompiled elsewhere.
std::filesystem::path game_executable(const std::filesystem::path& program);

void open_folder(const std::filesystem::path& folder);
// The file's folder, with the file chosen where the file manager can.
void show_file(const std::filesystem::path& file);

// Text to standard output or error. A Windows GUI program's C streams reach no redirected handle.
void write_standard(std::string_view text, bool error);
void unset(const std::string& name);

// Held for the launcher's life: false when another launcher holds the folder of the same player's files.
class Instance {
public:
    explicit Instance(const std::filesystem::path& data);
    ~Instance();
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    bool first() const { return first_; }

private:
    bool first_ = false;
    void* held_ = nullptr;
    int descriptor_ = -1;
};

// The system's name and version, then one line per display and graphics adapter, for a problem report.
std::vector<std::string> system_lines();

} // namespace disruptor::launcher::platform
