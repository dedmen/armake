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


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <list>
#include <vector>
#include <utility>
#include <optional>


#define MAXCONSTS 4096
#define MAXARGS 32
#define MAXINCLUDES 64
#define FILEINTERVAL 32
#define LINEINTERVAL 1024


struct constant {
    std::string name;
    std::string value;
    int num_args;
    int num_occurences;
    //offset, length
    std::vector<std::pair<size_t, size_t>> occurrences;
};

struct lineref {
    std::vector<std::string> file_names;
    std::vector<uint32_t> file_index;
    std::vector<uint32_t> line_number;
};

struct constant_stack {
    std::list<std::list<constant>::iterator> stack;
};

bool matches_includepath(const char *path, const char *includepath, const char *includefolder);
int find_file_helper(const char *includepath, const char *origin, char *includefolder, char *actualpath, const char *cwd);
int find_file(const char *includepath, const char *origin, char *actualpath);


class Preprocessor {
    std::vector<std::string> include_stack;
    //Replace block comments by empty lines instead of ommiting them
    bool keepLineCount = true;
    struct lineref lineref;

    bool constants_parse(std::list<constant> &constants, std::string_view definition, int line);
    std::optional<std::string> constants_preprocess(std::list<constant> &constants, std::string_view source, int line, constant_stack & constant_stack);

    std::optional<std::string> constant_value(std::list<constant> &constants, std::list<constant>::iterator constant,
        int num_args, std::vector<std::string>& args, int value, constant_stack &constant_stack);
    void constant_free(struct constant *constant);


    char * resolve_macros(char *string, size_t buffsize, std::list<constant> &constants);

public:

    int preprocess(char *source, std::ostream &f_target, std::list<constant> &constants);
    int preprocess(const char* sourceFileName, std::istream &input, std::ostream &output, std::list<constant> &constants);
    struct lineref& getLineref() { return lineref; }

};

