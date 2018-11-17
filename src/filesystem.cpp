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
    std::error_code ec;
    bool res = std::filesystem::create_directories(path, ec);
    return res;
}

bool create_temp_folder(char *addon, char *temp_folder, size_t bufsize) {
    /*
     * Create a temp folder for the given addon in the proper place
     * depending on the operating system. Returns false on failure and true
     * on success.
     */

    char temp[2048] = TEMPPATH;
    char addon_sanitized[2048];
    int i;

#ifdef _WIN32
    temp[0] = 0;
    GetTempPath(sizeof(temp), temp);
    strcat(temp, "armake\\");
#endif

    temp[strlen(temp) - 1] = 0;

    for (i = 0; i <= strlen(addon); i++)
        addon_sanitized[i] = (addon[i] == '\\' || addon[i] == '/') ? '_' : addon[i];

    // find a free one
    for (i = 0; i < 1024; i++) {
        snprintf(temp_folder, bufsize, "%s_%s_%i", temp, addon_sanitized, i);
        if (!std::filesystem::exists(temp_folder))
            break;
    }

    if (i == 1024)
        return false;

    return create_folder(temp_folder);
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


#ifndef _WIN32
int alphasort_ci(const struct dirent **a, const struct dirent **b) {
    /*
     * A case insensitive version of alphasort.
     */

    int i;
    char a_name[512];
    char b_name[512];

    strncpy(a_name, (*a)->d_name, sizeof(a_name));
    strncpy(b_name, (*b)->d_name, sizeof(b_name));

    for (i = 0; i < strlen(a_name); i++) {
        if (a_name[i] >= 'A' && a_name[i] <= 'Z')
            a_name[i] = a_name[i] - ('A' - 'a');
    }

    for (i = 0; i < strlen(b_name); i++) {
        if (b_name[i] >= 'A' && b_name[i] <= 'Z')
            b_name[i] = b_name[i] - ('A' - 'a');
    }

    return strcoll(a_name, b_name);
}
#endif


int traverse_directory_recursive(char *root, char *cwd, int (*callback)(char *, char *, char *), char *third_arg) {
    /*
     * Recursive helper function for directory traversal.
     */

#ifdef _WIN32

    WIN32_FIND_DATA file;
    HANDLE handle = NULL;
    char mask[2048];
    int success;

    if (cwd[strlen(cwd) - 1] == '\\')
        cwd[strlen(cwd) - 1] = 0;

    GetFullPathName(cwd, 2048, mask, NULL);
    sprintf(mask, "%s\\*", mask);

    handle = FindFirstFile(mask, &file);
    if (handle == INVALID_HANDLE_VALUE)
        return 1;

    do {
        if (strcmp(file.cFileName, ".") == 0 || strcmp(file.cFileName, "..") == 0)
            continue;

        sprintf(mask, "%s\\%s", cwd, file.cFileName);
        if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            traverse_directory_recursive(root, mask, callback, third_arg);
        } else {
            success = callback(root, mask, third_arg);
            if (success)
                return success;
        }
    } while (FindNextFile(handle, &file));

    FindClose(handle);

    return 0;

#else

    struct dirent **namelist;
    struct stat st;
    char next[2048];
    int i;
    int n;
    int success;

    n = scandir(cwd, &namelist, NULL, alphasort_ci);
    if (n < 0)
        return 1;

    success = 0;
    for (i = 0; i < n; i++) {
        if (strcmp(namelist[i]->d_name, "..") == 0 ||
                strcmp(namelist[i]->d_name, ".") == 0)
            continue;

        strcpy(next, cwd);
        strcat(next, "/");
        strcat(next, namelist[i]->d_name);

        stat(next, &st);

        if (S_ISDIR(st.st_mode))
            success = traverse_directory_recursive(root, next, callback, third_arg);
        else
            success = callback(root, next, third_arg);

        if (success)
            goto cleanup;
    }

cleanup:
    for (i = 0; i < n; i++)
        free(namelist[i]);
    free(namelist);

    return success;

#endif
}

__itt_string_handle* handle_traverse_directory = __itt_string_handle_create("traverse_directory");
int traverse_directory(char *root, int (*callback)(char *, char *, char *), char *third_arg) {
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
    auto res = traverse_directory_recursive(root, root, callback, third_arg);
    __itt_task_end(fsDomain);
    return res;
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
