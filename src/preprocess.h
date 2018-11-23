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


#include <cstdint>
#include <list>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <map>
#include <unordered_map>
#include <filesystem>
#include "logger.h"


#define MAXCONSTS 4096
#define MAXARGS 32
#define MAXINCLUDES 64
#define FILEINTERVAL 32
#define LINEINTERVAL 1024



struct lineref {
    std::vector<std::string> file_names;
    std::vector<uint32_t> file_index;
    std::vector<uint32_t> line_number;
    //signal that there are no file names or numbers or line numbers here
    //For when text didn't go through preproc
    bool empty = false;
};

bool matches_includepath(std::filesystem::path path, std::string_view includepath, std::string_view includefolder);
std::optional<std::filesystem::path> find_file_helper(std::string_view includepath, std::string_view origin, std::string_view includefolder);
std::optional<std::filesystem::path> find_file(std::string_view includepath, std::string_view origin);


class Preprocessor {
public:
    struct constant {
        std::string name;
        std::string value;
        int num_args;
        int num_occurences;
        //offset, length
        std::vector<std::pair<size_t, size_t>> occurrences;
    };

    using ConstantMapType = std::unordered_map<std::string, constant>;//, std::less<>

    struct constant_stack {
        std::list<ConstantMapType::const_iterator> stack;
    };
private:
    std::vector<std::string> include_stack;
    //Replace block comments by empty lines instead of ommiting them
    bool keepLineCount = true;
    struct lineref lineref;
    bool constants_parse(ConstantMapType &constants, std::string_view definition, int line);
    std::optional<std::string> constants_preprocess(const ConstantMapType &constants, std::string_view source, int line, constant_stack & constant_stack);
    std::optional<std::string> constant_value(const ConstantMapType &constants, ConstantMapType::const_iterator constant,
        int num_args, std::vector<std::string>& args, int value, constant_stack &constant_stack);

    char * resolve_macros(char *string, size_t buffsize, ConstantMapType &constants);

    std::string_view current_file; //used for logging errors

    Logger& logger;
public:
    Preprocessor(Logger& logger) : logger(logger){}


    int preprocess(char *source, std::ostream &f_target, ConstantMapType &constants);
    int preprocess(std::string_view sourceFileName, std::istream &input, std::ostream &output, ConstantMapType &constants);
    struct lineref& getLineref() { return lineref; }

};

