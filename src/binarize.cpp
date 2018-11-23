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


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
//#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#else
#include <errno.h>
#include <fts.h>
#endif

#include "args.h"
#include "filesystem.h"
#include "utils.h"
#include "rapify.h"
#include "p3d.h"
#include "binarize.h"
#include "utils.h"
#include <filesystem>


bool warned_bi_not_found = false;


#ifdef _WIN32

char *find_root(char *source) {
    char *root = (char *)malloc(2048);
    char *candidate = (char *)malloc(2048);

    GetFullPathName(source, 2048, root, NULL);

    while (true) {
        if (strrchr(root, '\\') == NULL) {
            strcpy(candidate, "config.cpp");
            if (std::filesystem::exists(candidate)) {
                free(candidate);
                free(root);
                return ".\\";
            }
            return NULL;
        }

        *(strrchr(root, '\\') + 1) = 0;

        strcpy(candidate, root);
        strcat(candidate, "config.cpp");
        if (std::filesystem::exists(candidate)) {
            free(candidate);
            return root;
        }

        *(strrchr(root, '\\')) = 0;
    }
}


int attempt_bis_binarize(char *source, char *target, Logger& logger) {
    /*
     * Attempts to find and use the BI binarize.exe for binarization. If the
     * exe is not found, a negative integer is returned. 0 is returned on
     * success and a positive integer on failure.
     */

    extern std::string current_target;
    SECURITY_ATTRIBUTES secattr = { sizeof(secattr) };
    STARTUPINFO info = { sizeof(info) };
    PROCESS_INFORMATION processInfo;
    long unsigned buffsize;
    long success;
    int32_t num_lods;
    int i;
    int j;
    int k;
    bool is_rtm;
    char command[2048];
    char temp[2048];
    char filename[2048];
    char *dependencies[MAXTEXTURES];
    char *root;
    FILE *f_source;
    std::vector<mlod_lod> mlod_lods;

    current_target = source;

    if (getenv("NATIVEBIN"))
        return -1;

    is_rtm = !strcmp(source + strlen(source) - 4, ".rtm");

    for (i = 0; i < strlen(source); i++)
        source[i] = (source[i] == '/') ? '\\' : source[i];

    for (i = 0; i < strlen(target); i++)
        target[i] = (target[i] == '/') ? '\\' : target[i];

    // Find binarize.exe
    buffsize = sizeof(command);
    success = RegGetValue(HKEY_CURRENT_USER, "Software\\Bohemia Interactive\\binarize", "path",
            RRF_RT_ANY, NULL, command, &buffsize);

    if (success != ERROR_SUCCESS)
        return -2;

    strcat(command, "\\binarize.exe");

    if (!std::filesystem::is_regular_file(command))
        return -3;

    // Read P3D and create a list of required files
    if (!is_rtm) {
        //f_source = fopen(source, "rb");
        //if (!f_source) {
        //    printf("Failed to open %s.\n", source);
        //    return 1;
        //}


        //P3DFile f(logger);
        //f.readMLOD(f_source); //#TODO compact variant that just reads lods without modelinfo

        //fseek(f_source, 8, SEEK_SET);
        //fread(&num_lods, 4, 1, f_source);
        //mlod_lods.resize(num_lods);
        //num_lods = read_lods(f_source, mlod_lods, num_lods);
        //fflush(stdout);
        //if (num_lods < 0) {
        //    printf("Source file seems to be invalid P3D.\n");
        //    return 2;
        //}
        //
        //fclose(f_source);

        memset(dependencies, 0, sizeof(dependencies));
        for (i = 0; i < num_lods; i++) {
            for (j = 0; j < mlod_lods[i].num_faces; j++) {
                if (!mlod_lods[i].faces[j].texture_name.empty() && mlod_lods[i].faces[j].texture_name[0] != '#') {
                    for (k = 0; k < MAXTEXTURES; k++) {
                        if (dependencies[k] == 0)
                            break;
                        if (stricmp(mlod_lods[i].faces[j].texture_name.c_str(), dependencies[k]) == 0)
                            break;
                    }
                    if (k < MAXTEXTURES && dependencies[k] == 0) {
                        dependencies[k] = (char *)safe_malloc(2048);
                        strcpy(dependencies[k], mlod_lods[i].faces[j].texture_name.c_str());
                    }
                }
                if (!mlod_lods[i].faces[j].material_name.empty() && mlod_lods[i].faces[j].material_name[0] != '#') {
                    for (k = 0; k < MAXTEXTURES; k++) {
                        if (dependencies[k] == 0)
                            break;
                        if (stricmp(mlod_lods[i].faces[j].material_name.c_str(), dependencies[k]) == 0)
                            break;
                    }
                    if (k < MAXTEXTURES && dependencies[k] == 0) {
                        dependencies[k] = (char *)safe_malloc(2048);
                        strcpy(dependencies[k], mlod_lods[i].faces[j].material_name.c_str());
                    }
                }
            }
        }
    }

    // Create a temporary folder to isolate the target file and copy it there
    if (strchr(source, '\\') != NULL)
        strcpy(filename, strrchr(source, '\\') + 1);
    else
        strcpy(filename, source);

    auto tempfolder = create_temp_folder(filename);

    if (!tempfolder) {
        logger.error("Failed to create temp folder.\n");
        return 1;
    }

    std::filesystem::path target_tempfolder;

    // Create a temp target folder for binarize calls
    if (strcmp(args.positionals[0], "binarize") == 0) {
        strcpy(temp, filename);
        strcat(temp, ".out");

        auto newFolder = create_temp_folder(temp);

        if (!newFolder) {
            logger.error("Failed to create temp folder.\n");
            return 1;
        }
        target_tempfolder = *newFolder;
    }

    strcpy(temp, (strchr(source, PATHSEP) == NULL) ? source : strrchr(source, PATHSEP) + 1);
    strcpy(filename, tempfolder->string().c_str());
    strcat(filename, temp);

    GetFullPathName(source, 2048, temp, NULL);

    if (!::copy_file(std::filesystem::path(temp), *tempfolder / temp)) {
        logger.error("Failed to copy %s to temp folder.\n", temp);
        return 2;
    }

    // Try to find the required files and copy them there too
    root = find_root(source);

    strcpy(temp, root);
    strcat(temp, "config.cpp");
    strcpy(filename, tempfolder->string().c_str());
    strcat(filename, "config.cpp");
    copy_file(temp, filename);

    strcpy(temp, root);
    strcat(temp, "model.cfg");
    strcpy(filename, tempfolder->string().c_str());
    strcat(filename, "model.cfg");
    copy_file(temp, filename);

    free(root);

    if (!is_rtm) {
        for (i = 0; i < MAXTEXTURES; i++) {
            if (dependencies[i] == 0)
                break;

            *filename = 0;
            if (dependencies[i][0] != '\\')
                strcpy(filename, "\\");
            strcat(filename, dependencies[i]);

            auto fileFound = find_file(filename, "");
            if (!fileFound) {
                logger.warning(std::string_view(source), 0u, "Failed to find file %s.\n", filename);
                free(dependencies[i]);
                continue;
            }

            strcpy(filename, tempfolder->string().c_str());
            strcat(filename, dependencies[i]);

            if (!copy_file(fileFound->string().c_str(), filename)) {
                logger.error("Failed to copy %s to temp folder.\n", temp);
                return 3;
            }

            free(dependencies[i]);
        }
    }

    // Call binarize.exe
    strcpy(temp, command);
    sprintf(command, "\"%s\"", temp);

    strcat(command, " -norecurse -always -silent -maxProcesses=0 ");
    strcat(command, tempfolder->string().c_str());
    strcat(command, " ");

    if (strcmp(args.positionals[0], "binarize") == 0) {
        strcpy(temp, target_tempfolder.string().c_str());
        *(strrchr(temp, PATHSEP)) = 0;
    } else {
        GetFullPathName(target, 2048, temp, NULL);
        *(strrchr(temp, PATHSEP)) = 0;
    }

    strcat(command, temp);

    if (getenv("BIOUTPUT"))
        logger.debug("cmdline: %s\n", command);

    if (!getenv("BIOUTPUT")) {
        secattr.lpSecurityDescriptor = NULL;
        secattr.bInheritHandle = TRUE;
        info.hStdOutput = info.hStdError = CreateFile("NUL", GENERIC_WRITE, 0, &secattr, OPEN_EXISTING, 0, NULL);
        info.dwFlags |= STARTF_USESTDHANDLES;
    }

    if (CreateProcess(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo)) {
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
    } else {
        logger.error("Failed to binarize %s.\n", source);
        return 3;
    }

    if (getenv("BIOUTPUT"))
        logger.debug("done with binarize.exe\n");

    // Copy final file to target
    if (strcmp(args.positionals[0], "binarize") == 0) {
        strcpy(temp, (strchr(source, PATHSEP) == NULL) ? source : strrchr(source, PATHSEP) + 1);
        strcpy(filename, target_tempfolder.string().c_str());
        strcat(filename, temp);
        if (!copy_file(filename, target))
            return 4;

        if (!remove_folder(target_tempfolder)) {
            logger.error("Failed to remove temp folder.\n");
            return 5;
        }
    }

    // Clean Up
    if (!remove_folder(*tempfolder)) {
        logger.error("Failed to remove temp folder.\n");
        return 5;
    }

    return success;
}
#endif


int binarize(std::filesystem::path source, std::filesystem::path target, Logger& logger) {
    /*
     * Binarize the given file. If source and target are identical, the target
     * is overwritten. If the source is a P3D, it is converted to ODOL. If the
     * source is a rapifiable type (cpp, ext, etc.), it is rapified.
     *
     * If the file type is not recognized, -1 is returned. 0 is returned on
     * success and a positive integer on error.
     */



    auto fileExtension = source.extension();

    if (fileExtension == ".cpp" ||
        fileExtension == ".rvmat" ||
        fileExtension == ".ext") //#TODO not ext! only description.ext
        return Rapifier::rapify_file(source.string().c_str(), target.string().c_str(), logger);

    if (fileExtension == ".p3d" ||
        fileExtension == ".rtm") {
#ifdef _WIN32
        int success;
        extern bool warned_bi_not_found;
        //success = attempt_bis_binarize(source, target, logger);
        //if (success >= 0)
        //    return success;
        //if (!warned_bi_not_found) {
        //    lwarningf(source, -1, "Failed to find BI tools, using internal binarizer.\n");
        //    warned_bi_not_found = true;
        //}
#endif
        if (fileExtension == ".p3d")
            return mlod2odol(source.string().c_str(), target.string().c_str(), logger);
    }

    return -1;
}


int cmd_binarize(Logger& logger) {
    int success;

    if (args.num_positionals == 1) {
        return 128;//only binarize argument exists, missing path arguments
    }

    if (args.num_positionals == 2) {
        success = binarize(args.positionals[1], "-", logger);
    } else {
        // check if target already exists
        if (std::filesystem::exists(args.positionals[2]) && !args.force) {
            logger.error("File %s already exists and --force was not set.\n", args.positionals[2]);
            return 1;
        }

        success = binarize(args.positionals[1], args.positionals[2], logger);
    }

    if (success == -1) {
        logger.error("File is no P3D and doesn't seem rapifiable.\n");
        return 1;
    }

    return success;
}
