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


#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>

#include "args.h"
#include "rapify.h"
#include "derapify.h"
#include <filesystem>

int derapify_file(char *source, char *target, Logger& logger) {
    /*
     * Reads the rapified file in source and writes it as a human-readable
     * config into target. If the source file isn't a rapified file, -1 is
     * returned. 0 is returned on success and a positive integer on failure.
     */

    extern std::string current_target;
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
         return derapify_file(std::cin, std::cout, logger);
        else
            return derapify_file(std::cin, std::ofstream(target), logger);
    else
        if (toConsoleOutput)
            return derapify_file(std::ifstream(source, std::ifstream::binary), std::cout, logger);
        else
            return derapify_file(std::ifstream(source, std::ifstream::binary), std::ofstream(target), logger);
}
int derapify_file(std::istream& source, std::ostream& target, Logger& logger) {
    auto cfg = Config::fromBinarized(source, logger);

    if (!cfg.hasConfig()) {
        logger.error("Failed to derapify root class.\n");
        return 1;
    }
    extern struct arguments args;

    cfg.toPlainText(target, logger, args.indent ? args.indent : "    ");

    return 0;
}

int cmd_derapify(Logger& logger) {
    extern struct arguments args;
    int success;

    if (args.num_positionals == 1) {
        success = derapify_file("-", "-", logger);
    } else if (args.num_positionals == 2) {
        success = derapify_file(args.positionals[1], "-", logger);
    } else {
        // check if target already exists
        if (std::filesystem::exists(args.positionals[2]) && !args.force) {
            logger.error("File %s already exists and --force was not set.\n", args.positionals[2]);
            return 1;
        }
        //#TODO check if source exists. else throw error

        success = derapify_file(args.positionals[1], args.positionals[2], logger);
    }

    return abs(success);
}
