#ifndef PSX_USER_DIR_H
#define PSX_USER_DIR_H

/* Where a writable path goes when the player's files are kept apart from the program. */

#include <filesystem>
#include <system_error>

/* `path` with its part inside `program` placed in `user`, any other path as it is. Whether it lies inside is asked of
 * the file system, so another spelling of the program folder, in another case on Windows, counts too. */
inline std::filesystem::path psx_moved_to_user_dir(const std::filesystem::path &path,
                                                   const std::filesystem::path &program,
                                                   const std::filesystem::path &user) {
    namespace fs = std::filesystem;
    const fs::path normal = path.lexically_normal();
    std::error_code failed;
    for (fs::path folder = normal; !folder.empty(); folder = folder.parent_path()) {
        if (fs::equivalent(folder, program, failed)) {
            const fs::path inside = normal.lexically_relative(folder);
            return inside.empty() || inside == "." ? user : user / inside;
        }
        if (folder == folder.parent_path()) break;
    }
    return path;
}

#endif
