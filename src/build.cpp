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
#include <filesystem>
#include "unpack.h"
#include <iostream>
//#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#endif
extern "C" {
#include "sha1.h"
}

#include "args.h"
#include "binarize.h"
#include "filesystem.h"
#include "utils.h"
#include "sign.h"
#include "build.h"
#include "utils.h"

__itt_domain* buildDomain = __itt_domain_create("armake.build");



__itt_string_handle* handle_hash_file = __itt_string_handle_create("hash_file");
int hash_file(char *path, unsigned char *hash) {
    __itt_task_begin(buildDomain, __itt_null, __itt_null, handle_hash_file);
    SHA1Context sha;
    FILE *file;
    int filesize, i;
    unsigned temp;
    char buffer[4096];

    SHA1Reset(&sha);

    file = fopen(path, "rb");
    if (!file)
        return -1;

    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    for (i = 0; filesize - i >= sizeof(buffer); i += sizeof(buffer)) {
        fread(buffer, sizeof(buffer), 1, file);
        SHA1Input(&sha, (const unsigned char *)buffer, sizeof(buffer));
    }
    fread(buffer, filesize - i, 1, file);
    SHA1Input(&sha, (const unsigned char *)buffer, filesize - i);

    fclose(file);

    if (!SHA1Result(&sha))
        return -2;

    for (i = 0; i < 5; i++) {
        temp = sha.Message_Digest[i];
        sha.Message_Digest[i] = ((temp >> 24) & 0xff) |
            ((temp << 8) & 0xff0000) | ((temp >> 8) & 0xff00) | ((temp << 24) & 0xff000000);
    }

    memcpy(hash, sha.Message_Digest, 20);

    __itt_task_end(buildDomain);
    return 0;
}

__itt_string_handle* handle_writepbo = __itt_string_handle_create("writePbo");
__itt_string_handle* handle_prepWrite = __itt_string_handle_create("prepWrite");



bool Builder::file_allowed(std::string_view filename) {
    if (filename == "$PBOPREFIX$")
        return false;

    return std::none_of(excludeFiles.begin(), excludeFiles.end(), [&filename](std::string_view& ex) {
        return matches_glob(filename, ex.data());
    });
}

int Builder::binarize_callback(const std::filesystem::path &root, const std::filesystem::path &source) {

    std::string filename = source.string().substr(root.string().length() + 1);

    if (!file_allowed(filename))
        return 0;

    std::string target(source.string());

    if (target.length() > 10 &&
        source.extension().string() == ".cpp")
        target.replace(target.length() - 3, 3, "bin");


    const int success = binarize(source, target, logger);

    std::string_view p3do(".p3do");
    if (source.extension().string() == ".p3do")
        filename.pop_back();

    if (source.extension().string() == ".cpp")
        filename.replace(filename.length()-3,3,"bin");

    //#TODO store binarized config directly via membuf
    if (success == -1) {
        files_sizes.emplace_back(std::make_shared<PboFTW_CopyFromFile>(filename, source));
    } else {
        //if binarize failed, we just copy the source file
        files_sizes.emplace_back(std::make_shared<PboFTW_CopyFromFile>(filename, target));
    }
    

    if (success > 0)
        return success * -1;

    return 0;
}

int Builder::buildDirectory(std::filesystem::path inputDirectory, std::filesystem::path targetPbo) {
    
    // check if target/source exist
    try {
        //They will throw if something about the path is seriously wrong

        if (std::filesystem::exists(targetPbo) && !args.force) {
            logger.error("File %s already exists and --force was not set.\n", targetPbo.c_str());
            return 1;
        }
        if (!std::filesystem::exists(inputDirectory)) {
            logger.error("Source directory %s not found.\n", inputDirectory.c_str());
            return 1;
        }
    } catch (const std::filesystem::filesystem_error& err) {
        logger.error("Failed to check existence of file %s. Error code: %u\n", err.path1().c_str(), err.code());
        return 1;
    }




    // get addon prefix
    const auto prefixPath = inputDirectory / "$PBOPREFIX$";


    std::vector<PboProperty> pboProperties;

    // write extra header extensions
    for (int i = 0; i < args.num_headerextensions && args.headerextensions[i][0] != 0; i++) {

        std::string_view ext = args.headerextensions[i];
        auto seperatorOffset = ext.find_first_of('=');

        if (seperatorOffset == std::string::npos) { //no seperator found
            logger.error("Invalid header extension format (%s).\n", args.headerextensions[i]);
            return 6;
        }

        std::string_view key = ext.substr(0, seperatorOffset);
        std::string_view val = ext.substr(seperatorOffset + 1);

        if (key == "prefix") continue;


        pboProperties.emplace_back(std::string(key), std::string(val));
    }

    std::string_view addonPrefix;

    if (auto found = std::find_if(pboProperties.begin(), pboProperties.end(), [](const PboProperty& prop) {
        return prop.key == "prefix";
    }); found != pboProperties.end()) {
        //Prefix supplied via parameters
        std::string prefixUnclean(found->value);
        std::replace(found->value.begin(), found->value.end(), '/', '\\');
        if (prefixUnclean != found->value)
            logger.warning("Prefix name contains forward slashes: %s\n", prefixUnclean.c_str());
        addonPrefix = found->value;
    }
    else {
        //No prefix supplied via parameters. Read it from file and clean it up.
        std::ifstream prefixFile(prefixPath);
        std::string tmp;
        std::getline(prefixFile, tmp);


        std::string prefixUnclean(tmp);
        std::replace(tmp.begin(), tmp.end(), '/', '\\');
        if (prefixUnclean != tmp)
            logger.warning("Prefix name contains forward slashes: %s\n", prefixUnclean.c_str());

        addonPrefix = pboProperties.emplace_back("prefix", std::move(tmp)).value;
        //#TODO verbose log that prefix was read from file
    }





    std::ofstream outputFile(targetPbo, std::ofstream::binary);
    if (!outputFile.is_open()) {
        logger.error("Failed to open %s.\n", targetPbo.c_str());
        return 2;
    }

    ScopeGuard targetPboDeletionGuard([&targetPbo, &outputFile]() {
        outputFile.close();
        remove_file(targetPbo);
    });

    // create and prepare temp folder
    auto tempfolder = create_temp_folder(addonPrefix.data());
    if (!tempfolder) {
        logger.error("Failed to create temp folder.\n");
        return 2;
    }

    ScopeGuard tempFolderDeletionGuard([&tempfolder]() {
        remove_folder(*tempfolder);
    });




    static __itt_string_handle* handle_copy_directory = __itt_string_handle_create("copy_directory");

    __itt_task_begin(fsDomain, __itt_null, __itt_null, handle_copy_directory);
    try {
        std::filesystem::copy(args.positionals[1], *tempfolder, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    } catch (std::filesystem::filesystem_error& ex) {
        logger.error("Failed to copy to temp folder. %s \nFrom: %s\nTo: %s", ex.what(), ex.path1().string().c_str(), ex.path2().string().c_str());
        __itt_task_end(fsDomain);
        return 3;
    }

    __itt_task_end(fsDomain);

    // preprocess and binarize stuff if required
    if (!args.packonly &&
        !std::filesystem::exists(inputDirectory / "$NOBIN$") &&
        !std::filesystem::exists(inputDirectory / "$NOBIN-NOTEST$")) {
        if (traverse_directory(tempfolder->string().c_str(), [this](const std::filesystem::path& root, const std::filesystem::path& file) {
            binarize_callback(root, file);
        })) {
            current_target = args.positionals[1];
            logger.error("Failed to binarize some files.\n");
            return 4;
        }

        //We have a config.bin now, so we want to delete the old

        auto configPath = *tempfolder / "config.cpp";
        if (std::filesystem::exists(configPath) && !std::filesystem::remove(configPath))
            return 5; //delete failed
    }

    current_target = args.positionals[1];

    __itt_task_begin(buildDomain, __itt_null, __itt_null, handle_prepWrite);

    PboWriter writer;

    for (auto& it : pboProperties) {
        writer.addProperty(it);
    }

    for (auto& file : files_sizes) {
        writer.addFile(file);
    }
    __itt_task_end(buildDomain);

    __itt_task_begin(buildDomain, __itt_null, __itt_null, handle_writepbo);

    writer.writePbo(outputFile);
    __itt_task_end(buildDomain);
    //#TODO reuse writer's file list to generate bisign

    targetPboDeletionGuard.dismiss();
    tempFolderDeletionGuard.dismiss();
    // remove temp folder
    if (!remove_folder(*tempfolder)) {
        logger.error("Failed to remove temp folder.\n");
        return 11;
    }

    // sign pbo
    if (args.privatekey) {
        char keyname[512];
        char path_signature[2048];

        if (strcmp(strrchr(args.privatekey, '.'), ".biprivatekey") != 0) {
            logger.error("File %s doesn't seem to be a valid private key.\n", args.positionals[1]);
            return 1;
        }

        if (strchr(args.privatekey, PATHSEP) == NULL)
            strcpy(keyname, args.privatekey);
        else
            strcpy(keyname, strrchr(args.privatekey, PATHSEP) + 1);
        *strrchr(keyname, '.') = 0;

        if (args.signature) {
            strcpy(path_signature, args.signature);
            if (strlen(path_signature) < 7 || strcmp(&path_signature[strlen(path_signature) - 7], ".bisign") != 0)
                strcat(path_signature, ".bisign");
        }
        else {
            strcpy(path_signature, args.positionals[2]); //target pbo path
            strcat(path_signature, ".");
            strcat(path_signature, keyname);
            strcat(path_signature, ".bisign");
        }

        // check if target already exists
        if (std::filesystem::exists(path_signature) && !args.force) {
            logger.error("File %s already exists and --force was not set.\n", path_signature);
            return 2;
        }

        if (sign_pbo(args.positionals[2], args.privatekey, path_signature)) {
            logger.error("Failed to sign file.\n");
            return 3;
        }
    }

    return 0;







}

int cmd_build(Logger& logger) {
    extern std::string current_target;

    if (args.num_positionals != 3)
        return 128;

    current_target = args.positionals[1];

    // remove trailing slash in source
    const std::filesystem::path sourceDirectory([]() {
        std::string_view sourceDirectory = args.positionals[1];
        if (sourceDirectory.back() == '\\' || sourceDirectory.back() == '/')
            sourceDirectory = sourceDirectory.substr(0, sourceDirectory.length() - 1);
        return sourceDirectory;
    }());
    std::string_view targetPbo = args.positionals[2];

    Builder builder(logger);

    for (int j = 0; j < args.num_excludefiles; j++) {
        builder.excludeFiles.emplace_back(args.excludefiles[j]);
    }



    return builder.buildDirectory(sourceDirectory, targetPbo);
}
