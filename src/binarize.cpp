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

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <wchar.h>
#else
#include <errno.h>
#include <fts.h>
#endif

#include "args.h"
#include "filesystem.h"
#include "rapify.h"
#include "p3d.h"
#include "binarize.h"
#include <filesystem>


bool warned_bi_not_found = false;


#ifdef _WIN32

char *find_root(const char *source) {
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


int attempt_bis_binarize(std::filesystem::path source, std::filesystem::path target, Logger& logger) {
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
    std::vector <std::string> dependencies;
    char *root;
    FILE *f_source;
    std::vector<mlod_lod> mlod_lods;

    current_target = source.string();

    if (getenv("NATIVEBIN"))
        return -1;

    is_rtm = source.extension().string() ==  ".rtm";

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
        P3DFile f(logger);
        dependencies = f.retrieveDependencies(source);
    }

    // Create a temporary folder to isolate the target file and copy it there
    if (strchr(source.string().c_str(), '\\') != NULL)
        strcpy(filename, strrchr(source.string().c_str(), '\\') + 1);
    else
        strcpy(filename, source.string().c_str());

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

    strcpy(temp, (strchr(source.string().c_str(), PATHSEP) == NULL) ? source.string().c_str() : strrchr(source.string().c_str(), PATHSEP) + 1);
    strcpy(filename, tempfolder->string().c_str());
    strcat(filename, temp);

    GetFullPathName(source.string().c_str(), 2048, temp, NULL);

    if (!::copy_file(std::filesystem::path(temp), *tempfolder / temp)) {
        logger.error("Failed to copy %s to temp folder.\n", temp);
        return 2;
    }

    // Try to find the required files and copy them there too
    root = find_root(source.string().c_str());

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

        for (auto& it : dependencies) {
            *filename = 0;
            if (it.front() != '\\')
                it.insert(it.begin(), '\\');

            auto fileFound = find_file(it, "");
            if (!fileFound) {
                logger.warning(source.string(), 0u, "Failed to find file %s.\n", filename);
                continue;
            }

            if (!::copy_file(*fileFound, *tempfolder / it)) {
                logger.error("Failed to copy %s to temp folder.\n", temp);
                return 3;
            }
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
        GetFullPathName(target.string().c_str(), 2048, temp, NULL);
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
        strcpy(temp, (strchr(source.string().c_str(), PATHSEP) == NULL) ? source.string().c_str() : strrchr(source.string().c_str(), PATHSEP) + 1);
        strcpy(filename, target_tempfolder.string().c_str());
        strcat(filename, temp);
        if (!::copy_file(filename, target))
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
        fileExtension == ".rvmat")
        return Rapifier::rapify_file(source.string().c_str(), target.string().c_str(), logger);

    //#TODO rvmat's with >7 stages that are not using texGens are errors!
    //Should warn user about that.
    //also warn when using texGen's but not in every stage

    if (fileExtension == ".p3d" ||
        fileExtension == ".rtm") {
#ifdef _WIN32
        int success;
        extern bool warned_bi_not_found;
        success = attempt_bis_binarize(source, target, logger);
        if (success >= 0)
            return success;
        if (!warned_bi_not_found) {
            //lwarningf(source, -1, "Failed to find BI tools, using internal binarizer.\n");
            warned_bi_not_found = true;
        }
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

#include <fstream>

int cmd_preprocess(Logger& logger) {
    if (args.num_positionals != 3) {
        return 128;//missing path arguments
    }


    // check if target already exists
    if (std::filesystem::exists(args.positionals[2]) && !args.force) {
        logger.error("File %s already exists and --force was not set.\n", args.positionals[2]);
        return 1;
    }

    std::ifstream sourceFile(args.positionals[1], std::ifstream::in | std::ifstream::binary);
    std::ofstream targetFile(args.positionals[2], std::ofstream::out | std::ofstream::binary);

    Preprocessor preproc(logger);
    Preprocessor::ConstantMapType constants;
    int success = preproc.preprocess(args.positionals[1], sourceFile, targetFile, constants);

    if (success) {
        logger.error("Failed to preprocess %s.\n", args.positionals[1]);
        return success;
    }


    return success;
}
