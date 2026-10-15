#include "launcher_flow.h"

#include "launcher_settings.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <exception>
#include <stdexcept>
#include <utility>

namespace disruptor::launcher {

Outcome run(Request request, const Discs& discs, const std::atomic_bool& cancelled) {
    Outcome outcome;
    outcome.task = request.task;
    outcome.launch = request.launch;
    outcome.image = request.source;
    try {
        if (request.task == Task::take && discs.look(request.source)) {
            if (request.home.empty())
                throw std::runtime_error("That is a disc of another region.\n"
                                         "Install the USA disc first: it is the game, and the other disc gives it a language.");
            outcome.task = Task::language;
        }
        if (outcome.task == Task::language) {
            outcome.language = discs.check(request.home, request.source);
            request.listed.push_back(utf8(request.source));
        } else if (outcome.task == Task::import || outcome.task == Task::verify) {
            outcome.disc = outcome.task == Task::import ? discs.import(request.source) : discs.verify(request.source);
            discs.check_package(outcome.disc);
            if (cancelled) {
                outcome.disc = {};
                outcome.error = L"Cancelled. A finished import is kept.\nReopen the launcher to check it again.";
            } else if (request.launch && !request.spoken.empty()) {
                if (discs.report) discs.report(L"Checking the language disc...", 100);
                try {
                    discs.check(outcome.disc.data, from_utf8(request.spoken));
                } catch (const std::exception& error) {
                    outcome.language_error = L"The language disc cannot be used. " + error_text(error);
                }
            }
        }
    } catch (const std::exception& error) {
        outcome.disc = {};
        outcome.language.reset();
        outcome.error = error_text(error);
    }
    for (const std::string& path : request.listed) {
        std::optional<RegionDisc> seen;
        try {
            seen = discs.look(from_utf8(path));
        } catch (const std::exception&) {
            seen.reset(); // a path that cannot be read is a disc that cannot be named
        }
        outcome.regions.emplace(path, std::move(seen));
    }
    return outcome;
}

Step after(const Outcome& outcome, bool cancelled) {
    Step step;
    const bool failed = !outcome.error.empty();
    if (outcome.task == Task::look || (outcome.task == Task::take && !failed)) {
        step.import_next = outcome.task == Task::take && !cancelled;
        if (cancelled) step.status = L"Cancelled.";
    } else if (outcome.task == Task::language || outcome.task == Task::take) {
        if (failed) {
            step.status = outcome.error;
        } else if (cancelled || !outcome.language) {
            step.status = L"Cancelled. The language is as it was.";
        } else {
            step.save_language = true;
            step.status = describe(*outcome.language) + L"\nThe image is used where it is, so keep it there.";
        }
    } else if (failed) {
        // A cancellation can leave an already committed installation.
        // Browsing again or reopening the launcher checks it afresh.
        step.disc = DiscAfter::cleared;
        step.status = outcome.error;
    } else {
        step.disc = DiscAfter::taken;
        if (!outcome.language_error.empty()) {
            step.status = outcome.language_error + L"\nChoose English or another disc, then press Play.";
        } else {
            step.status = L"Game data is installed and ready. Press Play to start.";
            step.launch = outcome.launch && !cancelled;
        }
    }
    return step;
}

std::wstring running_text(const std::wstring& overriding, bool unwritten) {
    std::wstring text = L"Disruptor is running. You can close this launcher and keep playing.";
    if (unwritten)
        text += L"\nsettings.toml cannot be written.\n"
                L"Where it names no value, the game uses its own, which may differ from what is shown here.";
    if (!overriding.empty())
        text += L"\nPSX_ environment variables are set, and the game lets some of them replace settings for this "
                L"run: " + overriding;
    return text;
}

LanguageList language_list(const std::vector<std::string>& discs, const Regions& regions, const std::string& spoken) {
    LanguageList list{{L"English (the US disc alone)"}, 0, L"Add a French, German or Japanese disc image to play in that language."};
    for (size_t index = 0; index < discs.size(); ++index) {
        const auto looked = regions.find(discs[index]);
        const RegionDisc* disc = looked != regions.end() && looked->second ? &*looked->second : nullptr;
        const std::wstring kind = disc ? std::wstring(disc->language.begin(), disc->language.end())
            : looked == regions.end() ? L"Disc image" : L"Unknown or missing";
        list.labels.push_back(kind + L" (" + from_utf8(discs[index]).filename().wstring() + L")");
        if (discs[index] != spoken) continue;
        list.selected = static_cast<int>(index) + 1;
        list.note = disc ? describe(*disc) + L" The image is used where it is."
            : looked == regions.end() ? L""
            : L"This image is missing or is not a known disc. Choose another one or English.";
    }
    return list;
}

std::string shown_version(std::string_view described) {
    std::string_view tag = described;
    bool dev = false;
    if (tag.ends_with("-dirty")) {
        tag.remove_suffix(6);
        dev = true;
    }
    const auto hash = tag.rfind("-g");
    const auto count = hash == std::string_view::npos ? std::string_view::npos : tag.rfind('-', hash - 1);
    if (count != std::string_view::npos && hash > count + 1 &&
        std::all_of(tag.begin() + static_cast<std::ptrdiff_t>(count) + 1, tag.begin() + static_cast<std::ptrdiff_t>(hash),
                    [](char letter) { return letter >= '0' && letter <= '9'; })) {
        tag = tag.substr(0, count);
        dev = true;
    }
    if (!tag.starts_with('v') || tag.size() < 2 || tag[1] < '0' || tag[1] > '9') return "dev build";
    return std::string(tag) + (dev ? " (dev)" : "");
}

std::string fitted(std::string text, float room, const std::function<float(const std::string&)>& width) {
    if (width(text) <= room) return text;
    const std::string ellipsis = "\xe2\x80\xa6";
    if (width(ellipsis) > room) return {};
    while (!text.empty() && width(text + ellipsis) > room) {
        while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) text.pop_back();
        if (!text.empty()) text.pop_back();
    }
    return text + ellipsis;
}

Hit hit_at(float x, float y, float width, float height, const TitleBar& bar, bool maximized) {
    if (x < 0.0f || y < 0.0f || x >= width || y >= height) return Hit::client;
    if (!maximized && bar.edge > 0.0f) {
        const float corner = bar.edge * 2.0f;
        const bool left = x < bar.edge, right = x >= width - bar.edge, top = y < bar.edge, bottom = y >= height - bar.edge;
        const bool near_left = x < corner, near_right = x >= width - corner, near_top = y < corner, near_bottom = y >= height - corner;
        if ((top && near_left) || (left && near_top)) return Hit::top_left;
        if ((top && near_right) || (right && near_top)) return Hit::top_right;
        if ((bottom && near_left) || (left && near_bottom)) return Hit::bottom_left;
        if ((bottom && near_right) || (right && near_bottom)) return Hit::bottom_right;
        if (top) return Hit::top;
        if (bottom) return Hit::bottom;
        if (left) return Hit::left;
        if (right) return Hit::right;
    }
    return y < bar.height && x < bar.buttons_left ? Hit::drag : Hit::client;
}

std::wstring quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

std::vector<std::string> game_arguments(const std::filesystem::path& program, const VerifiedDisc& disc) {
    return {"--no-launcher", "--game", utf8(program / disc.profile->game_config), "--disc", utf8(disc.cue)};
}

std::wstring value_text(const Option& option, int value) {
    wchar_t text[32] = L"";
    if (option.unit == Unit::percent) swprintf(text, 32, L"%d%%", value);
    else if (option.unit == Unit::frames) swprintf(text, 32, L"%d FPS", value);
    else if (option.unit == Unit::thousandths) {
        const double times = static_cast<double>(value) / option.initial;
        swprintf(text, 32, times < 1.0 ? L"%.2fx" : times < 10.0 ? L"%.1fx" : L"%.0fx", times);
    }
    else swprintf(text, 32, L"%d", value);
    return text;
}

int to_position(const Option& option, int value, int highest) {
    if (option.unit != Unit::thousandths) return value;
    const double span = std::log(static_cast<double>(highest) / option.lowest);
    return static_cast<int>(std::lround(std::log(static_cast<double>(value) / option.lowest) / span * slider_steps));
}

int from_position(const Option& option, int position, int highest) {
    if (option.unit != Unit::thousandths) return position;
    const double ratio = static_cast<double>(highest) / option.lowest;
    const double value = option.lowest * std::pow(ratio, static_cast<double>(position) / slider_steps);
    return std::clamp(static_cast<int>(std::lround(value)), option.lowest, highest);
}

} // namespace disruptor::launcher
