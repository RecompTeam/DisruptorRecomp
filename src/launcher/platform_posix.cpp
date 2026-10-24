#include "platform.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>
#include <thread>

extern char** environ;

namespace fs = std::filesystem;

namespace disruptor::launcher::platform {

namespace {

std::system_error errno_error(const std::string& what) { return {errno, std::generic_category(), what}; }

void reap(pid_t pid) {
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
}

// Moved above stdin, stdout and stderr, closed on exec: a number from 0 to 2 would be lost to the child's redirection.
int above_standard(int descriptor) {
    if (descriptor < 0 || descriptor > 2) return descriptor;
    const int moved = fcntl(descriptor, F_DUPFD_CLOEXEC, 3);
    const int failed = errno;
    close(descriptor);
    errno = failed;
    return moved;
}

// Runs a helper program without waiting for it and without letting it outlive its purpose as a zombie.
void spawn_detached(const std::vector<std::string>& command) {
    std::vector<char*> argv;
    for (const std::string& part : command) argv.push_back(const_cast<char*>(part.c_str()));
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child == 0) {
        if (fork() == 0) {
            const int null = open("/dev/null", O_RDWR);
            if (null >= 0) {
                dup2(null, 0);
                dup2(null, 1);
                dup2(null, 2);
            }
            execvp(argv[0], argv.data());
        }
        _exit(0);
    }
    if (child > 0) reap(child);
}

std::string os_release_name() {
    std::ifstream input("/etc/os-release");
    for (std::string line; std::getline(input, line);) {
        if (!line.starts_with("PRETTY_NAME=")) continue;
        std::string name = line.substr(12);
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
        return name;
    }
    return "?";
}

} // namespace

struct Process::State {
    pid_t pid = -1;
    std::optional<int> code;
    bool owned = false;  // ends with the launcher
};

Variables environment() {
    Variables variables;
    for (char** entry = environ; entry && *entry; ++entry) {
        const std::string_view text(*entry);
        const std::size_t equals = text.find('=');
        variables.emplace_back(std::string(text.substr(0, equals)), equals == std::string_view::npos ? std::string() : std::string(text.substr(equals + 1)));
    }
    return variables;
}

Variables with(Variables base, const Variables& given, const std::vector<std::string>& dropped) {
    std::erase_if(base, [&](const auto& pair) {
        return std::ranges::any_of(given, [&](const auto& set) { return pair.first == set.first; }) ||
               std::ranges::find(dropped, pair.first) != dropped.end();
    });
    base.insert(base.end(), given.begin(), given.end());
    return base;
}

Process::Process(const Launch& launch) : state_(std::make_unique<State>()) {
    state_->owned = launch.ends_with_launcher;
    const int output = above_standard(launch.output.empty() ? open("/dev/null", O_WRONLY | O_CLOEXEC)
                                                            : open(launch.output.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
    const int input = above_standard(open("/dev/null", O_RDONLY | O_CLOEXEC));
    std::array<int, 2> report{-1, -1};  // the child writes errno here when it cannot become the program
    const bool piped = pipe2(report.data(), O_CLOEXEC) == 0;
    if (piped) {
        report[0] = above_standard(report[0]);
        report[1] = above_standard(report[1]);
    }
    if (output < 0 || input < 0 || !piped || report[0] < 0 || report[1] < 0) {
        const std::system_error failed = errno_error("cannot open " + (launch.output.empty() ? std::string("/dev/null") : launch.output.string()));
        for (const int descriptor : {output, input, report[0], report[1]})
            if (descriptor >= 0) close(descriptor);
        throw failed;
    }
    if (!launch.heading.empty() && write(output, launch.heading.data(), launch.heading.size()) < 0) {}
    std::vector<std::string> strings{launch.executable.string()};
    strings.insert(strings.end(), launch.arguments.begin(), launch.arguments.end());
    std::vector<char*> argv;
    for (std::string& part : strings) argv.push_back(part.data());
    argv.push_back(nullptr);
    std::vector<std::string> pairs;
    for (const auto& [name, value] : launch.environment) pairs.push_back(name + "=" + value);
    std::vector<char*> envp;
    for (std::string& pair : pairs) envp.push_back(pair.data());
    envp.push_back(nullptr);
    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child == 0) {
        // A group of its own, so that stop() reaches the programs it starts as well.
        setpgid(0, 0);
#ifdef __linux__
        if (launch.ends_with_launcher) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent) _exit(127);
        }
#endif
        if (dup2(input, 0) >= 0 && dup2(output, 1) >= 0 && dup2(output, 2) >= 0 &&
            (launch.folder.empty() || chdir(launch.folder.c_str()) == 0))
            execve(argv[0], argv.data(), envp.data());
        const int failed = errno;
        if (write(report[1], &failed, sizeof(failed)) < 0) {}
        _exit(127);
    }
    const int started = child < 0 ? errno : 0;
    if (child > 0) setpgid(child, child);  // also here: stop() may come before the child has run
    close(output);
    close(input);
    close(report[1]);
    int failed = 0;
    ssize_t got = 0;
    if (child > 0)
        while ((got = read(report[0], &failed, sizeof(failed))) < 0 && errno == EINTR) {}
    close(report[0]);
    if (child < 0 || got > 0) {
        if (child > 0) reap(child);
        errno = child < 0 ? started : failed;
        throw errno_error("cannot start " + launch.executable.filename().string());
    }
    state_->pid = child;
}

// The program is reaped only here: until then its number cannot be handed to another group that kill() would reach.
Process::~Process() {
    if (!state_ || state_->pid <= 0) return;
    if (state_->owned) kill(-state_->pid, SIGKILL);
    if (state_->owned || state_->code) {
        reap(state_->pid);
        return;
    }
    // A program that keeps running is reaped by a thread of its own, so that it never stays a zombie.
    std::thread([pid = state_->pid] { reap(pid); }).detach();
}

std::optional<int> Process::ended() {
    if (state_->code) return state_->code;
    siginfo_t info{};
    if (waitid(P_PID, static_cast<id_t>(state_->pid), &info, WEXITED | WNOHANG | WNOWAIT) != 0 || info.si_pid != state_->pid)
        return std::nullopt;
    state_->code = info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status;
    return state_->code;
}

std::optional<int> Process::wait(std::chrono::milliseconds most) {
    const auto until = std::chrono::steady_clock::now() + most;
    while (true) {
        if (const auto code = ended()) return code;
        if (std::chrono::steady_clock::now() >= until) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool Process::stop() {
    // The whole group, even when its first program has ended: the ones it started may still run.
    kill(-state_->pid, SIGKILL);
    return wait(std::chrono::seconds(10)).has_value();
}

fs::path executable_path() {
    std::error_code failed;
    const fs::path path = fs::read_symlink("/proc/self/exe", failed);
    if (failed) throw std::runtime_error("Cannot find the launcher's folder");
    return path;
}

fs::path game_executable(const fs::path& program) { return program / "DisruptorRecompiled"; }

Moved move_new(const fs::path& from, const fs::path& to) {
    // renameat2 with RENAME_NOREPLACE, Linux 3.15 on: a file system without it moves nothing rather than risk a replace.
    constexpr unsigned no_replace = 1;
    if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), no_replace) == 0) return Moved::done;
    return errno == EEXIST ? Moved::taken : Moved::failed;
}

void open_folder(const fs::path& folder) { spawn_detached({"xdg-open", folder.string()}); }

void show_file(const fs::path& file) { spawn_detached({"xdg-open", file.parent_path().string()}); }

void write_standard(std::string_view text, bool error) {
    if (::write(error ? 2 : 1, text.data(), text.size()) < 0) {}
}

void unset(const std::string& name) { unsetenv(name.c_str()); }

bool animations_enabled() { return true; }

Instance::Instance(const fs::path& data) {
    const fs::path lock = data / ".launcher.lock";  // in the folder itself, whatever the caller's environment says
    descriptor_ = open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    first_ = descriptor_ >= 0 && flock(descriptor_, LOCK_EX | LOCK_NB) == 0;
}

Instance::~Instance() {
    if (descriptor_ >= 0) close(descriptor_);
}

std::vector<std::string> system_lines() {
    std::vector<std::string> lines;
    utsname host{};
    const std::string kernel = uname(&host) == 0 ? std::string(host.sysname) + " " + host.release + " " + host.machine : "?";
    lines.push_back("System: " + os_release_name() + ", " + kernel);
    const char* session = std::getenv("XDG_SESSION_TYPE");
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    lines.push_back(std::string("Session: ") + (session ? session : "?") + ", " + (desktop ? desktop : "?"));
    return lines;
}

} // namespace disruptor::launcher::platform
