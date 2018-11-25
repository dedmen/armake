/*
 * Copyright (C)  2016  Felix "KoffeinFlummi" Wiegand
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


//#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
//#include <unistd.h>
#endif

#include "filesystem.h"
#include "utils.h"
#include <string>
#include <filesystem>



bool create_folder(const std::filesystem::path& path) {
    /*
     * Create the given folder recursively. Returns -2 if the directory already exists,
     * false on error and true on success.
     */
    return std::filesystem::create_directories(path);
}

std::optional<std::filesystem::path> create_temp_folder(std::string_view addonName) {
    /*
     * Create a temp folder for the given addon in the proper place
     * depending on the operating system. Returns created directory on success and nothing
     * on failure.
     */

    auto tempDir = std::filesystem::temp_directory_path() / "armake";

    std::string addonNameSanitized(addonName);
    std::replace(addonNameSanitized.begin(), addonNameSanitized.end(), '/', '_');
    std::replace(addonNameSanitized.begin(), addonNameSanitized.end(), '\\', '_');

    // find a free one
    for (int i = 0; i < 1024; i++) {
        auto path = tempDir / (addonNameSanitized + std::to_string(i));
        if (!std::filesystem::exists(path) && create_folder(path)) {
            return path;
        }
    }

    return {};
}


int remove_file(const std::filesystem::path &path) {
    /*
     * Remove a file. Returns true on success and 0 on failure.
     */


    return std::filesystem::remove(path);
}

__itt_string_handle* handle_remove_folder = __itt_string_handle_create("remove_folder");
bool remove_folder(const std::filesystem::path &folder) {
    /*
     * Recursively removes a folder tree. Returns false on
     * failure and true on success.
     */

    __itt_task_begin(fsDomain, __itt_null, __itt_null, handle_remove_folder);
    std::filesystem::remove_all(folder); //#TODO catch filesystem exception. Can happen if folder is in use or open
    __itt_task_end(fsDomain);

    return true;
}


bool copy_file(const std::filesystem::path &source, const std::filesystem::path &target) {
    /*
     * Copy the file from the source to the target. Overwrites if the target
     * already exists.
     * Returns a true on success and false on failure.
     */

    if (!create_folder(target.parent_path()))
        return false;

    return std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
}

