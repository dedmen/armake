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

int traverse_directory(const std::filesystem::path &root, int (*callback)(const std::filesystem::path &rootDir, const std::filesystem::path &file, const char *thirdArg),
    const char *third_arg);

bool copy_directory(const std::filesystem::path &source, const std::filesystem::path &target);
