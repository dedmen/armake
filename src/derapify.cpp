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
#include <math.h>
#include <iostream>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

#include "args.h"
#include "filesystem.h"
#include "rapify.h"
#include "utils.h"
#include "derapify.h"
#include <filesystem>


int skip_array(FILE *f) {
    uint8_t type;
    uint32_t num_entries;

    for (num_entries = read_compressed_int(f); num_entries > 0; num_entries--) {
        type = fgetc(f);

        if (type == 0 || type == 4)
            while (fgetc(f) != 0);
        else if (type == 1 || type == 2)
            fseek(f, 4, SEEK_CUR);
        else
            skip_array(f);
    }

    return 0;
}


int seek_config_path(FILE *f, char *config_path) {
    /*
     * Assumes the file pointer f points at the start of a rapified
     * class body. The config path should be formatted like one used
     * by the ingame commands (case insensitive):
     *
     *   CfgExample >> MyClass >> MyValue
     *
     * This function then moves the file pointer to the start of the
     * desired class entry (or class body for classes).
     *
     * Returns a positive integer on failure, a 0 on success and -1
     * if the given path doesn't exist.
     */

    int i;
    uint8_t type;
    uint32_t num_entries;
    uint32_t fp;
    char path[2048];
    char target[512];
    char buffer[512];

    // Trim leading spaces
    strncpy(path, config_path, sizeof(path));
    trim_leading(path, sizeof(path));

    // Extract first element
    for (i = 0; i < sizeof(target) - 1; i++) {
        if (path[i] == 0 || path[i] == '>' || path[i] == ' ')
            break;
    }
    strncpy(target, path, i);
    target[i] = 0;

    // Inherited classname
    while (fgetc(f) != 0);

    num_entries = read_compressed_int(f);

    for (i = 0; i < num_entries; i++) {
        fp = ftell(f);
        type = fgetc(f);

        if (type == 0) { // class
            if (fgets(buffer, sizeof(buffer), f) == NULL)
                return 1;

            fseek(f, fp + strlen(buffer) + 2, SEEK_SET);
            fread(&fp, 4, 1, f);

            if (stricmp(buffer, target))
                continue;

            fseek(f, fp, SEEK_SET);

            if (strchr(path, '>') == NULL)
                return 0;

            strcpy(path, strchr(config_path, '>') + 2);
            return seek_config_path(f, path);
        } else if (type == 1) { // value
            type = fgetc(f);

            if (fgets(buffer, sizeof(buffer), f) == NULL)
                return 1;

            if (stricmp(buffer, target) == 0) {
                fseek(f, fp, SEEK_SET);
                return 0;
            }

            fseek(f, fp + strlen(buffer) + 3, SEEK_SET);

            if (type == 0)
                while (fgetc(f) != 0);
            else
                fseek(f, 4, SEEK_CUR);
        } else if (type == 2) { // array
            if (fgets(buffer, sizeof(buffer), f) == NULL)
                return 1;

            if (stricmp(buffer, target) == 0) {
                fseek(f, fp, SEEK_SET);
                return 0;
            }

            fseek(f, fp + strlen(buffer) + 2, SEEK_SET);

            skip_array(f);
        } else { // extern & delete statements
            while (fgetc(f) != 0);
        }
    }

    return -1;
}


int find_parent(FILE *f, char *config_path, char *buffer, size_t buffsize) {
    /*
     * Takes a config path and returns the parent class of that class.
     * Assumes the given config path points to an existing class.
     *
     * Returns -1 if the class doesn't have a parent class, -2 if that
     * class cannot be found, 0 on success and a positive integer
     * on failure.
     */

    int i;
    int success;
    bool is_root;
    char containing[2048];
    char name[2048];
    char parent[2048];

    // Loop up class
    fseek(f, 16, SEEK_SET);
    if (seek_config_path(f, config_path))
        return 1;

    // Get parent class name
    if (fgets(parent, sizeof(parent), f) == NULL)
        return 2;
    lower_case(parent);

    if (strlen(parent) == 0)
        return -1;

    // Extract class name and the name of the containing class
    is_root = strchr(config_path, '>') == NULL;
    if (is_root) {
        strncpy(name, config_path, sizeof(name));

        containing[0] = 0;
    } else {
        strncpy(name, strrchr(config_path, '>') + 1, sizeof(name));
        trim_leading(name, sizeof(name));

        strncpy(containing, config_path, sizeof(containing));
        *(strrchr(containing, '>') - 1) = 0;
        for (i = strlen(containing) - 1; i >= 0 && containing[i] == ' '; i--)
            containing[i] = 0;
    }
    lower_case(name);

    // Check parent class inside same containing class
    if (strcmp(name, parent) != 0) {
        sprintf(buffer, "%s >> %s", containing, parent);

        fseek(f, 16, SEEK_SET);
        success = seek_config_path(f, buffer);
        if (success == 0)
            return 0;
    }

    // If this is a root class, we can't do anything at this point
    if (is_root)
        return -2;

    // Try to find the class parent in the parent of the containing class
    success = find_parent(f, containing, buffer, sizeof(buffer));
    if (success > 0)
        return success;
    if (success < 0)
        return -2;

    strcat(buffer, " >> ");
    strcat(buffer, parent);
    return 0;
}


int seek_definition(FILE *f, char *config_path) {
    /*
     * Finds the definition of the given value, even if it is defined in a
     * parent class.
     *
     * Returns 0 on success, a positive integer on failure.
     */

    int success;

    // Try the direct way first
    fseek(f, 16, SEEK_SET);
    success = seek_config_path(f, config_path);
    if (success >= 0)
        return success;

    // No containing class
    if (strchr(config_path, '>') == NULL)
        return 1;

    // Try to find the definition
    int i;
    char containing[2048];
    char parent[2048];
    char value[2048];

    strncpy(containing, config_path, sizeof(containing));
    *(strrchr(containing, '>') - 1) = 0;
    for (i = strlen(containing) - 1; i >= 0 && containing[i] == ' '; i--)
        containing[i] = 0;

    fseek(f, 16, SEEK_SET);
    success = seek_config_path(f, containing);

    // Containing class doesn't even exist
    if (success < 0)
        return success;

    // Find parent of the containing class
    success = find_parent(f, containing, parent, sizeof(parent));
    if (success) {
        return success;
    }

    strncpy(value, strrchr(config_path, '>') + 1, sizeof(value));
    trim_leading(value, sizeof(value));

    strcat(parent, " >> ");
    strcat(parent, value);

    return seek_definition(f, parent);
}


int read_string(FILE *f, char *config_path, char *buffer, size_t buffsize) {
    /*
     * Reads the given config string into the given buffer.
     *
     * Returns -1 if the value could not be found, 0 on success
     * and a positive integer on failure.
     */

    int success;
    long fp;
    uint8_t temp;

    success = seek_definition(f, config_path);
    if (success != 0)
        return success;

    temp = fgetc(f);
    if (temp != 1)
        return 1;

    temp = fgetc(f);
    if (temp != 0)
        return 2;

    while (fgetc(f) != 0);

    fp = ftell(f);

    if (fgets(buffer, buffsize, f) == NULL)
        return 3;

    fseek(f, fp + strlen(buffer) + 1, SEEK_SET);

    return 0;
}


int read_int(FILE *f, char *config_path, int32_t *result) {
    /*
     * Reads the given integer from config.
     *
     * Returns -1 if the value could not be found, 0 on success
     * and a positive integer on failure.
     */

    int success;
    uint8_t temp;

    success = seek_definition(f, config_path);
    if (success != 0)
        return success;

    temp = fgetc(f);
    if (temp != 1)
        return 1;

    temp = fgetc(f);
    if (temp != 2)
        return 2;

    while (fgetc(f) != 0);

    fread(result, 4, 1, f);

    return 0;
}


int read_float(FILE *f, char *config_path, float *result) {
    /*
     * Reads the given float from config.
     *
     * Returns -1 if the value could not be found, 0 on success
     * and a positive integer on failure.
     */

    int success;
    uint8_t temp;

    success = seek_definition(f, config_path);
    if (success != 0)
        return success;

    temp = fgetc(f);
    if (temp != 1)
        return 1;

    temp = fgetc(f);

    while (fgetc(f) != 0);

    if (temp == 2) {
        // Convert integer to float
        int32_t int_value;

        fread(&int_value, 4, 1, f);
        *result = (float)int_value;
    } else if (temp == 0) {
        // Try to parse "rad X" strings
        char string_value[512];
        char *endptr;
        long fp;

        fp = ftell(f);
        if (fgets(string_value, sizeof(string_value), f) == NULL)
            return 2;
        fseek(f, fp + strlen(string_value) + 1, SEEK_SET);

        trim_leading(string_value, sizeof(string_value));
        lower_case(string_value);

        if (strncmp(string_value, "rad ", 4) != 0)
            return 3;

        *result = strtof(string_value + 4, &endptr);
        if (strlen(endptr) > 0)
            return 4;

        *result *= RAD2DEG;
    } else {
        fread(result, 4, 1, f);
    }

    return 0;
}


int read_long_array(FILE *f, char *config_path, int32_t *array, int size) {
    /*
     * Reads the given array from config. size should be the maximum number of
     * elements in the array, buffsize the length of the individual buffers.
     *
     * Returns -1 if the value could not be found, 0 on success
     * and a positive integer on failure.
     */

    int i;
    int success;
    uint8_t temp;
    uint32_t num_entries;
    float float_value;

    success = seek_definition(f, config_path);
    if (success != 0)
        return success;

    temp = fgetc(f);
    if (temp != 2)
        return 1;

    while (fgetc(f) != 0);

    num_entries = read_compressed_int(f);

    for (i = 0; i < num_entries; i++) {
        // Array is full
        if (i == size)
            return 2;

        temp = fgetc(f);
        if (temp != 1 && temp != 2)
            return 3;

        if (fread(&array[i], sizeof(int32_t), 1, f) != 1)
            return 3;

        if (temp == 1) {
            memcpy(&float_value, &array[i], sizeof(int32_t));
            array[i] = (int32_t)float_value;
        }
    }

    return 0;
}


int read_float_array(FILE *f, char *config_path, float *array, int size) {
    /*
     * Reads the given array from config. size should be the maximum number of
     * elements in the array, buffsize the length of the individual buffers.
     *
     * Returns -1 if the value could not be found, 0 on success
     * and a positive integer on failure.
     */

    int i;
    int success;
    uint8_t temp;
    uint32_t num_entries;
    uint32_t long_value;

    success = seek_definition(f, config_path);
    if (success != 0)
        return success;

    temp = fgetc(f);
    if (temp != 2)
        return 1;

    while (fgetc(f) != 0);

    num_entries = read_compressed_int(f);

    for (i = 0; i < num_entries; i++) {
        // Array is full
        if (i == size)
            return 2;

        temp = fgetc(f);
        if (temp != 1 && temp != 2)
            return 3;

        if (fread(&array[i], sizeof(float), 1, f) != 1)
            return 3;

        if (temp == 2) {
            memcpy(&long_value, &array[i], sizeof(float));
            array[i] = (float)long_value;
        }
    }

    return 0;
}


int read_string_array(FILE *f, char *config_path, std::vector<std::string> output, size_t buffsize) {
    /*
     * Reads the given array from config. size should be the maximum number of
     * elements in the array, buffsize the length of the individual buffers.
     *
     * Returns -1 if the value could not be found, 0 on success
     * and a positive integer on failure.
     */
    std::vector<char> buffer;
    buffer.resize(buffsize);

    int success = seek_definition(f, config_path);
    if (success != 0)
        return success;

    uint8_t temp = fgetc(f);
    if (temp != 2)
        return 1;

    while (fgetc(f) != 0);

    uint32_t num_entries = read_compressed_int(f);

    for (uint32_t i = 0; i < num_entries; i++) {
        temp = fgetc(f);
        if (temp != 0)
            return 3;

        long fp = ftell(f);

        if (fgets(buffer.data(), buffer.size(), f) == NULL)
            return 3;
        auto it = output.emplace_back(buffer.data());
        
        fseek(f, fp + it.length() + 1, SEEK_SET);
    }

    return 0;
}


int read_classes(FILE *f, char *config_path, std::vector<std::string> output, size_t buffsize) {
    /*
     * Reads all subclass names for the given config path into the given
     * array.
     *
     * Returns a positive integer on failure, a 0 on success and -1
     * if the given path doesn't exist.
     */

    int i;
    int j;
    int success;
    uint8_t type;
    uint32_t num_entries;
    uint32_t fp;
    //char target[512];
    std::vector<char> buffer;
    buffer.resize(buffsize);

    fseek(f, 16, SEEK_SET);
    success = seek_config_path(f, config_path);
    if (success)
        return success;

    // Inherited classname
    while (fgetc(f) != 0);

    num_entries = read_compressed_int(f);

    for (i = 0; i < num_entries; i++) {
        fp = ftell(f);
        type = fgetc(f);

        if (type == 0) { // class
            if (fgets(buffer.data(), buffer.size(), f) == NULL)
                return 1;

            auto newElement = output.emplace_back(buffer.data());

            fseek(f, fp + newElement.length() + 6, SEEK_SET);
        } else if (type == 1) { // value
            type = fgetc(f);

            if (fgets(buffer.data(), buffer.size(), f) == NULL)
                return 1;

            std::transform(buffer.begin(), buffer.end(), buffer.begin(), tolower);

            //https://github.com/KoffeinFlummi/armake/pull/86#issuecomment-436292204
            //if (strcmp(buffer.data(), target) == 0) {
            //    fseek(f, fp, SEEK_SET);
            //    return 0;
            //}

            fseek(f, fp + strlen(buffer.data()) + 3, SEEK_SET);

            if (type == 0 || type == 4)
                while (fgetc(f) != 0);
            else
                fseek(f, 4, SEEK_CUR);
        } else if (type == 2) { // array
            if (fgets(buffer.data(), buffer.size(), f) == NULL)
                return 1;

            std::transform(buffer.begin(), buffer.end(), buffer.begin(), tolower);

            //https://github.com/KoffeinFlummi/armake/pull/86#issuecomment-436292204
            //if (strcmp(buffer.data(), target) == 0) {
            //    fseek(f, fp, SEEK_SET);
            //    return 0;
            //}

            fseek(f, fp + strlen(buffer.data()) + 2, SEEK_SET);

            skip_array(f);
        } else { // extern & delete statements
            while (fgetc(f) != 0);
        }
    }

    return 0;
}



int derapify_file(char *source, char *target) {
    /*
     * Reads the rapified file in source and writes it as a human-readable
     * config into target. If the source file isn't a rapified file, -1 is
     * returned. 0 is returned on success and a positive integer on failure.
     */

    extern const char *current_target;
    FILE *f_source;
    FILE *f_target;
    char buffer[4096];
    int bytes;
    int success;
#ifdef _WIN32
    char temp_name[2048];
#endif

    if (strcmp(source, "-") == 0)
        current_target = "stdin";
    else
        current_target = source;

    bool fromConsoleInput = strcmp(source, "-") == 0;
    bool toConsoleOutput = strcmp(target, "-") == 0;

    //#TODO check if input is rapified
    //if (strncmp(buffer, "\0raP", 4) != 0) {
    //    errorf("Source file is not a rapified config.\n");
    //    if (strcmp(source, "-") != 0)
    //        fclose(f_source);
    //    return -3;
    //}

    if (fromConsoleInput)
        if (toConsoleOutput)
         return derapify_file(std::cin, std::cout);
        else
            return derapify_file(std::cin, std::ofstream(target));
    else
        if (toConsoleOutput)
            return derapify_file(std::ifstream(source, std::ifstream::binary), std::cout);
        else
            return derapify_file(std::ifstream(source, std::ifstream::binary), std::ofstream(target));
}
int derapify_file(std::istream& source, std::ostream& target) {
    auto cfg = Config::fromBinarized(source);

    if (!cfg.hasConfig()) {
        errorf("Failed to derapify root class.\n");
        return 1;
    }
    cfg.toPlainText(target);

    return 0;
}

int cmd_derapify() {
    extern struct arguments args;
    int success;

    if (args.num_positionals == 1) {
        success = derapify_file("-", "-");
    } else if (args.num_positionals == 2) {
        success = derapify_file(args.positionals[1], "-");
    } else {
        // check if target already exists
        if (std::filesystem::exists(args.positionals[2]) && !args.force) {
            errorf("File %s already exists and --force was not set.\n", args.positionals[2]);
            return 1;
        }
        //#TODO check if source exists. else throw error

        success = derapify_file(args.positionals[1], args.positionals[2]);
    }

    return abs(success);
}
