#include "update_flow.h"

#include "game/settings/user_folder.h"
#include "launcher_settings.h"
#include "player_files.h"
#include "report.h"

#include <exception>

namespace fs = std::filesystem;

namespace disruptor::launcher {

bool apply_update(Updater& updater, const fs::path& program, const fs::path& data, Activity& activity, bool silent,
                  bool restart) {
    try {
        if (user_folder::copy_of(program) == user_folder::Copy::plain) {
            activity.add("The update was not started: the program is not in the folder the updater installed");
            return false;
        }
        const fs::path copy = back_up_saves(data, installed_version(program), report_stamp());
        if (!copy.empty()) activity.add("Your saves are copied to " + utf8(copy));
        return updater.apply(silent, restart);
    } catch (const std::exception& error) {
        activity.add(std::string("The update was not started: ") + error.what());
    }
    return false;
}

} // namespace disruptor::launcher
