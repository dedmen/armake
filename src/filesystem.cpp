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

__itt_domain* fsDomain = __itt_domain_create("armake.filesystem");

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
    std::filesystem::remove_all(folder);
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

__itt_string_handle* handle_traverse_directory = __itt_string_handle_create("traverse_directory");
int traverse_directory(const std::filesystem::path &root, int (*callback)(const std::filesystem::path &rootDir, const std::filesystem::path &file, char *thirdArg), char *third_arg) {
    /*
     * Traverse the given path and call the callback with the root folder as
     * the first, the current file path as the second, and the given third
     * arg as the third argument.
     *
     * The callback should return 0 success and any negative integer on
     * failure.
     *
     * This function returns 0 on success, a positive integer on a traversal
     * error and the last callback return value should the callback fail.
     */
    __itt_task_begin(fsDomain, __itt_null, __itt_null, handle_traverse_directory);

    for (auto i = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::follow_directory_symlink);
        i != std::filesystem::recursive_directory_iterator();
        ++i) {
        //if (i->is_directory() && (i->path().filename() == ignoreGit || i->path().filename() == ignoreSvn)) {
        //    i.disable_recursion_pending(); //Don't recurse into that directory
        //    continue;
        //}
        if (!i->is_regular_file()) continue;

        callback(root, i->path().string().c_str(), third_arg);
    }
    __itt_task_end(fsDomain);
    return 0;
}

__itt_string_handle* handle_copy_directory = __itt_string_handle_create("copy_directory");
bool copy_directory(const std::filesystem::path &source, const std::filesystem::path &target) {
    /*
     * Copy the entire directory given with source to the target folder.
     * Returns true on success and false on failure.
     */

    __itt_task_begin(fsDomain, __itt_null, __itt_null, handle_copy_directory);
    try {
        std::filesystem::copy(source, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    } catch (std::filesystem::filesystem_error& ex) {
        errorf("copy_directory failed. %s \nFrom: %s\nTo: %s", ex.what(), ex.path1().string().c_str(), ex.path2().string().c_str());
        __itt_task_end(fsDomain);
        return false;
    }

    __itt_task_end(fsDomain);
    return true;
}
