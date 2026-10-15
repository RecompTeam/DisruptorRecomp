#pragma once

#include "disc_import.h"
#include "launcher_settings.h"
#include "region_disc.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::launcher {

// take: an image whose kind the worker finds out. look: only the descriptions of the listed discs.
enum class Task { import, verify, language, take, look };
using Regions = std::map<std::string, std::optional<RegionDisc>>;

struct Request {
    Task task = Task::verify;
    std::filesystem::path source;
    bool launch = false;
    std::filesystem::path home;      // the verified game disc's data, empty when there is none
    std::string spoken;              // the language disc in use, UTF-8, empty for English
    std::vector<std::string> listed; // the language discs to describe when the task is done
};

struct Outcome {
    Task task = Task::verify; // what the request turned out to be
    bool launch = false;
    std::filesystem::path image;
    VerifiedDisc disc;
    std::optional<RegionDisc> language;
    Regions regions;
    std::wstring error, language_error;
};

// What a task does with discs, so that a test can stand in for them.
struct Discs {
    std::function<VerifiedDisc(const std::filesystem::path&)> import, verify;
    std::function<void(const VerifiedDisc&)> check_package;
    std::function<std::optional<RegionDisc>(const std::filesystem::path&)> look;
    std::function<RegionDisc(const std::filesystem::path& home, const std::filesystem::path& image)> check;
    std::function<void(const std::wstring&, unsigned)> report;
};

// Runs on the worker: no image is opened anywhere else.
Outcome run(Request request, const Discs& discs, const std::atomic_bool& cancelled);

enum class DiscAfter { kept, cleared, taken };

// What the window does with an outcome.
struct Step {
    DiscAfter disc = DiscAfter::kept;
    bool import_next = false;   // the image was no language disc: it goes on as the game disc
    bool save_language = false; // name the image as the language disc, the status is for a save that worked
    bool launch = false;
    std::wstring status;        // empty leaves the status box as it is
};

Step after(const Outcome& outcome, bool cancelled);
// The status while the game runs. unwritten: settings.toml lacks rows and could not be completed.
std::wstring running_text(const std::wstring& overriding, bool unwritten);

// The language box: English first, then the listed discs in their order.
struct LanguageList {
    std::vector<std::wstring> labels;
    int selected = 0;
    std::wstring note;
};

// A disc the worker has not described yet is absent from the regions, one it could not name has no value there.
LanguageList language_list(const std::vector<std::string>& discs, const Regions& regions, const std::string& spoken);

// What the window calls a build from `git describe`: a release's tag alone, the tag it follows and "(dev)" for any
// other build, "dev build" when there is no tag.
std::string shown_version(std::string_view described);

// UTF-8 `text` whole when `width` says it fits in `room`, else cut at a code point and ended with an ellipsis, or empty
// when not even the ellipsis fits.
std::string fitted(std::string text, float room, const std::function<float(const std::string&)>& width);

// The window draws its own title bar: a press there moves the window, one near a side resizes it.
enum class Hit { client, drag, top_left, top, top_right, right, bottom_right, bottom, bottom_left, left };
struct TitleBar {
    float height = 0.0f;
    float buttons_left = 0.0f; // the bar's buttons take presses from here to the right
    float edge = 0.0f;         // how near a side a press resizes; twice that from a corner takes the corner
};
Hit hit_at(float x, float y, float width, float height, const TitleBar& bar, bool maximized);

// Windows command-line quoting, including trailing backslashes and quotes.
std::wstring quote(const std::wstring& argument);
// Whole paths: the disc lives with the player's files, which need not be beside the program.
std::wstring game_command(const std::filesystem::path& program, const VerifiedDisc& disc);

constexpr int slider_steps = 200;
std::wstring value_text(const Option& option, int value);
// Sensitivity spans 0.005 to 2.000, so its slider is logarithmic like the in-game one.
int to_position(const Option& option, int value, int highest);
int from_position(const Option& option, int position, int highest);

} // namespace disruptor::launcher
