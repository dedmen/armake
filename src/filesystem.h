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

#pragma once

#include <string_view>
#include <optional>
#include <functional>
#include "ittnotify.h"
#include <filesystem>

#ifdef _WIN32
#define PATHSEP '\\'
#define PATHSEP_STR "\\"
#define TEMPPATH "C:\\Windows\\Temp\\armake\\"
#else
#define PATHSEP '/'
#define PATHSEP_STR "/"
#define TEMPPATH "/tmp/armake/"
#endif

namespace std::filesystem {
        class path;
}


bool create_folder(const std::filesystem::path &path);

std::optional<std::filesystem::path> create_temp_folder(std::string_view addonName);

int remove_file(const std::filesystem::path &path);

bool remove_folder(const std::filesystem::path &folder);

bool copy_file(const std::filesystem::path &source, const std::filesystem::path &target);


inline __itt_domain* fsDomain = __itt_domain_create("armake.filesystem");
inline __itt_string_handle* handle_traverse_directory = __itt_string_handle_create("traverse_directory");
template<typename callbk,typename ... T>
int traverse_directory(const std::filesystem::path &root, callbk callback, T... args) {
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

        callback(root, i->path(), args...);
    }
    __itt_task_end(fsDomain);
    return 0;
}
